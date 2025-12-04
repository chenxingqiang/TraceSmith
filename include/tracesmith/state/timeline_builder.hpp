#pragma once

#include "tracesmith/common/types.hpp"
#include <vector>
#include <cstddef>

namespace tracesmith {

/**
 * Timeline Span
 * 
 * Represents a time interval for a GPU operation
 */
struct TimelineSpan {
    uint64_t correlation_id;
    uint32_t device_id;
    uint32_t stream_id;
    EventType type;
    std::string name;
    Timestamp start_time;
    Timestamp end_time;
};

/**
 * Timeline
 * 
 * Collection of timeline spans with statistics
 */
struct Timeline {
    std::vector<TimelineSpan> spans;
    Timestamp total_duration = 0;
    double gpu_utilization = 0.0;
    size_t max_concurrent_ops = 0;
};

/**
 * Timeline Builder
 * 
 * Converts trace events into timeline spans for visualization
 */
class TimelineBuilder {
public:
    TimelineBuilder() = default;
    
    /**
     * Add a single event
     */
    void addEvent(const TraceEvent& event);
    
    /**
     * Add multiple events
     */
    void addEvents(const std::vector<TraceEvent>& events);
    
    /**
     * Build timeline from accumulated events
     * 
     * @return Timeline with spans and statistics
     */
    Timeline build();
    
    /**
     * Clear all events
     */
    void clear();

private:
    std::vector<TraceEvent> events_;
    
    void calculateStatistics(Timeline& timeline);
    size_t calculateMaxConcurrentOps(const std::vector<TimelineSpan>& spans);
};

} // namespace tracesmith
