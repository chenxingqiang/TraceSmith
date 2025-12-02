#pragma once

#include "tracesmith/types.hpp"
#include <string>
#include <vector>
#include <fstream>

namespace tracesmith {

/**
 * Perfetto Trace Exporter
 * 
 * Exports TraceSmith events to Perfetto JSON format for visualization
 * in chrome://tracing or https://ui.perfetto.dev
 * 
 * Format: https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU
 */
class PerfettoExporter {
public:
    PerfettoExporter() = default;
    
    /**
     * Export events to Perfetto JSON format
     * 
     * @param events Events to export
     * @param output_file Path to output file
     * @return true if successful
     */
    bool exportToFile(const std::vector<TraceEvent>& events, const std::string& output_file);
    
    /**
     * Export to JSON string
     * 
     * @param events Events to export
     * @return JSON string in Perfetto format
     */
    std::string exportToString(const std::vector<TraceEvent>& events);

private:
    void writeHeader(std::ofstream& out);
    void writeEvent(std::ofstream& out, const TraceEvent& event, bool first);
    void writeFooter(std::ofstream& out);
    
    std::string getEventPhase(EventType type);
    std::string getEventCategory(EventType type);
    uint64_t eventToMicroseconds(Timestamp ns);
};

} // namespace tracesmith
