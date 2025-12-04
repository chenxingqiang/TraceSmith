#include "tracesmith/state/perfetto_exporter.hpp"
#include "tracesmith/common/types.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace tracesmith {

bool PerfettoExporter::exportToFile(const std::vector<TraceEvent>& events, const std::string& output_file) {
    return exportToFile(events, {}, output_file);
}

bool PerfettoExporter::exportToFile(const std::vector<TraceEvent>& events,
                                    const std::vector<CounterEvent>& counters,
                                    const std::string& output_file) {
    std::ofstream out(output_file);
    if (!out) {
        return false;
    }
    
    // Extract metadata from events
    extractMetadata(events);
    
    writeHeader(out);
    
    bool first = true;
    
    // Write metadata events (process/thread names)
    writeMetadataEvents(out, events, first);
    
    // Write counter track metadata
    if (enable_counter_tracks_ && !counters.empty()) {
        writeCounterTrackMetadata(out, counters, first);
    }
    
    // Write trace events
    for (const auto& event : events) {
        writeEvent(out, event, first);
    }
    
    // Write counter events
    if (enable_counter_tracks_ && !counters.empty()) {
        writeCounterEvents(out, counters, first);
    }
    
    // Write flow events for dependencies
    if (enable_flow_events_) {
        writeFlowEvents(out, events, first);
    }
    
    writeFooter(out);
    return true;
}

std::string PerfettoExporter::exportToString(const std::vector<TraceEvent>& events) {
    return exportToString(events, {});
}

std::string PerfettoExporter::exportToString(const std::vector<TraceEvent>& events,
                                             const std::vector<CounterEvent>& counters) {
    // Use string stream to build output
    std::ostringstream temp_file;
    
    // Extract metadata from events
    extractMetadata(events);
    
    writeHeader(temp_file);
    
    bool first = true;
    
    // Write metadata events
    writeMetadataEvents(temp_file, events, first);
    
    // Write counter track metadata
    if (enable_counter_tracks_ && !counters.empty()) {
        writeCounterTrackMetadata(temp_file, counters, first);
    }
    
    // Write trace events
    for (const auto& event : events) {
        writeEvent(temp_file, event, first);
    }
    
    // Write counter events
    if (enable_counter_tracks_ && !counters.empty()) {
        writeCounterEvents(temp_file, counters, first);
    }
    
    // Write flow events
    if (enable_flow_events_) {
        writeFlowEvents(temp_file, events, first);
    }
    
    writeFooter(temp_file);
    
    return temp_file.str();
}

void PerfettoExporter::writeHeader(std::ostream& out) {
    out << "{\n";
    out << "  \"traceEvents\": [\n";
}

void PerfettoExporter::writeEvent(std::ostream& out, const TraceEvent& event, bool& first) {
    if (!first) {
        out << ",\n";
    }
    first = false;
    
    out << "    {\n";
    out << "      \"name\": \"" << event.name << "\",\n";
    out << "      \"cat\": \"" << getEventCategory(event.type) << "\",\n";
    out << "      \"ph\": \"" << getEventPhase(event.type) << "\",\n";
    out << "      \"ts\": " << eventToMicroseconds(event.timestamp) << ",\n";
    out << "      \"pid\": " << event.device_id << ",\n";
    out << "      \"tid\": " << event.stream_id;
    
    // Add duration if available (must come before args)
    if (event.duration > 0) {
        out << ",\n      \"dur\": " << (event.duration / 1000);
    }
    
    // Use GPU track names if enabled
    out << ",\n      \"args\": {\n";
    if (enable_gpu_tracks_) {
        out << "        \"track_name\": \"" << getGPUTrackName(event.type) << "\"";
    }
    
    // Add correlation ID for flow events
    if (enable_flow_events_ && event.correlation_id != 0) {
        if (enable_gpu_tracks_) {
            out << ",\n        \"correlation_id\": " << event.correlation_id;
        } else {
            out << "\n        \"correlation_id\": " << event.correlation_id;
        }
    }
    
    // Write detailed args
    writeEventArgs(out, event);
    
    out << "\n      }\n";
    out << "    }";
}

