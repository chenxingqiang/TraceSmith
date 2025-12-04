#include "tracesmith/state/timeline_viewer.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>

namespace tracesmith {

TimelineViewer::TimelineViewer(const ViewConfig& config) : config_(config) {}

std::string TimelineViewer::render(const Timeline& timeline) {
    std::ostringstream ss;
    
    if (timeline.spans.empty()) {
        ss << "Empty timeline\n";
        return ss.str();
    }
    
    // Group spans by stream
    auto rows = groupSpansByStream(timeline);
    
    // Get time range
    Timestamp start_time = timeline.spans[0].start_time;
    Timestamp end_time = timeline.spans[0].end_time;
    for (const auto& span : timeline.spans) {
        start_time = std::min(start_time, span.start_time);
        end_time = std::max(end_time, span.end_time);
    }
    
    // Render header
    ss << "Timeline View\n";
    ss << "=============\n";
    if (config_.show_timestamps) {
        ss << "Start: " << formatTimestamp(start_time) << "\n";
        ss << "End:   " << formatTimestamp(end_time) << "\n";
        ss << "Duration: " << formatDuration(end_time - start_time) << "\n\n";
    }
    
    // Render each stream
    size_t rows_rendered = 0;
    for (const auto& row : rows) {
        if (rows_rendered >= config_.max_rows) {
            ss << "... (" << (rows.size() - rows_rendered) << " more streams)\n";
            break;
        }
        
        ss << "Stream " << row.stream_id << ": ";
        ss << renderRow(row, start_time, end_time) << "\n";
        rows_rendered++;
    }
    
    return ss.str();
}

std::string TimelineViewer::renderStream(const Timeline& timeline, uint32_t stream_id) {
    std::ostringstream ss;
    
    // Filter spans for this stream
    std::vector<const TimelineSpan*> spans;
    for (const auto& span : timeline.spans) {
        if (span.stream_id == stream_id) {
            spans.push_back(&span);
        }
    }
    
    if (spans.empty()) {
        ss << "No events in stream " << stream_id << "\n";
        return ss.str();
    }
    
    // Get time range
    Timestamp start_time = spans[0]->start_time;
    Timestamp end_time = spans[0]->end_time;
    for (const auto* span : spans) {
        start_time = std::min(start_time, span->start_time);
        end_time = std::max(end_time, span->end_time);
    }
    
    ss << "Stream " << stream_id << " Timeline\n";
    ss << "========================\n";
    if (config_.show_timestamps) {
        ss << "Start: " << formatTimestamp(start_time) << "\n";
        ss << "End:   " << formatTimestamp(end_time) << "\n\n";
    }
    
    TimelineRow row{stream_id, spans};
    ss << renderRow(row, start_time, end_time) << "\n";
    
    // Show event details
    ss << "\nEvents:\n";
    for (const auto* span : spans) {
        ss << "  " << span->name;
        if (config_.show_duration) {
            ss << " [" << formatDuration(span->end_time - span->start_time) << "]";
        }
        ss << "\n";
    }
    
    return ss.str();
}

std::string TimelineViewer::renderStats(const Timeline& timeline) {
    std::ostringstream ss;
    
    ss << "Timeline Statistics\n";
    ss << "===================\n";
    ss << "Total Events: " << timeline.spans.size() << "\n";
    
    // Count by type
    std::map<EventType, size_t> type_counts;
    for (const auto& span : timeline.spans) {
        type_counts[span.type]++;
    }
    
    ss << "\nEvent Types:\n";
    for (const auto& [type, count] : type_counts) {
        ss << "  " << static_cast<int>(type) << ": " << count << "\n";
    }
    
    // Stream statistics
    std::map<uint32_t, size_t> stream_counts;
    for (const auto& span : timeline.spans) {
        stream_counts[span.stream_id]++;
    }
    
    ss << "\nStream Activity:\n";
    for (const auto& [stream_id, count] : stream_counts) {
        ss << "  Stream " << stream_id << ": " << count << " events\n";
    }
    
    return ss.str();
}

std::vector<TimelineViewer::TimelineRow> TimelineViewer::groupSpansByStream(const Timeline& timeline) {
    std::map<uint32_t, TimelineRow> stream_map;
    
    for (const auto& span : timeline.spans) {
        auto& row = stream_map[span.stream_id];
        row.stream_id = span.stream_id;
        row.spans.push_back(&span);
    }
    
    std::vector<TimelineRow> rows;
    for (auto& [_, row] : stream_map) {
        rows.push_back(std::move(row));
    }
    
    return rows;
}

std::string TimelineViewer::renderRow(const TimelineRow& row, Timestamp start_time, Timestamp end_time) {
    // Create a character buffer for the timeline
    std::string buffer(config_.width, ' ');
    
    // Place events in the buffer
    for (const auto* span : row.spans) {
        size_t start_col = timeToColumn(span->start_time, start_time, end_time);
        size_t end_col = timeToColumn(span->end_time, start_time, end_time);
        
        // Clamp to buffer bounds
        start_col = std::min(start_col, config_.width - 1);
        end_col = std::min(end_col, config_.width - 1);
        
        // Fill the span
        for (size_t i = start_col; i <= end_col && i < config_.width; ++i) {
            buffer[i] = config_.fill_char;
        }
    }
    
    return buffer;
}

std::string TimelineViewer::formatTimestamp(Timestamp ts) {
    std::ostringstream ss;
    
    if (ts < 1000) {
        ss << ts << " ns";
    } else if (ts < 1000000) {
        ss << std::fixed << std::setprecision(2) << (ts / 1000.0) << " µs";
    } else if (ts < 1000000000) {
        ss << std::fixed << std::setprecision(2) << (ts / 1000000.0) << " ms";
    } else {
        ss << std::fixed << std::setprecision(3) << (ts / 1000000000.0) << " s";
    }
    
    return ss.str();
}

std::string TimelineViewer::formatDuration(Timestamp dur) {
    return formatTimestamp(dur);
}

size_t TimelineViewer::timeToColumn(Timestamp ts, Timestamp start_time, Timestamp end_time) {
    if (end_time <= start_time) {
        return 0;
    }
    
    double ratio = static_cast<double>(ts - start_time) / static_cast<double>(end_time - start_time);
    return static_cast<size_t>(ratio * config_.width);
}

} // namespace tracesmith
