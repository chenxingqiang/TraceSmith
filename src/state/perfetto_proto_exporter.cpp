#include "tracesmith/state/perfetto_proto_exporter.hpp"
#include "tracesmith/state/perfetto_exporter.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

namespace tracesmith {

#ifdef TRACESMITH_PERFETTO_SDK_ENABLED

// Perfetto SDK implementation (PIMPL pattern) - simplified for ProtoZero
class PerfettoProtoExporter::PerfettoImpl {
public:
    // Reserved for future real-time tracing support
    PerfettoImpl() = default;
    ~PerfettoImpl() = default;
};

#endif // TRACESMITH_PERFETTO_SDK_ENABLED

PerfettoProtoExporter::PerfettoProtoExporter(Format format)
    : format_(format)
{
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    if (format == Format::PROTOBUF) {
        impl_ = std::make_unique<PerfettoImpl>();
        // ProtoZero doesn't require explicit initialization
    }
#else
    // Force JSON if SDK not available
    if (format == Format::PROTOBUF) {
        std::cerr << "Warning: Perfetto SDK not enabled, falling back to JSON format\n";
        format_ = Format::JSON;
    }
#endif
}

PerfettoProtoExporter::~PerfettoProtoExporter() {
    // ProtoZero version doesn't need cleanup
}

bool PerfettoProtoExporter::exportToFile(
    const std::vector<TraceEvent>& events,
    const std::string& output_file)
{
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    // Auto-detect format from file extension
    bool use_proto = false;
    if (format_ == Format::PROTOBUF) {
        // C++17 compatible suffix check
        auto has_suffix = [](const std::string& str, const std::string& suffix) {
            return str.size() >= suffix.size() && 
                   str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        
        if (has_suffix(output_file, ".perfetto-trace") || 
            has_suffix(output_file, ".pftrace")) {
            use_proto = true;
        }
    }
    
    if (use_proto && isSDKAvailable()) {
        // Use Perfetto SDK protobuf export
        auto proto_data = exportToProto(events);
        
        std::ofstream out(output_file, std::ios::binary);
        if (!out) {
            std::cerr << "Failed to open output file: " << output_file << "\n";
            return false;
        }
        
        out.write(reinterpret_cast<const char*>(proto_data.data()), 
                 proto_data.size());
        return out.good();
    }
#endif
    
    // Fallback to JSON export
    return exportToJSON(events, output_file);
}

#ifdef TRACESMITH_PERFETTO_SDK_ENABLED

// ProtoZero-based implementation - direct protobuf writing
std::vector<uint8_t> PerfettoProtoExporter::exportToProto(
    const std::vector<TraceEvent>& events)
{
    using namespace perfetto::protos::pbzero;
    
    // Create in-memory buffer for protobuf output
    protozero::HeapBuffered<perfetto::protos::pbzero::Trace> trace;
    
    uint32_t sequence_id = 1;
    
    // Write each event as a TracePacket
    for (const auto& event : events) {
        auto* packet = trace->add_packet();
        
        // Set timestamp (in nanoseconds)
        packet->set_timestamp(event.timestamp);
        
        // Set trusted sequence ID
        packet->set_trusted_packet_sequence_id(sequence_id);
        
        // Create TrackEvent
        auto* track_event = packet->set_track_event();
        
        // Set event name
        track_event->set_name(event.name);
        
        // Set event type
        auto perfetto_type = mapEventTypeToPerfetto(event.type);
        switch (perfetto_type) {
            case PerfettoEventType::SliceBegin:
                track_event->set_type(TrackEvent::TYPE_SLICE_BEGIN);
                break;
            case PerfettoEventType::SliceEnd:
                track_event->set_type(TrackEvent::TYPE_SLICE_END);
                break;
            case PerfettoEventType::Instant:
                track_event->set_type(TrackEvent::TYPE_INSTANT);
                break;
            case PerfettoEventType::Counter:
                track_event->set_type(TrackEvent::TYPE_COUNTER);
                if (event.duration > 0) {
                    track_event->set_counter_value(static_cast<int64_t>(event.duration));
                }
                break;
        }
        
        // Set track UUID (based on device_id and stream_id)
        uint64_t track_uuid = (static_cast<uint64_t>(event.device_id) << 32) | event.stream_id;
        track_event->set_track_uuid(track_uuid);
        
        // Add category
        track_event->add_categories(getEventCategory(event.type).c_str());
        
        // Add debug annotations for additional data
        if (event.kernel_params.has_value()) {
            const auto& kp = event.kernel_params.value();
            
            auto* grid_ann = track_event->add_debug_annotations();
            grid_ann->set_name("grid_dim");
            std::string grid_str = "[" + std::to_string(kp.grid_x) + "," + 
                                  std::to_string(kp.grid_y) + "," + 
                                  std::to_string(kp.grid_z) + "]";
            grid_ann->set_string_value(grid_str);
            
            auto* block_ann = track_event->add_debug_annotations();
            block_ann->set_name("block_dim");
            std::string block_str = "[" + std::to_string(kp.block_x) + "," + 
                                   std::to_string(kp.block_y) + "," + 
                                   std::to_string(kp.block_z) + "]";
            block_ann->set_string_value(block_str);
        }
        
        if (event.memory_params.has_value()) {
            const auto& mp = event.memory_params.value();
            
            auto* size_ann = track_event->add_debug_annotations();
            size_ann->set_name("size_bytes");
            size_ann->set_uint_value(mp.size_bytes);
        }
        
        // Add thread_id from Kineto schema
        if (event.thread_id != 0) {
            auto* tid_ann = track_event->add_debug_annotations();
            tid_ann->set_name("thread_id");
            tid_ann->set_uint_value(event.thread_id);
        }
        
        // Add metadata key-value pairs
        for (const auto& [key, value] : event.metadata) {
            auto* meta_ann = track_event->add_debug_annotations();
            meta_ann->set_name(key);
            meta_ann->set_string_value(value);
        }
    }
    
    // Serialize to binary
    std::vector<uint8_t> result = trace.SerializeAsArray();
    return result;
}

bool PerfettoProtoExporter::initializeTracingSession(const TracingConfig& /*config*/) {
    // Real-time tracing not implemented in ProtoZero version
    // Use exportToProto() for offline export instead
    std::cerr << "Real-time tracing not implemented. Use exportToProto() instead.\n";
    return false;
}

void PerfettoProtoExporter::stopTracingSession() {
    // No-op in ProtoZero version
}

void PerfettoProtoExporter::emitEvent(const TraceEvent& /*event*/) {
    // Not used in ProtoZero offline export
    // Events are batched and written via exportToProto()
}

void PerfettoProtoExporter::addGPUTrack(const std::string& track_name, uint32_t device_id) {
    // Track management for future use
    GPUTrack track;
    track.name = track_name;
    track.device_id = device_id;
    track.uuid = static_cast<uint64_t>(device_id) << 32 | gpu_tracks_.size();
    gpu_tracks_.push_back(track);
}

void PerfettoProtoExporter::addCounterTrack(const std::string& counter_name, uint32_t track_id) {
    // Track management for future use
    CounterTrack track;
    track.name = counter_name;
    track.track_id = track_id;
    track.uuid = static_cast<uint64_t>(track_id) << 32 | 0x1000;
    counter_tracks_.push_back(track);
}

void PerfettoProtoExporter::emitCounter(uint32_t /*track_id*/, int64_t /*value*/, Timestamp /*timestamp*/) {
    // Not implemented in ProtoZero offline export version
    // Use exportToProto() with counter events instead
}

PerfettoProtoExporter::PerfettoEventType 
PerfettoProtoExporter::mapEventTypeToPerfetto(EventType type) {
    switch (type) {
        case EventType::KernelLaunch:
        case EventType::KernelComplete:
        case EventType::MemcpyH2D:
        case EventType::MemcpyD2H:
        case EventType::MemcpyD2D:
        case EventType::MemsetDevice:
        case EventType::StreamSync:
        case EventType::DeviceSync:
            // Events with duration - use SliceBegin
            return PerfettoEventType::SliceBegin;
        case EventType::MemAlloc:
        case EventType::MemFree:
        case EventType::StreamCreate:
        case EventType::StreamDestroy:
        case EventType::EventRecord:
        case EventType::EventSync:
        case EventType::Marker:
        case EventType::RangeStart:
        case EventType::RangeEnd:
            return PerfettoEventType::Instant;
        default:
            return PerfettoEventType::Instant;
    }
}

std::string PerfettoProtoExporter::getEventCategory(EventType type) {
    switch (type) {
        case EventType::KernelLaunch:
        case EventType::KernelComplete:
            return "gpu_kernel";
        case EventType::MemcpyH2D:
        case EventType::MemcpyD2H:
        case EventType::MemcpyD2D:
        case EventType::MemsetDevice:
        case EventType::MemAlloc:
        case EventType::MemFree:
            return "gpu_memory";
        case EventType::StreamCreate:
        case EventType::StreamDestroy:
        case EventType::StreamSync:
            return "gpu_stream";
        case EventType::EventRecord:
        case EventType::EventSync:
        case EventType::DeviceSync:
            return "gpu_sync";
        default:
            return "gpu_other";
    }
}

#endif // TRACESMITH_PERFETTO_SDK_ENABLED

bool PerfettoProtoExporter::exportToJSON(
    const std::vector<TraceEvent>& events,
    const std::string& output_file)
{
    // Use existing JSON exporter
    PerfettoExporter json_exporter;
    return json_exporter.exportToFile(events, output_file);
}

// TracingSession implementation (v0.3.0)
bool TracingSession::exportToFile(const std::string& filename, bool use_protobuf) {
    if (state_ == State::Running) {
        flushEvents();
        flushCounters();
    }
    
    if (use_protobuf && PerfettoProtoExporter::isSDKAvailable()) {
        PerfettoProtoExporter exporter(PerfettoProtoExporter::Format::PROTOBUF);
        return exporter.exportToFile(flushed_events_, filename);
    } else {
        PerfettoExporter exporter;
        return exporter.exportToFile(flushed_events_, filename);
    }
}

} // namespace tracesmith
