#include "tracesmith/format/sbt_format.hpp"
#include <iostream>

namespace tracesmith {

// ============================================================================
// SBTWriter Implementation
// ============================================================================

SBTWriter::SBTWriter(const std::string& filename)
    : filename_(filename)
    , event_count_(0)
    , events_start_offset_(0)
    , first_timestamp_(0)
    , last_timestamp_(0)
    , finalized_(false) {
    
    file_.open(filename, std::ios::binary | std::ios::out);
    
    if (file_.is_open()) {
        // Write placeholder header (will be updated in finalize)
        file_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
    }
}

SBTWriter::~SBTWriter() {
    if (!finalized_ && file_.is_open()) {
        finalize();
    }
}

uint32_t SBTWriter::internString(const std::string& str) {
    auto it = string_table_.find(str);
    if (it != string_table_.end()) {
        return it->second;
    }
    
    uint32_t index = static_cast<uint32_t>(string_list_.size());
    string_table_[str] = index;
    string_list_.push_back(str);
    return index;
}

void SBTWriter::writeVarInt(uint64_t value) {
    // Write variable-length integer (7 bits per byte, MSB indicates continuation)
    while (value >= 0x80) {
        uint8_t byte = (value & 0x7F) | 0x80;
        file_.write(reinterpret_cast<const char*>(&byte), 1);
        value >>= 7;
    }
    uint8_t byte = static_cast<uint8_t>(value);
    file_.write(reinterpret_cast<const char*>(&byte), 1);
}

void SBTWriter::writeString(const std::string& str) {
    writeVarInt(str.size());
    file_.write(str.data(), str.size());
}

void SBTWriter::writeEventCompact(const TraceEvent& event) {
    // Event format:
    // - type (1 byte)
    // - flags (1 byte): has_duration, has_kernel_params, has_memory_params, has_callstack
    // - timestamp delta (varint)
    // - duration (varint, if present)
    // - device_id (varint)
    // - stream_id (varint)
    // - correlation_id (varint)
    // - name_index (varint)
    // - kernel_params (if present)
    // - memory_params (if present)
    // - callstack (if present)
    
    uint8_t type = static_cast<uint8_t>(event.type);
    file_.write(reinterpret_cast<const char*>(&type), 1);
    
    uint8_t flags = 0;
    if (event.duration > 0) flags |= 0x01;
    if (event.kernel_params.has_value()) flags |= 0x02;
    if (event.memory_params.has_value()) flags |= 0x04;
    if (event.call_stack.has_value() && !event.call_stack->empty()) flags |= 0x08;
    file_.write(reinterpret_cast<const char*>(&flags), 1);
    
    // Timestamp delta encoding
    uint64_t ts_delta = event.timestamp - last_timestamp_;
    writeVarInt(ts_delta);
    
    if (flags & 0x01) {
        writeVarInt(event.duration);
    }
    
    writeVarInt(event.device_id);
    writeVarInt(event.stream_id);
    writeVarInt(event.correlation_id);
    
    // Name (interned string)
    uint32_t name_index = internString(event.name);
    writeVarInt(name_index);
    
    // Kernel params
    if (flags & 0x02) {
        const auto& kp = event.kernel_params.value();
        writeVarInt(kp.grid_x);
        writeVarInt(kp.grid_y);
        writeVarInt(kp.grid_z);
        writeVarInt(kp.block_x);
        writeVarInt(kp.block_y);
        writeVarInt(kp.block_z);
        writeVarInt(kp.shared_mem_bytes);
        writeVarInt(kp.registers_per_thread);
    }
    
    // Memory params
    if (flags & 0x04) {
        const auto& mp = event.memory_params.value();
        writeVarInt(mp.src_address);
        writeVarInt(mp.dst_address);
        writeVarInt(mp.size_bytes);
    }
    
    // Call stack
    if (flags & 0x08) {
        const auto& cs = event.call_stack.value();
        writeVarInt(cs.thread_id);
        writeVarInt(cs.frames.size());
        for (const auto& frame : cs.frames) {
            writeVarInt(frame.address);
            uint32_t func_idx = internString(frame.function_name);
            uint32_t file_idx = internString(frame.file_name);
            writeVarInt(func_idx);
            writeVarInt(file_idx);
            writeVarInt(frame.line_number);
        }
    }
}

SBTResult SBTWriter::writeMetadata(const TraceMetadata& metadata) {
    if (!file_.is_open()) {
        return SBTResult("File not open");
    }
    
    header_.metadata_offset = static_cast<uint64_t>(file_.tellp());
    
    // Write metadata section
    uint8_t section_type = static_cast<uint8_t>(sbt::SectionType::Metadata);
    file_.write(reinterpret_cast<const char*>(&section_type), 1);
    
    writeString(metadata.application_name);
    writeString(metadata.command_line);
    writeVarInt(metadata.start_time);
    writeVarInt(metadata.end_time);
    writeString(metadata.hostname);
    writeVarInt(metadata.process_id);
    
    return SBTResult(true);
}

SBTResult SBTWriter::writeDeviceInfo(const std::vector<DeviceInfo>& devices) {
    if (!file_.is_open()) {
        return SBTResult("File not open");
    }
    
    header_.device_info_offset = static_cast<uint64_t>(file_.tellp());
    
    uint8_t section_type = static_cast<uint8_t>(sbt::SectionType::DeviceInfo);
    file_.write(reinterpret_cast<const char*>(&section_type), 1);
    
    writeVarInt(devices.size());
    
    for (const auto& dev : devices) {
        writeVarInt(dev.device_id);
        writeString(dev.name);
        writeString(dev.vendor);
        writeVarInt(dev.compute_major);
        writeVarInt(dev.compute_minor);
        writeVarInt(dev.total_memory);
        writeVarInt(dev.memory_clock_rate);
        writeVarInt(dev.memory_bus_width);
        writeVarInt(dev.multiprocessor_count);
        writeVarInt(dev.max_threads_per_mp);
        writeVarInt(dev.clock_rate);
        writeVarInt(dev.warp_size);
    }
    
    return SBTResult(true);
}

SBTResult SBTWriter::writeEvent(const TraceEvent& event) {
    if (!file_.is_open()) {
        return SBTResult("File not open");
    }
    
    if (event_count_ == 0) {
        // First event - mark events section start
        header_.events_offset = static_cast<uint64_t>(file_.tellp());
        
        uint8_t section_type = static_cast<uint8_t>(sbt::SectionType::Events);
        file_.write(reinterpret_cast<const char*>(&section_type), 1);
        
        first_timestamp_ = event.timestamp;
        last_timestamp_ = event.timestamp;
        
        // Write base timestamp
        writeVarInt(first_timestamp_);
    }
    
    writeEventCompact(event);
    last_timestamp_ = event.timestamp;
    event_count_++;
    
    return SBTResult(true);
}

SBTResult SBTWriter::writeEvents(const std::vector<TraceEvent>& events) {
    for (const auto& event : events) {
        auto result = writeEvent(event);
        if (!result) {
            return result;
        }
    }
    return SBTResult(true);
}

SBTResult SBTWriter::finalize() {
    if (!file_.is_open()) {
        return SBTResult("File not open");
    }
    
    if (finalized_) {
        return SBTResult("Already finalized");
    }
    
    // Write string table
    header_.string_table_offset = static_cast<uint64_t>(file_.tellp());
    
    uint8_t section_type = static_cast<uint8_t>(sbt::SectionType::StringTable);
    file_.write(reinterpret_cast<const char*>(&section_type), 1);
    
    writeVarInt(string_list_.size());
    for (const auto& str : string_list_) {
        writeString(str);
    }
    
    // Write EOF marker
    section_type = static_cast<uint8_t>(sbt::SectionType::EndOfFile);
    file_.write(reinterpret_cast<const char*>(&section_type), 1);
    
    // Update header with final values
    header_.event_count = event_count_;
    if (!string_list_.empty()) {
        header_.flags |= sbt::FLAG_HAS_CALLSTACKS;  // Simplified flag usage
    }
    
    // Seek back and write final header
    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
    
    file_.close();
    finalized_ = true;
    
    return SBTResult(true);
}

uint64_t SBTWriter::fileSize() const {
    if (!file_.is_open()) {
        return 0;
    }
    return static_cast<uint64_t>(const_cast<std::ofstream&>(file_).tellp());
}

// ============================================================================
// SBTReader Implementation
// ============================================================================

SBTReader::SBTReader(const std::string& filename)
    : filename_(filename)
    , header_read_(false) {
    
    file_.open(filename, std::ios::binary | std::ios::in);
    
    if (file_.is_open()) {
        file_.read(reinterpret_cast<char*>(&header_), sizeof(header_));
        header_read_ = header_.isValid();
    }
}

SBTReader::~SBTReader() {
    if (file_.is_open()) {
        file_.close();
    }
}

uint64_t SBTReader::readVarInt() {
    uint64_t result = 0;
    int shift = 0;
    uint8_t byte;
    
    do {
        file_.read(reinterpret_cast<char*>(&byte), 1);
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    
    return result;
}

std::string SBTReader::readString() {
    uint64_t len = readVarInt();
    std::string result(len, '\0');
    file_.read(&result[0], len);
    return result;
}

bool SBTReader::readStringTable() {
    if (header_.string_table_offset == 0) {
        return true;  // No string table
    }
    
    file_.seekg(header_.string_table_offset);
    
    uint8_t section_type;
    file_.read(reinterpret_cast<char*>(&section_type), 1);
    
    if (section_type != static_cast<uint8_t>(sbt::SectionType::StringTable)) {
        return false;
    }
    
    uint64_t count = readVarInt();
    string_table_.resize(count);
    
    for (uint64_t i = 0; i < count; ++i) {
        string_table_[i] = readString();
    }
    
    return true;
}

TraceEvent SBTReader::readEventCompact() {
    TraceEvent event;
    
    uint8_t type;
    file_.read(reinterpret_cast<char*>(&type), 1);
    event.type = static_cast<EventType>(type);
    
    uint8_t flags;
    file_.read(reinterpret_cast<char*>(&flags), 1);
    
    event.timestamp = readVarInt();  // Will add delta later
    
    if (flags & 0x01) {
        event.duration = readVarInt();
    }
    
    event.device_id = static_cast<uint32_t>(readVarInt());
    event.stream_id = static_cast<uint32_t>(readVarInt());
    event.correlation_id = readVarInt();
    
    uint32_t name_index = static_cast<uint32_t>(readVarInt());
    if (name_index < string_table_.size()) {
        event.name = string_table_[name_index];
    }
    
    if (flags & 0x02) {
        KernelParams kp;
        kp.grid_x = static_cast<uint32_t>(readVarInt());
        kp.grid_y = static_cast<uint32_t>(readVarInt());
        kp.grid_z = static_cast<uint32_t>(readVarInt());
        kp.block_x = static_cast<uint32_t>(readVarInt());
        kp.block_y = static_cast<uint32_t>(readVarInt());
        kp.block_z = static_cast<uint32_t>(readVarInt());
        kp.shared_mem_bytes = static_cast<uint32_t>(readVarInt());
        kp.registers_per_thread = static_cast<uint32_t>(readVarInt());
        event.kernel_params = kp;
    }
    
    if (flags & 0x04) {
        MemoryParams mp;
        mp.src_address = readVarInt();
        mp.dst_address = readVarInt();
        mp.size_bytes = readVarInt();
        event.memory_params = mp;
    }
    
    if (flags & 0x08) {
        CallStack cs;
        cs.thread_id = readVarInt();
        uint64_t frame_count = readVarInt();
        cs.frames.resize(frame_count);
        for (uint64_t i = 0; i < frame_count; ++i) {
            cs.frames[i].address = readVarInt();
            uint32_t func_idx = static_cast<uint32_t>(readVarInt());
            uint32_t file_idx = static_cast<uint32_t>(readVarInt());
            cs.frames[i].line_number = static_cast<uint32_t>(readVarInt());
            
            if (func_idx < string_table_.size()) {
                cs.frames[i].function_name = string_table_[func_idx];
            }
            if (file_idx < string_table_.size()) {
                cs.frames[i].file_name = string_table_[file_idx];
            }
        }
        event.call_stack = cs;
    }
    
    return event;
}

SBTResult SBTReader::readMetadata(TraceMetadata& metadata) {
    if (!file_.is_open() || !header_read_) {
        return SBTResult("File not open or invalid");
    }
    
    if (header_.metadata_offset == 0) {
        return SBTResult("No metadata section");
    }
    
    file_.seekg(header_.metadata_offset);
    
    uint8_t section_type;
    file_.read(reinterpret_cast<char*>(&section_type), 1);
    
    if (section_type != static_cast<uint8_t>(sbt::SectionType::Metadata)) {
        return SBTResult("Invalid metadata section");
    }
    
    metadata.application_name = readString();
    metadata.command_line = readString();
    metadata.start_time = readVarInt();
    metadata.end_time = readVarInt();
    metadata.hostname = readString();
    metadata.process_id = static_cast<uint32_t>(readVarInt());
    
    return SBTResult(true);
}

SBTResult SBTReader::readAll(TraceRecord& record) {
    if (!file_.is_open() || !header_read_) {
        return SBTResult("File not open or invalid");
    }
    
    // Read string table first (needed for event names)
    if (!readStringTable()) {
        return SBTResult("Failed to read string table");
    }
    
    // Read metadata if present
    if (header_.metadata_offset > 0) {
        auto result = readMetadata(record.metadata());
        if (!result) {
            return result;
        }
    }
    
    // Read events
    if (header_.events_offset == 0 || header_.event_count == 0) {
        return SBTResult(true);  // No events
    }
    
    file_.seekg(header_.events_offset);
    
    uint8_t section_type;
    file_.read(reinterpret_cast<char*>(&section_type), 1);
    
    if (section_type != static_cast<uint8_t>(sbt::SectionType::Events)) {
        return SBTResult("Invalid events section");
    }
    
    // Read base timestamp
    uint64_t base_timestamp = readVarInt();
    uint64_t current_timestamp = base_timestamp;
    
    record.reserve(header_.event_count);
    
    for (uint64_t i = 0; i < header_.event_count; ++i) {
        TraceEvent event = readEventCompact();
        // Add delta to get absolute timestamp
        current_timestamp += event.timestamp;
        event.timestamp = current_timestamp;
        record.addEvent(std::move(event));
    }
    
    return SBTResult(true);
}

SBTResult SBTReader::readEvents(std::vector<TraceEvent>& events, 
                                 size_t offset, size_t count) {
    if (!file_.is_open() || !header_read_) {
        return SBTResult("File not open or invalid");
    }
    
    // Ensure string table is loaded
    if (string_table_.empty() && header_.string_table_offset > 0) {
        if (!readStringTable()) {
            return SBTResult("Failed to read string table");
        }
    }
    
    // For now, just read all and slice (TODO: optimize for large files)
    TraceRecord record;
    auto result = readAll(record);
    if (!result) {
        return result;
    }
    
    size_t end = std::min(offset + count, record.size());
    for (size_t i = offset; i < end; ++i) {
        events.push_back(record.events()[i]);
    }
    
    return SBTResult(true);
}

} // namespace tracesmith
