#include "tracesmith/common/xray_importer.hpp"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <stack>
#include <unordered_set>

namespace tracesmith {

// XRay basic mode record structure (32 bytes)
struct XRayBasicRecord {
    uint64_t timestamp;
    uint32_t function_id;
    uint32_t thread_id;
    uint8_t record_type;
    uint8_t cpu_id;
    uint8_t padding[14];
};
static_assert(sizeof(XRayBasicRecord) == 32, "XRay record must be 32 bytes");

// XRay file magic
constexpr uint32_t XRAY_MAGIC = 0x4152584C;  // "LXRA"

std::vector<TraceEvent> XRayImporter::importFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return {};
    }
    
    return importBuffer(buffer.data(), buffer.size());
}

std::vector<TraceEvent> XRayImporter::importBuffer(const uint8_t* data, size_t size) {
    raw_records_.clear();
    function_map_.clear();
    stats_ = Statistics{};
    
    if (size < sizeof(XRayFileHeader)) {
        return {};
    }
    
    if (!parseHeader(data, size)) {
        return {};
    }
    
    // Parse based on file type
    if (header_.type == 0) {
        // Basic mode
        if (!parseBasicMode(data, size)) {
            return {};
        }
    } else if (header_.type == 1) {
        // FDR mode
        if (!parseFDRMode(data, size)) {
            return {};
        }
    } else {
        // Unknown format
        return {};
    }
    
    // Resolve symbols if configured
    if (config_.resolve_symbols) {
        resolveSymbols();
    }
    
    // Convert to TraceSmith events
    return convertToEvents();
}

bool XRayImporter::parseHeader(const uint8_t* data, size_t size) {
    if (size < 16) return false;
    
    // Check magic number
    uint32_t magic;
    std::memcpy(&magic, data, sizeof(magic));
    
    // XRay files can have different formats
    // Try to detect the format
    if (magic == XRAY_MAGIC) {
        // Standard XRay format
        std::memcpy(&header_.version, data + 4, sizeof(uint16_t));
        std::memcpy(&header_.type, data + 6, sizeof(uint16_t));
        std::memcpy(&header_.cycle_frequency, data + 8, sizeof(uint32_t));
    } else {
        // Try basic mode without header (raw records)
        header_.version = 1;
        header_.type = 0;
        header_.cycle_frequency = 2400000000;  // Default 2.4 GHz
    }
    
    return true;
}

bool XRayImporter::parseBasicMode(const uint8_t* data, size_t size) {
    // Skip header if present
    size_t offset = 0;
    uint32_t magic;
    std::memcpy(&magic, data, sizeof(magic));
    if (magic == XRAY_MAGIC) {
        offset = 16;  // Skip header
    }
    
    // Parse records
    while (offset + sizeof(XRayBasicRecord) <= size) {
        XRayBasicRecord record;
        std::memcpy(&record, data + offset, sizeof(XRayBasicRecord));
        offset += sizeof(XRayBasicRecord);
        
        // Validate record type
        if (record.record_type > 4) {
            continue;  // Invalid record type
        }
        
        XRayFunctionRecord func_record;
        func_record.function_id = record.function_id;
        func_record.timestamp = record.timestamp;
        func_record.type = static_cast<XRayEntryType>(record.record_type);
        func_record.thread_id = record.thread_id;
        func_record.cpu_id = record.cpu_id;
        
        raw_records_.push_back(func_record);
        stats_.records_read++;
    }
    
    return true;
}

bool XRayImporter::parseFDRMode(const uint8_t* data, size_t size) {
    // FDR (Flight Data Recorder) mode has a more complex format
    // with buffer extents and metadata records
    
    size_t offset = 16;  // Skip header
    
    while (offset + 8 <= size) {
        // Read record type
        uint8_t record_type = data[offset];
        
        switch (record_type) {
            case 0: {
                // Buffer extent - metadata about buffer
                offset += 16;
                break;
            }
            case 1: {
                // New buffer record
                offset += 16;
                break;
            }
            case 2: {
                // End of buffer
                offset += 8;
                break;
            }
            case 3: {
                // New CPU record
                offset += 16;
                break;
            }
            case 4: {
                // Function entry/exit record (8 bytes)
                if (offset + 8 > size) break;
                
                uint64_t entry;
                std::memcpy(&entry, data + offset, sizeof(entry));
                
                XRayFunctionRecord func_record;
                func_record.type = (entry & 1) ? XRayEntryType::FunctionExit 
                                               : XRayEntryType::FunctionEnter;
                func_record.function_id = (entry >> 1) & 0xFFFFFFF;
                func_record.timestamp = entry >> 32;
                
                raw_records_.push_back(func_record);
                stats_.records_read++;
                
                offset += 8;
                break;
            }
            case 5: {
                // Custom event record
                if (offset + 16 > size) break;
                
                uint64_t size_field;
                std::memcpy(&size_field, data + offset + 8, sizeof(size_field));
                size_t event_size = size_field & 0xFFFFFFFF;
                
                stats_.custom_events++;
                offset += 16 + event_size;
                break;
            }
            case 6: {
                // Typed event record
                offset += 24;  // Fixed size typed event
                break;
            }
            default:
                // Unknown record type, try to skip
                offset += 8;
                break;
        }
    }
    
    return true;
}

