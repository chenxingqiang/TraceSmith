#pragma once

#include "tracesmith/types.hpp"
#include <vector>
#include <string>
#include <memory>

#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
#include "perfetto.h"
#endif

namespace tracesmith {

/// Tracing configuration for Perfetto SDK
struct TracingConfig {
    // Buffer size in KB
    uint32_t buffer_size_kb = 4096;
    
    // Duration for in-process tracing (0 = no limit)
    uint32_t duration_ms = 0;
    
    // Whether to write directly to file vs in-memory buffer
    bool write_to_file = false;
    std::string output_file;
    
    // Enable specific track types
    bool enable_gpu_tracks = true;
    bool enable_counter_tracks = true;
    bool enable_flow_events = true;
    
    TracingConfig() = default;
};

/// Perfetto protobuf exporter with fallback to JSON
class PerfettoProtoExporter {
public:
    /// Output format selection
    enum class Format {
        JSON,       // Fallback to JSON if SDK not available
        PROTOBUF    // Native Perfetto protobuf (requires SDK)
    };
    
    /// Constructor with format selection
    /// @param format Output format (PROTOBUF requires TRACESMITH_PERFETTO_SDK_ENABLED)
    explicit PerfettoProtoExporter(Format format = Format::PROTOBUF);
    
    /// Destructor
    ~PerfettoProtoExporter();
    
    /// Export events to file (auto-detects format from extension)
    /// @param events Vector of trace events to export
    /// @param output_file Path to output file (.json or .perfetto-trace)
    /// @return true if export succeeded
    bool exportToFile(const std::vector<TraceEvent>& events, 
                     const std::string& output_file);
    
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    /// Export to protobuf buffer (SDK only)
    /// @param events Vector of trace events to export
    /// @return Protobuf binary data
    std::vector<uint8_t> exportToProto(const std::vector<TraceEvent>& events);
    
    /// Initialize real-time tracing session (SDK only)
    /// @param config Tracing configuration
    /// @return true if initialization succeeded
    bool initializeTracingSession(const TracingConfig& config);
    
    /// Stop the active tracing session and flush data
    void stopTracingSession();
    
    /// Emit a single event to active tracing session
    /// @param event Event to emit
    void emitEvent(const TraceEvent& event);
    
    /// Add GPU-specific track
    /// @param track_name Name of the GPU track
    /// @param device_id GPU device ID
    void addGPUTrack(const std::string& track_name, uint32_t device_id);
    
    /// Add counter track for metrics
    /// @param counter_name Name of the counter (e.g., "Memory Bandwidth")
    /// @param track_id Unique track ID
    void addCounterTrack(const std::string& counter_name, uint32_t track_id);
    
    /// Emit a counter value
    /// @param track_id Counter track ID
    /// @param value Counter value
    /// @param timestamp Timestamp (0 = use current time)
    void emitCounter(uint32_t track_id, int64_t value, Timestamp timestamp = 0);
#endif
    
    /// Get selected format
    Format getFormat() const { return format_; }
    
    /// Check if SDK is available
    static bool isSDKAvailable() {
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
        return true;
#else
        return false;
#endif
    }
    
private:
    Format format_;
    
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    // Perfetto SDK implementation details
    class PerfettoImpl;
    std::unique_ptr<PerfettoImpl> impl_;
    
    // Track management
    struct GPUTrack {
        std::string name;
        uint32_t device_id;
        uint64_t uuid;  // Unique identifier for track
    };
    
    struct CounterTrack {
        std::string name;
        uint32_t track_id;
        uint64_t uuid;  // Unique identifier for track
    };
    
    std::vector<GPUTrack> gpu_tracks_;
    std::vector<CounterTrack> counter_tracks_;
    
    // Event conversion helpers
    std::string getEventCategory(EventType type);
    
    // Track event type conversion
    enum class PerfettoEventType {
        SliceBegin,
        SliceEnd,
        Instant,
        Counter
    };
    
    PerfettoEventType mapEventTypeToPerfetto(EventType type);
#endif
    
    // JSON fallback
    bool exportToJSON(const std::vector<TraceEvent>& events, 
                     const std::string& output_file);
};

} // namespace tracesmith