void PerfettoExporter::writeFooter(std::ostream& out) {
    out << "\n  ],\n";
    out << "  \"displayTimeUnit\": \"ns\",\n";
    out << "  \"otherData\": {\n";
    out << "    \"version\": \"TraceSmith v" 
        << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << "\"\n";
    out << "  }\n";
    out << "}\n";
}

void PerfettoExporter::writeCounterTrackMetadata(std::ostream& out, const std::vector<CounterEvent>& counters, bool& first) {
    // Collect unique counter names
    counter_names_.clear();
    for (const auto& counter : counters) {
        counter_names_.insert(counter.counter_name);
    }
    
    // Use a separate process for counter tracks to keep them organized
    const uint32_t counter_pid = 9999; // Special PID for counter tracks
    
    // Write process metadata for counter tracks
    if (!first) {
        out << ",\n";
    }
    first = false;
    
    out << "    {\n";
    out << "      \"name\": \"process_name\",\n";
    out << "      \"ph\": \"M\",\n";
    out << "      \"pid\": " << counter_pid << ",\n";
    out << "      \"args\": {\n";
    out << "        \"name\": \"Performance Counters\"\n";
    out << "      }\n";
    out << "    }";
    
    // Write thread metadata for each counter track
    uint32_t counter_tid = 1;
    for (const auto& name : counter_names_) {
        if (!first) {
            out << ",\n";
        }
        first = false;
        
        out << "    {\n";
        out << "      \"name\": \"thread_name\",\n";
        out << "      \"ph\": \"M\",\n";
        out << "      \"pid\": " << counter_pid << ",\n";
        out << "      \"tid\": " << counter_tid << ",\n";
        out << "      \"args\": {\n";
        out << "        \"name\": \"" << name << "\"\n";
        out << "      }\n";
        out << "    }";
        
        counter_tid++;
    }
}

void PerfettoExporter::writeCounterEvents(std::ostream& out, const std::vector<CounterEvent>& counters, bool& first) {
    const uint32_t counter_pid = 9999; // Same as in writeCounterTrackMetadata
    
    // Build a map of counter name -> tid
    std::map<std::string, uint32_t> name_to_tid;
    uint32_t tid = 1;
    for (const auto& name : counter_names_) {
        name_to_tid[name] = tid++;
    }
    
    // Write counter events
    for (const auto& counter : counters) {
        if (!first) {
            out << ",\n";
        }
        first = false;
        
        out << "    {\n";
        out << "      \"name\": \"" << counter.counter_name << "\",\n";
        out << "      \"cat\": \"counter\",\n";
        out << "      \"ph\": \"C\",\n"; // Counter event
        out << "      \"ts\": " << eventToMicroseconds(counter.timestamp) << ",\n";
        out << "      \"pid\": " << counter_pid << ",\n";
        out << "      \"tid\": " << name_to_tid[counter.counter_name] << ",\n";
        out << "      \"args\": {\n";
        out << "        \"" << counter.counter_name << "\": " << std::fixed << std::setprecision(2) << counter.value;
        
        // Add unit if present
        if (!counter.unit.empty()) {
            out << ",\n        \"unit\": \"" << counter.unit << "\"";
        }
        
        out << "\n      }\n";
        out << "    }";
    }
}

std::string PerfettoExporter::getEventPhase(EventType type) {
    // Perfetto phases:
    // B = Begin, E = End, X = Complete, i = Instant
    // s = Async Start, f = Async Finish
    switch (type) {
        case EventType::KernelLaunch:
        case EventType::KernelComplete:
        case EventType::MemcpyH2D:
        case EventType::MemcpyD2H:
        case EventType::MemcpyD2D:
        case EventType::MemsetDevice:
            return "X"; // Complete event (has duration)
        case EventType::StreamCreate:
        case EventType::StreamDestroy:
        case EventType::EventRecord:
        case EventType::EventSync:
            return "i"; // Instant event
        case EventType::StreamSync:
        case EventType::DeviceSync:
            return "X"; // Complete (sync has duration)
        default:
            return "i";
    }
}

