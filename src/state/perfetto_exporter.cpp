#include "tracesmith/perfetto_exporter.hpp"
#include <sstream>
#include <iomanip>

namespace tracesmith {

bool PerfettoExporter::exportToFile(const std::vector<TraceEvent>& events, const std::string& output_file) {
    std::ofstream out(output_file);
    if (!out) {
        return false;
    }
    
    writeHeader(out);
    
    bool first = true;
    for (const auto& event : events) {
        writeEvent(out, event, first);
        first = false;
    }
    
    writeFooter(out);
    return true;
}

std::string PerfettoExporter::exportToString(const std::vector<TraceEvent>& events) {
    std::ostringstream ss;
    ss << "{\n  \"traceEvents\": [\n";
    
    bool first = true;
    for (const auto& event : events) {
        if (!first) {
            ss << ",\n";
        }
        first = false;
        
        // Convert event to Perfetto JSON format
        ss << "    {\n";
        ss << "      \"name\": \"" << event.name << "\",\n";
        ss << "      \"cat\": \"" << getEventCategory(event.type) << "\",\n";
        ss << "      \"ph\": \"" << getEventPhase(event.type) << "\",\n";
        ss << "      \"ts\": " << eventToMicroseconds(event.timestamp) << ",\n";
        ss << "      \"pid\": " << event.device_id << ",\n";
        ss << "      \"tid\": " << event.stream_id << ",\n";
        ss << "      \"id\": " << event.correlation_id;
        
        // Add duration if available
        if (event.duration > 0) {
            ss << ",\n      \"dur\": " << (event.duration / 1000);
        }
        
        // Add args
        ss << ",\n      \"args\": {\n";
        ss << "        \"type\": \"" << static_cast<int>(event.type) << "\",\n";
        ss << "        \"device_id\": " << event.device_id << ",\n";
        ss << "        \"stream_id\": " << event.stream_id;
        
        if (event.memory_params) {
            ss << ",\n        \"size_bytes\": " << event.memory_params->size_bytes;
        }
        
        ss << "\n      }\n";
        ss << "    }";
    }
    
    ss << "\n  ],\n";
    ss << "  \"displayTimeUnit\": \"ns\",\n";
    ss << "  \"otherData\": {\n";
    ss << "    \"version\": \"TraceSmith v0.1\"\n";
    ss << "  }\n";
    ss << "}\n";
    
    return ss.str();
}

void PerfettoExporter::writeHeader(std::ofstream& out) {
    out << "{\n";
    out << "  \"traceEvents\": [\n";
}

void PerfettoExporter::writeEvent(std::ofstream& out, const TraceEvent& event, bool first) {
    if (!first) {
        out << ",\n";
    }
    
    out << "    {\n";
    out << "      \"name\": \"" << event.name << "\",\n";
    out << "      \"cat\": \"" << getEventCategory(event.type) << "\",\n";
    out << "      \"ph\": \"" << getEventPhase(event.type) << "\",\n";
    out << "      \"ts\": " << eventToMicroseconds(event.timestamp) << ",\n";
    out << "      \"pid\": " << event.device_id << ",\n";
    out << "      \"tid\": " << event.stream_id << ",\n";
    out << "      \"id\": " << event.correlation_id;
    
    if (event.duration > 0) {
        out << ",\n      \"dur\": " << (event.duration / 1000);
    }
    
    out << ",\n      \"args\": {\n";
    out << "        \"type\": \"" << static_cast<int>(event.type) << "\",\n";
    out << "        \"device_id\": " << event.device_id << ",\n";
    out << "        \"stream_id\": " << event.stream_id;
    
    if (event.memory_params) {
        out << ",\n        \"size_bytes\": " << event.memory_params->size_bytes;
    }
    
    out << "\n      }\n";
    out << "    }";
}

void PerfettoExporter::writeFooter(std::ofstream& out) {
    out << "\n  ],\n";
    out << "  \"displayTimeUnit\": \"ns\",\n";
    out << "  \"otherData\": {\n";
    out << "    \"version\": \"TraceSmith v0.1\"\n";
    out << "  }\n";
    out << "}\n";
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

} // namespace tracesmith
