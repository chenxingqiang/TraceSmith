#pragma once

#include "tracesmith/types.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <map>
#include <set>

namespace tracesmith {

struct PerfettoMetadata {
    std::string process_name;
    std::string thread_name;
    std::map<std::string, std::string> custom_metadata;
};

/**
 * Enhanced Perfetto Trace Exporter
 * 
 * Exports TraceSmith events to Perfetto JSON format with GPU-specific enhancements:
 * - Separate tracks for GPU compute, memory, and synchronization
 * - Process/thread metadata for better visualization
 * - Performance counters and GPU state information
 * - Flow events for dependency tracking
 * 
 * Compatible with:
 * - chrome://tracing
 * - https://ui.perfetto.dev
 * - Perfetto command-line tools
 * 
 * Format: https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU
 */
class PerfettoExporter {
public:
    PerfettoExporter() = default;
    
    /**
     * Export events to enhanced Perfetto JSON format
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
    
    /**
     * Enable GPU-specific track separation
     */
    void setEnableGPUTracks(bool enable) { enable_gpu_tracks_ = enable; }
    
    /**
     * Enable flow events for dependency tracking
     */
    void setEnableFlowEvents(bool enable) { enable_flow_events_ = enable; }
    
    /**
     * Set custom metadata for process/thread naming
     */
    void setMetadata(const PerfettoMetadata& metadata) { metadata_ = metadata; }

private:
    void writeHeader(std::ostream& out);
    void writeMetadataEvents(std::ostream& out, const std::vector<TraceEvent>& events, bool& first);
    void writeEvent(std::ostream& out, const TraceEvent& event, bool& first);
    void writeFlowEvents(std::ostream& out, const std::vector<TraceEvent>& events, bool& first);
    void writeFooter(std::ostream& out);
    
    std::string getEventPhase(EventType type);
    std::string getEventCategory(EventType type);
    std::string getGPUTrackName(EventType type);
    uint64_t eventToMicroseconds(Timestamp ns);
    void writeEventArgs(std::ostream& out, const TraceEvent& event);
    
    // Extract unique process/thread IDs for metadata
    void extractMetadata(const std::vector<TraceEvent>& events);
    
    bool enable_gpu_tracks_ = true;
    bool enable_flow_events_ = true;
    PerfettoMetadata metadata_;
    std::set<uint32_t> device_ids_;
    std::set<uint32_t> stream_ids_;
};

} // namespace tracesmith
