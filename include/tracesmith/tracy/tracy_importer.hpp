#pragma once

/**
 * Tracy Importer for TraceSmith
 * 
 * Imports Tracy profiling data and converts it to TraceSmith events.
 * This enables analysis of Tracy captures using TraceSmith's tools.
 * 
 * Features:
 * - Import Tracy file format (.tracy)
 * - Convert Tracy zones to TraceSmith events
 * - Import GPU zones as kernel events
 * - Convert memory allocations to MemoryEvents
 * - Import plot data as CounterEvents
 * 
 * Usage:
 *   #include <tracesmith/tracy/tracy_importer.hpp>
 *   
 *   tracesmith::tracy::TracyImporter importer;
 *   auto record = importer.importFile("profile.tracy");
 */

#include "tracesmith/common/types.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

namespace tracesmith {
namespace tracy {

/**
 * Tracy zone data structure
 * Represents a parsed Tracy zone for conversion
 */
struct TracyZone {
    std::string name;
    std::string source_file;
    std::string function;
    uint32_t source_line = 0;
    uint64_t start_time = 0;
    uint64_t end_time = 0;
    uint32_t thread_id = 0;
    uint32_t color = 0;
    int32_t depth = 0;
    bool is_gpu = false;
    uint8_t gpu_context = 0;
    
    // Child zones (for hierarchical representation)
    std::vector<TracyZone> children;
    
    uint64_t duration() const { return end_time - start_time; }
};

/**
 * Tracy GPU zone data structure
 */
struct TracyGpuZone {
    std::string name;
    uint64_t cpu_start = 0;
    uint64_t cpu_end = 0;
    uint64_t gpu_start = 0;
    uint64_t gpu_end = 0;
    uint8_t context_id = 0;
    uint32_t thread_id = 0;
    uint32_t color = 0;
    
    uint64_t gpu_duration() const { return gpu_end - gpu_start; }
    uint64_t cpu_duration() const { return cpu_end - cpu_start; }
};

/**
 * Tracy memory allocation data
 */
struct TracyMemoryAlloc {
    uint64_t ptr = 0;
    uint64_t size = 0;
    uint64_t alloc_time = 0;
    uint64_t free_time = 0;  // 0 if not freed
    uint32_t thread_id = 0;
    std::string pool_name;
    bool is_freed = false;
};

/**
 * Tracy plot point data
 */
struct TracyPlotPoint {
    std::string name;
    uint64_t timestamp = 0;
    double value = 0.0;
    bool is_int = false;
    int64_t int_value = 0;
};

/**
 * Tracy frame data
 */
struct TracyFrame {
    std::string name;
    uint64_t start_time = 0;
    uint64_t end_time = 0;
    uint32_t frame_number = 0;
    
    uint64_t duration() const { return end_time - start_time; }
};

/**
 * Tracy import result
 */
struct TracyImportResult {
    TraceRecord record;
    
    // Statistics
    uint64_t zones_imported = 0;
    uint64_t gpu_zones_imported = 0;
    uint64_t memory_events_imported = 0;
    uint64_t plot_points_imported = 0;
    uint64_t frames_imported = 0;
    
    // Errors encountered
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    bool success() const { return errors.empty(); }
};

/**
 * Configuration for Tracy import
 */
struct TracyImporterConfig {
    // Import settings
    bool import_zones = true;
    bool import_gpu_zones = true;
    bool import_memory = true;
    bool import_plots = true;
    bool import_frames = true;
    
    // Filtering
    uint64_t min_zone_duration_ns = 0;  // Skip zones shorter than this
    uint64_t max_zone_depth = 100;      // Max nesting depth
    
    // Conversion settings
    bool convert_gpu_zones_to_kernels = true;  // Convert GPU zones to KernelLaunch events
    bool flatten_zone_hierarchy = false;        // Flatten nested zones
    
    // Time adjustment
    int64_t time_offset_ns = 0;  // Add to all timestamps
    
    TracyImporterConfig() = default;
};

/**
 * Progress callback for import operations
 */
using TracyImportProgressCallback = std::function<void(float progress, const std::string& status)>;

/**
 * TracyImporter - Import Tracy profiling data to TraceSmith format
 * 
 * This class reads Tracy file format (.tracy) and converts the data
 * to TraceSmith's event representation for further analysis.
 */
class TracyImporter {
public:
    TracyImporter();
    explicit TracyImporter(const TracyImporterConfig& config);
    ~TracyImporter();
    
    // Disable copy
    TracyImporter(const TracyImporter&) = delete;
    TracyImporter& operator=(const TracyImporter&) = delete;
    
    /**
     * Import from Tracy file
     * @param filepath Path to .tracy file
     * @return Import result with TraceRecord and statistics
     */
    TracyImportResult importFile(const std::string& filepath);
    
    /**
     * Import from memory buffer
     * @param data Pointer to Tracy file data
     * @param size Size of data in bytes
     * @return Import result with TraceRecord and statistics
     */
    TracyImportResult importFromMemory(const uint8_t* data, size_t size);
    
    /**
     * Set progress callback for long imports
     */
    void setProgressCallback(TracyImportProgressCallback callback);
    
    /**
     * Get current configuration
     */
    const TracyImporterConfig& config() const { return config_; }
    
    /**
     * Update configuration
     */
    void setConfig(const TracyImporterConfig& config) { config_ = config; }
    
    // =========================================================================
    // Low-level conversion functions (for custom import pipelines)
    // =========================================================================
    
    /**
     * Convert a Tracy zone to TraceSmith event
     */
    static TraceEvent convertZone(const TracyZone& zone);
    
    /**
     * Convert a Tracy GPU zone to TraceSmith event
     */
    static TraceEvent convertGpuZone(const TracyGpuZone& zone);
    
    /**
     * Convert Tracy memory allocation to TraceSmith MemoryEvent
     */
    static MemoryEvent convertMemoryAlloc(const TracyMemoryAlloc& alloc, bool is_free);
    
    /**
     * Convert Tracy plot point to TraceSmith CounterEvent
     */
    static CounterEvent convertPlotPoint(const TracyPlotPoint& point);
    
private:
    // Implementation
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    TracyImporterConfig config_;
    TracyImportProgressCallback progress_callback_;
};

/**
 * Utility function to check if a file is a valid Tracy file
 */
bool isTracyFile(const std::string& filepath);

/**
 * Get Tracy file version
 * Returns 0 if not a valid Tracy file
 */
uint32_t getTracyFileVersion(const std::string& filepath);

} // namespace tracy
} // namespace tracesmith
