#pragma once

#include "tracesmith/state/timeline_builder.hpp"
#include <string>
#include <vector>

namespace tracesmith {

/**
 * Text-based Timeline Viewer
 * 
 * Renders timeline as ASCII art for terminal viewing
 */
class TimelineViewer {
public:
    struct ViewConfig {
        size_t width;                 // Terminal width in characters
        size_t max_rows;              // Maximum rows to display
        bool show_timestamps;         // Show timestamp labels
        bool show_duration;           // Show duration in events
        char fill_char;               // Character for event bars
        
        ViewConfig() : width(80), max_rows(50), show_timestamps(true), 
                       show_duration(true), fill_char('#') {}
    };
    
    TimelineViewer(const ViewConfig& config = ViewConfig());
    
    /**
     * Render timeline as ASCII art
     * 
     * @param timeline Timeline to render
     * @return String representation
     */
    std::string render(const Timeline& timeline);
    
    /**
     * Render specific stream
     * 
     * @param timeline Timeline to render
     * @param stream_id Stream to display
     * @return String representation
     */
    std::string renderStream(const Timeline& timeline, uint32_t stream_id);
    
    /**
     * Render statistics summary
     * 
     * @param timeline Timeline with statistics
     * @return String representation
     */
    std::string renderStats(const Timeline& timeline);

private:
    ViewConfig config_;
    
    struct TimelineRow {
        uint32_t stream_id;
        std::vector<const TimelineSpan*> spans;
    };
    
    std::vector<TimelineRow> groupSpansByStream(const Timeline& timeline);
    std::string renderRow(const TimelineRow& row, Timestamp start_time, Timestamp end_time);
    std::string formatTimestamp(Timestamp ts);
    std::string formatDuration(Timestamp dur);
    size_t timeToColumn(Timestamp ts, Timestamp start_time, Timestamp end_time);
};

} // namespace tracesmith
