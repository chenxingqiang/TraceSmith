#include "tracesmith/state/gpu_state_machine.hpp"
#include <algorithm>

namespace tracesmith {

// ============================================================================
// GPUStreamState Implementation
// ============================================================================

GPUStreamState::GPUStreamState(uint32_t stream_id, uint32_t device_id)
    : stream_id_(stream_id)
    , device_id_(device_id)
    , current_state_(GPUState::Idle)
    , last_transition_time_(0) {
}

void GPUStreamState::transitionTo(GPUState new_state, Timestamp when,
                                    uint64_t correlation_id, const std::string& reason) {
    if (new_state == current_state_) {
        return;  // No change
    }
    
    StateTransition transition(current_state_, new_state, when, correlation_id, reason);
    transitions_.push_back(transition);
    
    current_state_ = new_state;
    last_transition_time_ = when;
}

void GPUStreamState::processEvent(const TraceEvent& event) {
    switch (event.type) {
        case EventType::KernelLaunch:
            // Kernel submitted: Idle -> Queued -> Running
            if (current_state_ == GPUState::Idle) {
                transitionTo(GPUState::Queued, event.timestamp, event.correlation_id, "Kernel submitted");
            }
            transitionTo(GPUState::Running, event.timestamp, event.correlation_id, "Kernel executing");
            break;
            
        case EventType::KernelComplete:
            // Kernel finished: Running -> Complete -> Idle
            transitionTo(GPUState::Complete, event.timestamp, event.correlation_id, "Kernel complete");
            transitionTo(GPUState::Idle, event.timestamp + event.duration, event.correlation_id, "");
            break;
            
        case EventType::MemcpyH2D:
        case EventType::MemcpyD2H:
        case EventType::MemcpyD2D:
            // Memory operation: Idle -> Running -> Complete
            if (current_state_ == GPUState::Idle) {
                transitionTo(GPUState::Running, event.timestamp, event.correlation_id, "Memory op");
            }
            if (event.duration > 0) {
                transitionTo(GPUState::Complete, event.timestamp + event.duration, event.correlation_id, "");
                transitionTo(GPUState::Idle, event.timestamp + event.duration, event.correlation_id, "");
            }
            break;
            
        case EventType::StreamSync:
        case EventType::DeviceSync:
            // Synchronization: -> Waiting -> Idle
            transitionTo(GPUState::Waiting, event.timestamp, event.correlation_id, "Sync wait");
            transitionTo(GPUState::Idle, event.timestamp + event.duration, event.correlation_id, "Sync complete");
            break;
            
        default:
            break;
    }
}

GPUState GPUStreamState::stateAt(Timestamp time) const {
    if (transitions_.empty()) {
        return GPUState::Idle;
    }
    
    // Find the last transition before or at the given time
    GPUState state = GPUState::Idle;
    for (const auto& transition : transitions_) {
        if (transition.when <= time) {
            state = transition.to;
        } else {
            break;
        }
    }
    
    return state;
}

std::map<GPUState, Timestamp> GPUStreamState::timeInStates() const {
    std::map<GPUState, Timestamp> times;
    
    if (transitions_.empty()) {
        return times;
    }
    
    for (size_t i = 0; i < transitions_.size(); ++i) {
        const auto& transition = transitions_[i];
        
        Timestamp duration;
        if (i + 1 < transitions_.size()) {
            duration = transitions_[i + 1].when - transition.when;
        } else {
            duration = last_transition_time_ - transition.when;
        }
        
        times[transition.to] += duration;
    }
    
    return times;
}

double GPUStreamState::utilization() const {
    auto times = timeInStates();
    
    Timestamp total_time = 0;
    Timestamp non_idle_time = 0;
    
    for (const auto& [state, time] : times) {
        total_time += time;
        if (state != GPUState::Idle) {
            non_idle_time += time;
        }
    }
    
    return total_time > 0 ? (static_cast<double>(non_idle_time) / total_time) * 100.0 : 0.0;
}

void GPUStreamState::reset() {
    current_state_ = GPUState::Idle;
    last_transition_time_ = 0;
    transitions_.clear();
}

// ============================================================================
// GPUStateMachine Implementation
// ============================================================================

void GPUStateMachine::processEvent(const TraceEvent& event) {
    auto& stream_state = getOrCreateStreamState(event.device_id, event.stream_id);
    stream_state.processEvent(event);
    event_count_++;
}

void GPUStateMachine::processEvents(const std::vector<TraceEvent>& events) {
    for (const auto& event : events) {
        processEvent(event);
    }
}

GPUStreamState* GPUStateMachine::getStreamState(uint32_t device_id, uint32_t stream_id) {
    auto key = std::make_pair(device_id, stream_id);
    auto it = stream_states_.find(key);
    return it != stream_states_.end() ? &it->second : nullptr;
}

const GPUStreamState* GPUStateMachine::getStreamState(uint32_t device_id, uint32_t stream_id) const {
    auto key = std::make_pair(device_id, stream_id);
    auto it = stream_states_.find(key);
    return it != stream_states_.end() ? &it->second : nullptr;
}

std::vector<std::pair<uint32_t, uint32_t>> GPUStateMachine::getAllStreams() const {
    std::vector<std::pair<uint32_t, uint32_t>> streams;
    streams.reserve(stream_states_.size());
    
    for (const auto& [key, _] : stream_states_) {
        streams.push_back(key);
    }
    
    return streams;
}

GPUStateMachine::Statistics GPUStateMachine::getStatistics() const {
    Statistics stats;
    stats.total_events = event_count_;
    
    for (const auto& [key, stream_state] : stream_states_) {
        size_t transition_count = stream_state.transitions().size();
        stats.total_transitions += transition_count;
        stats.transitions_per_stream[key.second] = transition_count;
        
        // Aggregate time per state
        auto times = stream_state.timeInStates();
        for (const auto& [state, time] : times) {
            stats.total_time_per_state[state] += time;
        }
    }
    
    // Calculate overall utilization
    Timestamp total_time = 0;
    Timestamp non_idle_time = 0;
    
    for (const auto& [state, time] : stats.total_time_per_state) {
        total_time += time;
        if (state != GPUState::Idle) {
            non_idle_time += time;
        }
    }
    
    stats.overall_utilization = total_time > 0 ? 
        (static_cast<double>(non_idle_time) / total_time) * 100.0 : 0.0;
    
    return stats;
}

std::vector<GPUStateMachine::StateHistory> GPUStateMachine::exportHistory() const {
    std::vector<StateHistory> history;
    history.reserve(stream_states_.size());
    
    for (const auto& [key, stream_state] : stream_states_) {
        StateHistory h;
        h.device_id = key.first;
        h.stream_id = key.second;
        h.transitions = stream_state.transitions();
        history.push_back(h);
    }
    
    return history;
}

void GPUStateMachine::reset() {
    stream_states_.clear();
    event_count_ = 0;
}

GPUStreamState& GPUStateMachine::getOrCreateStreamState(uint32_t device_id, uint32_t stream_id) {
    auto key = std::make_pair(device_id, stream_id);
    auto it = stream_states_.find(key);
    
    if (it == stream_states_.end()) {
        it = stream_states_.emplace(key, GPUStreamState(stream_id, device_id)).first;
    }
    
    return it->second;
}

} // namespace tracesmith
