#pragma once

/**
 * Tracy Exporter for TraceSmith
 * 
 * Exports TraceSmith trace events to Tracy profiler for real-time visualization.
 * Supports both live streaming and batch export of captured events.
 * 
 * Features:
 * - Stream TraceSmith events to Tracy in real-time
 * - Export captured traces to Tracy format
 * - GPU context management for CUDA/Metal/ROCm
 * - Memory event tracking
 * - Counter/metric visualization
 * 
 * Usage:
 *   #include <tracesmith/tracy/tracy_exporter.hpp>
 *   
 *   // Create exporter
 *   tracesmith::tracy::TracyExporter exporter;
 *   exporter.initialize();
 *   
 *   // Stream events live
 *   exporter.emitEvent(event);
 *   
 *   // Or batch export
 *   exporter.exportEvents(events);
 */

#include "tracesmith/common/types.hpp"
#include "tracesmith/tracy/tracy_client.hpp"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace tracesmith {
namespace tracy {

// Forward declarations
class TracyGpuContext;

/**
 * Configuration for Tracy export
 */
struct TracyExporterConfig {
    // GPU context settings
    std::string gpu_context_name = "TraceSmith GPU";
    bool enable_gpu_zones = true;
    bool enable_memory_tracking = true;
    bool enable_counters = true;
    
    // Event filtering
    bool emit_kernel_events = true;
    bool emit_memcpy_events = true;
    bool emit_sync_events = true;
    bool emit_alloc_events = true;
    
    // Timing settings
    bool use_event_timestamps = true;  // Use original timestamps vs current time
    
    // Plot configuration
    bool auto_configure_plots = true;
    
    TracyExporterConfig() = default;
};

/**
 * TracyExporter - Export TraceSmith events to Tracy profiler
 * 
 * This class bridges TraceSmith's event capture system with Tracy's
 * real-time profiling visualization. Events can be streamed live or
 * exported in batches.
 */
class TracyExporter {
public:
    TracyExporter();
    explicit TracyExporter(const TracyExporterConfig& config);
    ~TracyExporter();
    
    // Disable copy
    TracyExporter(const TracyExporter&) = delete;
    TracyExporter& operator=(const TracyExporter&) = delete;
    
    // Allow move
    TracyExporter(TracyExporter&&) noexcept;
    TracyExporter& operator=(TracyExporter&&) noexcept;
    
    /**
     * Initialize the exporter
     * Must be called before emitting events
     */
    bool initialize();
    
    /**
     * Shutdown the exporter
     */
    void shutdown();
    
    /**
     * Check if exporter is initialized
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * Check if Tracy is connected and receiving data
     */
    bool isConnected() const;
    
    // =========================================================================
    // Event Emission
    // =========================================================================
    
    /**
     * Emit a single TraceSmith event to Tracy
     */
    void emitEvent(const TraceEvent& event);
    
    /**
     * Emit a memory event to Tracy
     */
    void emitMemoryEvent(const MemoryEvent& event);
    
    /**
     * Emit a counter event to Tracy
     */
    void emitCounterEvent(const CounterEvent& event);
    
    /**
     * Batch export multiple events
     */
    void exportEvents(const std::vector<TraceEvent>& events);
    
    /**
     * Export a complete trace record
     */
    void exportTraceRecord(const TraceRecord& record);
    
    // =========================================================================
    // GPU Context Management
    // =========================================================================
    
    /**
     * Create a GPU context for Tracy visualization
     * Returns the context ID
     */
    uint8_t createGpuContext(uint32_t device_id, const std::string& name = "");
    
    /**
     * Destroy a GPU context
     */
    void destroyGpuContext(uint8_t context_id);
    
    /**
     * Emit a GPU zone (kernel execution, memcpy, etc.)
     */
    void emitGpuZone(uint8_t context_id,
                     const std::string& name,
                     Timestamp cpu_start,
                     Timestamp cpu_end,
                     Timestamp gpu_start,
                     Timestamp gpu_end,
                     uint32_t color = 0);
    
    // =========================================================================
    // Frame Marking
    // =========================================================================
    
    /**
     * Mark a frame boundary
     */
    void markFrame(const char* name = nullptr);
    
    /**
     * Mark frame start for discontinuous frames
     */
    void markFrameStart(const char* name);
    
    /**
     * Mark frame end for discontinuous frames
     */
    void markFrameEnd(const char* name);
    
    // =========================================================================
    // Plot Management
    // =========================================================================
    
    /**
     * Configure a plot for counter visualization
     */
    void configurePlot(const std::string& name,
                       PlotType type = PlotType::Number,
                       bool step = false,
                       bool fill = true,
                       uint32_t color = 0);
    
    /**
     * Emit a plot value
     */
    void emitPlotValue(const std::string& name, double value);
    void emitPlotValue(const std::string& name, int64_t value);
    
    // =========================================================================
    // Statistics
    // =========================================================================
    
    /**
     * Get number of events emitted
     */
    uint64_t eventsEmitted() const { return events_emitted_.load(); }
    
    /**
     * Get number of GPU zones emitted
     */
    uint64_t gpuZonesEmitted() const { return gpu_zones_emitted_.load(); }
    
    /**
     * Reset statistics
     */
    void resetStats();
    
private:
    // Implementation helpers
    void emitTraceEventInternal(const TraceEvent& event);
    void setupDefaultPlots();
    uint32_t allocateQueryId();
    
    // Configuration
    TracyExporterConfig config_;
    
    // State
    bool initialized_ = false;
    std::atomic<uint64_t> events_emitted_{0};
    std::atomic<uint64_t> gpu_zones_emitted_{0};
    std::atomic<uint32_t> query_id_counter_{0};
    
    // GPU contexts (device_id -> context_id)
    std::unordered_map<uint32_t, uint8_t> gpu_contexts_;
    std::mutex gpu_context_mutex_;
    
    // Configured plots
    std::unordered_map<std::string, bool> configured_plots_;
    std::mutex plot_mutex_;
};

/**
 * GPU Zone RAII wrapper for Tracy
 * 
 * Automatically emits GPU zone begin/end to Tracy
 * Named TracyGpuZoneScope to avoid conflict with TracyGpuZone struct in tracy_importer.hpp
 */
class TracyGpuZoneScope {
public:
    TracyGpuZoneScope(TracyExporter& exporter,
                      uint8_t context_id,
                      const std::string& name,
                      uint32_t color = 0);
    ~TracyGpuZoneScope();
    
    // Disable copy
    TracyGpuZoneScope(const TracyGpuZoneScope&) = delete;
    TracyGpuZoneScope& operator=(const TracyGpuZoneScope&) = delete;
    
    // Set GPU timestamps (for async operations)
    void setGpuTimestamps(Timestamp gpu_start, Timestamp gpu_end);
    
private:
    TracyExporter& exporter_;
    uint8_t context_id_;
    std::string name_;
    uint32_t color_;
    Timestamp cpu_start_;
    Timestamp gpu_start_ = 0;
    Timestamp gpu_end_ = 0;
    bool gpu_timestamps_set_ = false;
};

// =============================================================================
// Global Tracy Exporter Instance
// =============================================================================

/**
 * Get the global Tracy exporter instance
 * Creates one if it doesn't exist
 */
TracyExporter& getGlobalTracyExporter();

/**
 * Set the global Tracy exporter configuration
 * Must be called before first use of getGlobalTracyExporter()
 */
void setGlobalTracyExporterConfig(const TracyExporterConfig& config);

} // namespace tracy
} // namespace tracesmith
