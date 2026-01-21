/**
 * Tracy Importer Implementation
 * 
 * This file implements the TracyImporter class that reads Tracy file format
 * and converts the data to TraceSmith events.
 * 
 * Note: Tracy file format is complex and evolves between versions.
 * This implementation provides the framework for import, with actual
 * binary parsing to be expanded based on Tracy's FileRead implementation.
 */

#include "tracesmith/tracy/tracy_importer.hpp"

#include <fstream>
#include <cstring>
#include <algorithm>

namespace tracesmith {
namespace tracy {

// Tracy file magic number
static constexpr uint64_t TRACY_MAGIC = 0x7574636172745f79ULL;  // "y_tracy\0"

// =============================================================================
// TracyImporter Implementation
// =============================================================================

class TracyImporter::Impl {
public:
    Impl(TracyImporter& parent) : parent_(parent) {}
    
    TracyImportResult importFile(const std::string& filepath) {
        TracyImportResult result;
        
        // Open file
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            result.errors.push_back("Failed to open file: " + filepath);
            return result;
        }
        
        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (file_size < 16) {
            result.errors.push_back("File too small to be a valid Tracy file");
            return result;
        }
        
        // Read magic number
        uint64_t magic;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        
        if (magic != TRACY_MAGIC) {
            result.errors.push_back("Invalid Tracy file magic number");
            return result;
        }
        
        // Read version
        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        
        result.record.metadata().application_name = "Tracy Import";
        result.record.metadata().start_time = getCurrentTimestamp();
        
        // Report progress
        reportProgress(0.1f, "Reading Tracy file header...");
        
        // Parse file based on version
        // Note: Tracy format is complex and version-dependent
        // This is a simplified implementation that shows the structure
        
        result.warnings.push_back("Full Tracy file parsing not yet implemented. "
                                  "Using simplified import.");
        
        // For now, create a placeholder result
        reportProgress(1.0f, "Import complete");
        
        result.record.metadata().end_time = getCurrentTimestamp();
        return result;
    }
    
    TracyImportResult importFromMemory(const uint8_t* data, size_t size) {
        TracyImportResult result;
        
        if (size < 16) {
            result.errors.push_back("Data too small to be a valid Tracy file");
            return result;
        }
        
        // Read magic number
        uint64_t magic;
        std::memcpy(&magic, data, sizeof(magic));
        
        if (magic != TRACY_MAGIC) {
            result.errors.push_back("Invalid Tracy file magic number");
            return result;
        }
        
        // Read version
        uint32_t version;
        std::memcpy(&version, data + sizeof(magic), sizeof(version));
        
        result.warnings.push_back("Full Tracy memory parsing not yet implemented.");
        
        return result;
    }
    
    void reportProgress(float progress, const std::string& status) {
        if (parent_.progress_callback_) {
            parent_.progress_callback_(progress, status);
        }
    }
    
private:
    TracyImporter& parent_;
};

TracyImporter::TracyImporter()
    : impl_(std::make_unique<Impl>(*this))
    , config_()
{
}

TracyImporter::TracyImporter(const TracyImporterConfig& config)
    : impl_(std::make_unique<Impl>(*this))
    , config_(config)
{
}

TracyImporter::~TracyImporter() = default;

TracyImportResult TracyImporter::importFile(const std::string& filepath) {
    return impl_->importFile(filepath);
}

TracyImportResult TracyImporter::importFromMemory(const uint8_t* data, size_t size) {
    return impl_->importFromMemory(data, size);
}

void TracyImporter::setProgressCallback(TracyImportProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

// =============================================================================
// Static Conversion Functions
// =============================================================================

TraceEvent TracyImporter::convertZone(const TracyZone& zone) {
    TraceEvent event;
    
    // Determine event type based on zone characteristics
    if (zone.is_gpu) {
        event.type = EventType::KernelLaunch;
    } else {
        event.type = EventType::Marker;  // CPU zones become markers
    }
    
    event.name = zone.name;
    event.timestamp = zone.start_time;
    event.duration = zone.duration();
    event.thread_id = zone.thread_id;
    
    // Add source location to metadata
    if (!zone.source_file.empty()) {
        event.metadata["source_file"] = zone.source_file;
    }
    if (!zone.function.empty()) {
        event.metadata["function"] = zone.function;
    }
    if (zone.source_line > 0) {
        event.metadata["source_line"] = std::to_string(zone.source_line);
    }
    if (zone.color != 0) {
        event.metadata["color"] = std::to_string(zone.color);
    }
    event.metadata["depth"] = std::to_string(zone.depth);
    event.metadata["source"] = "tracy";
    
    return event;
}

TraceEvent TracyImporter::convertGpuZone(const TracyGpuZone& zone) {
    TraceEvent event;
    event.type = EventType::KernelLaunch;
    event.name = zone.name;
    event.timestamp = zone.gpu_start;
    event.duration = zone.gpu_duration();
    event.thread_id = zone.thread_id;
    event.device_id = zone.context_id;
    
    // Add GPU timing metadata
    event.metadata["cpu_start"] = std::to_string(zone.cpu_start);
    event.metadata["cpu_end"] = std::to_string(zone.cpu_end);
    event.metadata["gpu_start"] = std::to_string(zone.gpu_start);
    event.metadata["gpu_end"] = std::to_string(zone.gpu_end);
    event.metadata["gpu_context"] = std::to_string(zone.context_id);
    event.metadata["source"] = "tracy_gpu";
    
    if (zone.color != 0) {
        event.metadata["color"] = std::to_string(zone.color);
    }
    
    return event;
}

MemoryEvent TracyImporter::convertMemoryAlloc(const TracyMemoryAlloc& alloc, bool is_free) {
    MemoryEvent event;
    event.ptr = alloc.ptr;
    event.bytes = alloc.size;
    event.timestamp = is_free ? alloc.free_time : alloc.alloc_time;
    event.is_allocation = !is_free;
    event.thread_id = alloc.thread_id;
    event.allocator_name = alloc.pool_name.empty() ? "tracy" : alloc.pool_name;
    event.category = MemoryEvent::Category::Unknown;
    
    return event;
}

CounterEvent TracyImporter::convertPlotPoint(const TracyPlotPoint& point) {
    CounterEvent event;
    event.counter_name = point.name;
    event.timestamp = point.timestamp;
    event.value = point.is_int ? static_cast<double>(point.int_value) : point.value;
    
    return event;
}

// =============================================================================
// Utility Functions
// =============================================================================

bool isTracyFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    uint64_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    
    return magic == TRACY_MAGIC;
}

uint32_t getTracyFileVersion(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }
    
    uint64_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    
    if (magic != TRACY_MAGIC) {
        return 0;
    }
    
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    return version;
}

} // namespace tracy
} // namespace tracesmith
