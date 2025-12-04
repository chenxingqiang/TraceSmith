#pragma once

#include "tracesmith/common/types.hpp"
#include "tracesmith/common/ring_buffer.hpp"
#include <vector>
#include <string>
#include <memory>

#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
#include "perfetto.h"
#endif

// Forward declaration
namespace tracesmith {
class PerfettoExporter;
}

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

/// Real-time tracing session (v0.3.0 full implementation)
/// 
/// Thread-safe trace collection using lock-free ring buffer.
/// Supports both in-process collection and file export.
/// 
/// Features:
/// - Thread-safe event emission via RingBuffer
/// - Counter track support
/// - Automatic flush on stop
/// - Statistics tracking
class TracingSession {
public:
    /// Session state
    enum class State {
        Stopped,
        Starting,
        Running,
        Stopping
    };
    
    /// Tracing mode
    enum class Mode {
        InProcess,      // In-process circular buffer
        File            // Direct file output
    };
    
    /// Session statistics
    struct Statistics {
        uint64_t events_emitted = 0;
        uint64_t events_dropped = 0;
        uint64_t counters_emitted = 0;
        Timestamp start_time = 0;
        Timestamp stop_time = 0;
        
        double duration_ms() const {
            return (stop_time - start_time) / 1000000.0;
        }
    };
    
    /// Default constructor with 64K event buffer
    TracingSession() 
        : state_(State::Stopped)
        , mode_(Mode::InProcess)
        , event_buffer_(65536)
        , counter_buffer_(4096) {}
    
    /// Constructor with custom buffer size
    explicit TracingSession(size_t event_buffer_size, size_t counter_buffer_size = 4096)
        : state_(State::Stopped)
        , mode_(Mode::InProcess)
        , event_buffer_(event_buffer_size)
        , counter_buffer_(counter_buffer_size) {}
    
    ~TracingSession() { stop(); }
    
    /// Start tracing session
    /// @param config Configuration for the session
    /// @return true if session started successfully
    bool start(const TracingConfig& config) {
        if (state_ != State::Stopped) return false;
        
        config_ = config;
        state_ = State::Starting;
        
        // Reset buffers and stats
        event_buffer_.reset();
        counter_buffer_.reset();
        stats_ = Statistics{};
        stats_.start_time = getCurrentTimestamp();
        
        state_ = State::Running;
        return true;
    }
    
    /// Stop tracing session and flush data
    void stop() {
        if (state_ != State::Running) return;
        
        state_ = State::Stopping;
        stats_.stop_time = getCurrentTimestamp();
        
        // Flush remaining events from buffer
        flushEvents();
        flushCounters();
        
        stats_.events_dropped = event_buffer_.droppedCount();
        
        state_ = State::Stopped;
    }
    
    /// Check if session is active
    bool isActive() const { return state_ == State::Running; }
    
    /// Get current state
    State getState() const { return state_; }
    
    /// Get tracing mode
    Mode getMode() const { return mode_; }
    
    /// Get session statistics
    const Statistics& getStatistics() const { return stats_; }
    
    /// Emit a trace event (thread-safe, lock-free)
    /// @param event Event to emit
    /// @return true if event was queued, false if dropped
    bool emit(const TraceEvent& event) {
        if (state_ != State::Running) return false;
        
        bool success = event_buffer_.push(event);
        if (success) {
            stats_.events_emitted++;
        }
        return success;
    }
    
    /// Emit a trace event with move semantics
    bool emit(TraceEvent&& event) {
        if (state_ != State::Running) return false;
        
        bool success = event_buffer_.push(std::move(event));
        if (success) {
            stats_.events_emitted++;
        }
        return success;
    }
    
    /// Emit a counter value (thread-safe)
    /// @param name Counter name
    /// @param value Counter value
    /// @param timestamp Optional timestamp (0 = auto)
    bool emitCounter(const std::string& name, double value, Timestamp timestamp = 0) {
        if (state_ != State::Running) return false;
        
        CounterEvent counter(name, value, timestamp);
        bool success = counter_buffer_.push(std::move(counter));
        if (success) {
            stats_.counters_emitted++;
        }
        return success;
    }
    
    /// Get all captured events (call after stop)
    const std::vector<TraceEvent>& getEvents() const { return flushed_events_; }
    
    /// Get all captured counters (call after stop)
    const std::vector<CounterEvent>& getCounters() const { return flushed_counters_; }
    
    /// Export session to Perfetto file (defined in cpp to avoid circular includes)
    /// @param filename Output file path
    /// @param use_protobuf Use protobuf format if SDK available
    /// @return true if export successful
    bool exportToFile(const std::string& filename, bool use_protobuf = true);
    
    /// Clear all captured data
    void clear() {
        event_buffer_.reset();
        counter_buffer_.reset();
        flushed_events_.clear();
        flushed_counters_.clear();
        stats_ = Statistics{};
    }
    
    /// Get buffer statistics
    size_t eventBufferSize() const { return event_buffer_.size(); }
    size_t eventBufferCapacity() const { return event_buffer_.capacity(); }
    uint64_t eventsDropped() const { return event_buffer_.droppedCount(); }

private:
    void flushEvents() {
        event_buffer_.popBatch(flushed_events_, event_buffer_.capacity());
    }
    
    void flushCounters() {
        counter_buffer_.popBatch(flushed_counters_, counter_buffer_.capacity());
    }
    
    State state_;
    Mode mode_;
    TracingConfig config_;
    Statistics stats_;
    
    // Lock-free ring buffers for thread-safe emission
    RingBuffer<TraceEvent> event_buffer_;
    RingBuffer<CounterEvent> counter_buffer_;
    
    // Flushed data storage
    std::vector<TraceEvent> flushed_events_;
    std::vector<CounterEvent> flushed_counters_;
};

} // namespace tracesmith
