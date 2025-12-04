#pragma once

#include "tracesmith/common/types.hpp"
#include <map>
#include <vector>
#include <string>

namespace tracesmith {

/// GPU execution state
enum class GPUState {
    Idle,       // No operations pending or executing
    Queued,     // Operation submitted, waiting for execution
    Running,    // Actively executing on GPU
    Waiting,    // Blocked on synchronization or dependency
    Complete    // Execution finished
};

/// Convert GPUState to string
inline const char* gpuStateToString(GPUState state) {
    switch (state) {
        case GPUState::Idle:     return "Idle";
        case GPUState::Queued:   return "Queued";
        case GPUState::Running:  return "Running";
        case GPUState::Waiting:  return "Waiting";
        case GPUState::Complete: return "Complete";
        default:                 return "Unknown";
    }
}

/// State transition record
struct StateTransition {
    GPUState from;
    GPUState to;
    Timestamp when;
    uint64_t correlation_id;
    std::string reason;
    
    StateTransition() : from(GPUState::Idle), to(GPUState::Idle), when(0), correlation_id(0) {}
    
    StateTransition(GPUState f, GPUState t, Timestamp w, uint64_t corr = 0, const std::string& r = "")
        : from(f), to(t), when(w), correlation_id(corr), reason(r) {}
};

/**
 * GPU Stream State Tracker
 * 
 * Tracks the execution state of a single GPU stream over time.
 */
class GPUStreamState {
public:
    explicit GPUStreamState(uint32_t stream_id, uint32_t device_id = 0);
    
    /// Get current state
    GPUState currentState() const { return current_state_; }
    
    /// Transition to a new state
    void transitionTo(GPUState new_state, Timestamp when, 
                      uint64_t correlation_id = 0, const std::string& reason = "");
    
    /// Process an event and update state accordingly
    void processEvent(const TraceEvent& event);
    
    /// Get state at a specific time
    GPUState stateAt(Timestamp time) const;
    
    /// Get all state transitions
    const std::vector<StateTransition>& transitions() const { return transitions_; }
    
    /// Get time spent in each state
    std::map<GPUState, Timestamp> timeInStates() const;
    
    /// Get utilization percentage (time not idle)
    double utilization() const;
    
    /// Get stream and device IDs
    uint32_t streamId() const { return stream_id_; }
    uint32_t deviceId() const { return device_id_; }
    
    /// Clear all state history
    void reset();

private:
    uint32_t stream_id_;
    uint32_t device_id_;
    GPUState current_state_;
    Timestamp last_transition_time_;
    std::vector<StateTransition> transitions_;
};

/**
 * GPU State Machine
 * 
 * Manages state for all streams and devices, reconstructs execution
 * timeline from trace events.
 */
class GPUStateMachine {
public:
    GPUStateMachine() = default;
    
    /// Process a single event
    void processEvent(const TraceEvent& event);
    
    /// Process multiple events
    void processEvents(const std::vector<TraceEvent>& events);
    
    /// Get state for a specific stream
    GPUStreamState* getStreamState(uint32_t device_id, uint32_t stream_id);
    const GPUStreamState* getStreamState(uint32_t device_id, uint32_t stream_id) const;
    
    /// Get all streams
    std::vector<std::pair<uint32_t, uint32_t>> getAllStreams() const;
    
    /// Get statistics
    struct Statistics {
        size_t total_events = 0;
        size_t total_transitions = 0;
        std::map<uint32_t, size_t> transitions_per_stream;
        std::map<GPUState, Timestamp> total_time_per_state;
        double overall_utilization = 0.0;
    };
    
    Statistics getStatistics() const;
    
    /// Export state history
    struct StateHistory {
        uint32_t device_id;
        uint32_t stream_id;
        std::vector<StateTransition> transitions;
    };
    
    std::vector<StateHistory> exportHistory() const;
    
    /// Clear all state
    void reset();

private:
    // Map of (device_id, stream_id) -> GPUStreamState
    std::map<std::pair<uint32_t, uint32_t>, GPUStreamState> stream_states_;
    
    size_t event_count_ = 0;
    
    // Helper to get or create stream state
    GPUStreamState& getOrCreateStreamState(uint32_t device_id, uint32_t stream_id);
};

} // namespace tracesmith
