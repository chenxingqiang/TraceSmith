#include "tracesmith/common/types.hpp"
#include <algorithm>

namespace tracesmith {

void TraceRecord::sortByTimestamp() {
    std::sort(events_.begin(), events_.end(), 
        [](const TraceEvent& a, const TraceEvent& b) {
            return a.timestamp < b.timestamp;
        });
}

std::vector<TraceEvent> TraceRecord::filterByType(EventType type) const {
    std::vector<TraceEvent> result;
    result.reserve(events_.size() / 4);  // Estimate
    
    for (const auto& event : events_) {
        if (event.type == type) {
            result.push_back(event);
        }
    }
    
    return result;
}

std::vector<TraceEvent> TraceRecord::filterByStream(uint32_t stream_id) const {
    std::vector<TraceEvent> result;
    result.reserve(events_.size() / 4);
    
    for (const auto& event : events_) {
        if (event.stream_id == stream_id) {
            result.push_back(event);
        }
    }
    
    return result;
}

std::vector<TraceEvent> TraceRecord::filterByDevice(uint32_t device_id) const {
    std::vector<TraceEvent> result;
    result.reserve(events_.size() / 4);
    
    for (const auto& event : events_) {
        if (event.device_id == device_id) {
            result.push_back(event);
        }
    }
    
    return result;
}

} // namespace tracesmith