std::string PerfettoExporter::getEventCategory(EventType type) {
    switch (type) {
        case EventType::KernelLaunch:
        case EventType::KernelComplete:
            return "kernel";
        case EventType::MemcpyH2D:
        case EventType::MemcpyD2H:
        case EventType::MemcpyD2D:
        case EventType::MemsetDevice:
        case EventType::MemAlloc:
        case EventType::MemFree:
            return "memory";
        case EventType::StreamCreate:
        case EventType::StreamDestroy:
        case EventType::StreamSync:
            return "stream";
        case EventType::EventRecord:
        case EventType::EventSync:
        case EventType::DeviceSync:
            return "sync";
        default:
            return "other";
    }
}

uint64_t PerfettoExporter::eventToMicroseconds(Timestamp ns) {
    return ns / 1000; // Convert nanoseconds to microseconds
}

std::string PerfettoExporter::getGPUTrackName(EventType type) {
    switch (type) {
        case EventType::KernelLaunch:
        case EventType::KernelComplete:
            return "GPU Compute";
        case EventType::MemcpyH2D:
            return "GPU Memory Copy (H->D)";
        case EventType::MemcpyD2H:
            return "GPU Memory Copy (D->H)";
        case EventType::MemcpyD2D:
            return "GPU Memory Copy (D->D)";
        case EventType::MemAlloc:
        case EventType::MemFree:
        case EventType::MemsetDevice:
            return "GPU Memory Ops";
        case EventType::StreamSync:
        case EventType::DeviceSync:
        case EventType::EventSync:
            return "GPU Synchronization";
        case EventType::StreamCreate:
        case EventType::StreamDestroy:
        case EventType::EventRecord:
            return "GPU Stream";
        default:
            return "GPU Other";
    }
}

void PerfettoExporter::writeEventArgs(std::ostream& out, const TraceEvent& event) {
    // Write basic event metadata
    out << ",\n        \"event_type\": \"" << static_cast<int>(event.type) << "\"";
    out << ",\n        \"device_id\": " << event.device_id;
    out << ",\n        \"stream_id\": " << event.stream_id;
    
    // Kineto-inspired additions
    if (event.thread_id != 0) {
        out << ",\n        \"thread_id\": " << event.thread_id;
    }
    
    // Export flow information if present
    if (event.flow_info.id != 0) {
        out << ",\n        \"flow_id\": " << event.flow_info.id;
        out << ",\n        \"flow_type\": " << static_cast<int>(event.flow_info.type);
        out << ",\n        \"flow_start\": " << (event.flow_info.is_start ? "true" : "false");
    }
    
    // Export metadata if present
    if (!event.metadata.empty()) {
        for (const auto& [key, value] : event.metadata) {
            out << ",\n        \"" << key << "\": \"" << value << "\"";
        }
    }
    
    // Memory-specific parameters
    if (event.memory_params) {
        out << ",\n        \"size_bytes\": " << event.memory_params->size_bytes;
        out << ",\n        \"src_address\": \"0x" << std::hex << event.memory_params->src_address << std::dec << "\"";
        out << ",\n        \"dst_address\": \"0x" << std::hex << event.memory_params->dst_address << std::dec << "\"";
    }
    
    // Kernel-specific parameters
    if (event.kernel_params) {
        out << ",\n        \"grid_dim\": [" 
            << event.kernel_params->grid_x << ", "
            << event.kernel_params->grid_y << ", "
            << event.kernel_params->grid_z << "]";
        out << ",\n        \"block_dim\": [" 
            << event.kernel_params->block_x << ", "
            << event.kernel_params->block_y << ", "
            << event.kernel_params->block_z << "]";
        out << ",\n        \"shared_memory_bytes\": " << event.kernel_params->shared_mem_bytes;
        out << ",\n        \"registers_per_thread\": " << event.kernel_params->registers_per_thread;
    }
}

