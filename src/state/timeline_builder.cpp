#include "tracesmith/state/timeline_builder.hpp"
#include <algorithm>
#include <map>

namespace tracesmith {

void TimelineBuilder::addEvent(const TraceEvent& event) {
    events_.push_back(event);
}

void TimelineBuilder::addEvents(const std::vector<TraceEvent>& events) {
    events_.insert(events_.end(), events.begin(), events.end());
}

Timeline TimelineBuilder::build() {
    Timeline timeline;
    
    if (events_.empty()) {
        return timeline;
    }
    
    // Sort events by timestamp
    std::sort(events_.begin(), events_.end(), [](const TraceEvent& a, const TraceEvent& b) {
        return a.timestamp < b.timestamp;
    });
    
    // Convert events to spans
    std::map<uint64_t, TimelineSpan> active_spans;
    
    for (const auto& event : events_) {
        TimelineSpan span;
        span.correlation_id = event.correlation_id;
        span.device_id = event.device_id;
        span.stream_id = event.stream_id;
        span.type = event.type;
        span.name = event.name;
        span.start_time = event.timestamp;
        
        if (event.duration > 0) {
            // Event has duration - create complete span
            span.end_time = event.timestamp + event.duration;
            timeline.spans.push_back(span);
        } else {
            // Track as active span (will be completed by matching event)
            active_spans[event.correlation_id] = span;
        }
    }
    
    // Complete any remaining spans with estimated end times
    for (auto& [_, span] : active_spans) {
        // Use a default duration if not completed
        span.end_time = span.start_time + 1000; // 1 microsecond default
        timeline.spans.push_back(span);
    }
    
    // Calculate statistics
    calculateStatistics(timeline);
    
    return timeline;
}

void TimelineBuilder::clear() {
    events_.clear();
}

void TimelineBuilder::calculateStatistics(Timeline& timeline) {
    if (timeline.spans.empty()) {
        return;
    }
    
    // Find time range
    Timestamp min_time = timeline.spans[0].start_time;
    Timestamp max_time = timeline.spans[0].end_time;
    
    for (const auto& span : timeline.spans) {
        min_time = std::min(min_time, span.start_time);
        max_time = std::max(max_time, span.end_time);
    }
    
    timeline.total_duration = max_time - min_time;
    
    // Calculate per-stream statistics
    std::map<uint32_t, Timestamp> stream_active_time;
    
    for (const auto& span : timeline.spans) {
        Timestamp duration = span.end_time - span.start_time;
        stream_active_time[span.stream_id] += duration;
    }
    
    // Calculate utilization (total active time / (num_streams * total_duration))
    Timestamp total_active = 0;
    for (const auto& [_, active_time] : stream_active_time) {
        total_active += active_time;
    }
    
    if (timeline.total_duration > 0 && !stream_active_time.empty()) {
        timeline.gpu_utilization = static_cast<double>(total_active) / 
                                   (stream_active_time.size() * timeline.total_duration);
    }
    
    // Count concurrent operations
    timeline.max_concurrent_ops = calculateMaxConcurrentOps(timeline.spans);
}

size_t TimelineBuilder::calculateMaxConcurrentOps(const std::vector<TimelineSpan>& spans) {
    if (spans.empty()) {
        return 0;
    }
    
    // Create events for span start/end
    struct TimePoint {
        Timestamp time;
        bool is_start;
    };
    
    std::vector<TimePoint> points;
    for (const auto& span : spans) {
        points.push_back({span.start_time, true});
        points.push_back({span.end_time, false});
    }
    
    // Sort by time
    std::sort(points.begin(), points.end(), [](const TimePoint& a, const TimePoint& b) {
        if (a.time != b.time) {
            return a.time < b.time;
        }
        // End events before start events at same time
        return !a.is_start && b.is_start;
    });
    
    // Sweep through and find max
    size_t current = 0;
    size_t max_concurrent = 0;
    
    for (const auto& point : points) {
        if (point.is_start) {
            current++;
            max_concurrent = std::max(max_concurrent, current);
        } else {
            current--;
        }
    }
    
    return max_concurrent;
}

} // namespace tracesmith