std::vector<TraceEvent> XRayImporter::convertToEvents() {
    std::vector<TraceEvent> events;
    
    // Sort records by timestamp
    std::vector<XRayFunctionRecord> sorted_records = raw_records_;
    std::sort(sorted_records.begin(), sorted_records.end(),
              [](const XRayFunctionRecord& a, const XRayFunctionRecord& b) {
                  return a.timestamp < b.timestamp;
              });
    
    // Use stack to match enter/exit pairs per thread
    std::unordered_map<uint32_t, std::stack<size_t>> thread_stacks;
    
    // First pass: create events for entries
    for (size_t i = 0; i < sorted_records.size(); ++i) {
        const auto& record = sorted_records[i];
        
        if (record.type == XRayEntryType::FunctionEnter) {
            TraceEvent event(EventType::RangeStart);
            event.timestamp = tscToNanoseconds(record.timestamp);
            event.thread_id = record.thread_id;
            event.correlation_id = i;  // Use index as correlation ID
            
            // Set function name
            auto it = function_map_.find(record.function_id);
            if (it != function_map_.end()) {
                event.name = it->second;
            } else {
                event.name = "func_" + std::to_string(record.function_id);
            }
            
            // Add XRay metadata
            event.metadata["xray_func_id"] = std::to_string(record.function_id);
            event.metadata["cpu_id"] = std::to_string(record.cpu_id);
            
            events.push_back(event);
            thread_stacks[record.thread_id].push(events.size() - 1);
            
            stats_.records_converted++;
        } else if (record.type == XRayEntryType::FunctionExit || 
                   record.type == XRayEntryType::TailExit) {
            auto& stack = thread_stacks[record.thread_id];
            
            if (!stack.empty()) {
                size_t start_idx = stack.top();
                stack.pop();
                
                // Update the start event with duration
                auto& start_event = events[start_idx];
                uint64_t end_time = tscToNanoseconds(record.timestamp);
                start_event.duration = end_time - start_event.timestamp;
                
                // Apply duration filter
                if (config_.filter_short_calls && 
                    start_event.duration < config_.min_duration_ns) {
                    // Mark for removal (we'll filter later)
                    start_event.type = EventType::Unknown;
                    stats_.records_filtered++;
                }
            }
        }
    }
    
    // Remove filtered events
    if (config_.filter_short_calls) {
        events.erase(
            std::remove_if(events.begin(), events.end(),
                          [](const TraceEvent& e) { 
                              return e.type == EventType::Unknown; 
                          }),
            events.end());
    }
    
    // Calculate statistics
    if (!events.empty()) {
        auto min_ts = events.front().timestamp;
        auto max_ts = events.back().timestamp + events.back().duration;
        stats_.total_duration_ms = (max_ts - min_ts) / 1000000.0;
    }
    
    stats_.functions_identified = function_map_.size();
    
    return events;
}

void XRayImporter::resolveSymbols() {
    // Symbol resolution would typically use:
    // 1. DWARF debug info from the binary
    // 2. XRay instrumentation map
    // 3. External symbol file
    
    // For now, we generate synthetic names
    // Real implementation would use libdwarf or llvm-symbolizer
    
    std::unordered_set<uint32_t> func_ids;
    for (const auto& record : raw_records_) {
        func_ids.insert(record.function_id);
    }
    
    for (uint32_t id : func_ids) {
        // Check if we have a symbol file
        if (!config_.symbol_file.empty()) {
            // TODO: Parse symbol file to get real function names
            // For now, use placeholder
            function_map_[id] = "xray_func_" + std::to_string(id);
        } else {
            function_map_[id] = "func_" + std::to_string(id);
        }
    }
}

uint64_t XRayImporter::tscToNanoseconds(uint64_t tsc) const {
    if (header_.cycle_frequency == 0) {
        // Assume 1ns per cycle if frequency unknown
        return tsc;
    }
    
    // Convert: ns = tsc * 1e9 / frequency
    // Use portable high-precision multiplication to avoid overflow
#if defined(__GNUC__) || defined(__clang__)
    // GCC/Clang support 128-bit integers
    __uint128_t result = static_cast<__uint128_t>(tsc) * 1000000000ULL;
    return static_cast<uint64_t>(result / header_.cycle_frequency);
#else
    // Windows MSVC: use double precision (sufficient for most cases)
    // Or use split multiplication to avoid overflow
    const uint64_t ns_per_sec = 1000000000ULL;
    const uint64_t freq = header_.cycle_frequency;
    
    // Split calculation: (tsc / freq) * ns_per_sec + ((tsc % freq) * ns_per_sec) / freq
    // This avoids overflow while maintaining precision
    uint64_t whole_seconds = tsc / freq;
    uint64_t remainder = tsc % freq;
    
    return whole_seconds * ns_per_sec + (remainder * ns_per_sec) / freq;
#endif
}

} // namespace tracesmith