void PerfettoExporter::extractMetadata(const std::vector<TraceEvent>& events) {
    device_ids_.clear();
    stream_ids_.clear();
    
    for (const auto& event : events) {
        device_ids_.insert(event.device_id);
        stream_ids_.insert(event.stream_id);
    }
}

void PerfettoExporter::writeMetadataEvents(std::ostream& out, const std::vector<TraceEvent>& events, bool& first) {
    // Write process name metadata for each device
    for (uint32_t device_id : device_ids_) {
        if (!first) {
            out << ",\n";
        }
        first = false;
        
        out << "    {\n";
        out << "      \"name\": \"process_name\",\n";
        out << "      \"ph\": \"M\",\n";
        out << "      \"pid\": " << device_id << ",\n";
        out << "      \"args\": {\n";
        out << "        \"name\": \"GPU Device " << device_id << "\"\n";
        out << "      }\n";
        out << "    }";
    }
    
    // Write thread name metadata for each stream
    for (uint32_t stream_id : stream_ids_) {
        if (!first) {
            out << ",\n";
        }
        first = false;
        
        // Find device for this stream
        uint32_t device_id = 0;
        for (const auto& event : events) {
            if (event.stream_id == stream_id) {
                device_id = event.device_id;
                break;
            }
        }
        
        out << "    {\n";
        out << "      \"name\": \"thread_name\",\n";
        out << "      \"ph\": \"M\",\n";
        out << "      \"pid\": " << device_id << ",\n";
        out << "      \"tid\": " << stream_id << ",\n";
        out << "      \"args\": {\n";
        out << "        \"name\": \"Stream " << stream_id << "\"\n";
        out << "      }\n";
        out << "    }";
    }
}

void PerfettoExporter::writeFlowEvents(std::ostream& out, const std::vector<TraceEvent>& events, bool& first) {
    // Build correlation map (correlation_id -> events)
    std::map<uint64_t, std::vector<const TraceEvent*>> correlation_map;
    
    for (const auto& event : events) {
        if (event.correlation_id != 0) {
            correlation_map[event.correlation_id].push_back(&event);
        }
    }
    
    // Write flow events for correlated events
    for (const auto& [correlation_id, correlated_events] : correlation_map) {
        if (correlated_events.size() < 2) {
            continue; // Need at least 2 events to form a flow
        }
        
        // Sort by timestamp
        auto sorted_events = correlated_events;
        std::sort(sorted_events.begin(), sorted_events.end(),
                  [](const TraceEvent* a, const TraceEvent* b) {
                      return a->timestamp < b->timestamp;
                  });
        
        // Create flow from first to last event
        const TraceEvent* start_event = sorted_events.front();
        const TraceEvent* end_event = sorted_events.back();
        
        // Flow start
        if (!first) {
            out << ",\n";
        }
        first = false;
        
        out << "    {\n";
        out << "      \"name\": \"Dependency\",\n";
        out << "      \"cat\": \"flow\",\n";
        out << "      \"ph\": \"s\",\n"; // Flow start
        out << "      \"ts\": " << eventToMicroseconds(start_event->timestamp) << ",\n";
        out << "      \"pid\": " << start_event->device_id << ",\n";
        out << "      \"tid\": " << start_event->stream_id << ",\n";
        out << "      \"id\": " << correlation_id << ",\n";
        out << "      \"bp\": \"e\"\n"; // Binding point: enclosing
        out << "    }";
        
        // Flow end
        if (!first) {
            out << ",\n";
        }
        
        out << "    {\n";
        out << "      \"name\": \"Dependency\",\n";
        out << "      \"cat\": \"flow\",\n";
        out << "      \"ph\": \"f\",\n"; // Flow finish
        out << "      \"ts\": " << eventToMicroseconds(end_event->timestamp) << ",\n";
        out << "      \"pid\": " << end_event->device_id << ",\n";
        out << "      \"tid\": " << end_event->stream_id << ",\n";
        out << "      \"id\": " << correlation_id << ",\n";
        out << "      \"bp\": \"e\"\n";
        out << "    }";
    }
}

} // namespace tracesmith
