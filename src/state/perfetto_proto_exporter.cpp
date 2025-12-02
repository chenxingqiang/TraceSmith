#include "tracesmith/perfetto_proto_exporter.hpp"
#include "tracesmith/perfetto_exporter.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

namespace tracesmith {

#ifdef TRACESMITH_PERFETTO_SDK_ENABLED

// Perfetto SDK implementation (PIMPL pattern)
class PerfettoProtoExporter::PerfettoImpl {
public:
    std::unique_ptr<perfetto::TracingSession> session;
    TracingConfig config;
    bool is_initialized = false;
    
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
        
        // Initialize Perfetto SDK
        perfetto::TracingInitArgs args;
        args.backends = perfetto::kInProcessBackend;
        perfetto::Tracing::Initialize(args);
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
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    if (impl_ && impl_->is_initialized) {
        stopTracingSession();
    }
#endif
}

bool PerfettoProtoExporter::exportToFile(
    const std::vector<TraceEvent>& events,
    const std::string& output_file)
{
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
    
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
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

std::vector<uint8_t> PerfettoProtoExporter::exportToProto(
    const std::vector<TraceEvent>& events)
{
    if (!impl_) {
        std::cerr << "Perfetto SDK not initialized\n";
        return {};
    }
    
    // Create a tracing session for offline export
    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(4096);
    
    // Add data source config
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");
    
    // Create session
    auto session = perfetto::Tracing::NewTrace();
    session->Setup(cfg);
    session->StartBlocking();
    
    // Emit all events
    for (const auto& event : events) {
        emitEvent(event);
    }
    
    // Stop and read trace
    session->StopBlocking();
    
    std::vector<char> trace_data(session->ReadTraceBlocking());
    
    // Convert char vector to uint8_t vector
    std::vector<uint8_t> result(trace_data.begin(), trace_data.end());
    
    return result;
}

bool PerfettoProtoExporter::initializeTracingSession(const TracingConfig& config) {
    if (!impl_) {
        return false;
    }
    
    if (impl_->is_initialized) {
        std::cerr << "Tracing session already initialized\n";
        return false;
    }
    
    // Configure trace
    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(config.buffer_size_kb);
    
    if (config.duration_ms > 0) {
        cfg.set_duration_ms(config.duration_ms);
    }
    
    // Add track event data source
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");
    
    // Output to file if specified
    if (config.write_to_file && !config.output_file.empty()) {
        cfg.set_write_into_file(true);
        cfg.set_output_path(config.output_file);
    }
    
    // Create and start session
    impl_->session = perfetto::Tracing::NewTrace();
    impl_->session->Setup(cfg);
    impl_->session->StartBlocking();
    
    impl_->config = config;
    impl_->is_initialized = true;
    
    return true;
}

void PerfettoProtoExporter::stopTracingSession() {
    if (!impl_ || !impl_->is_initialized) {
        return;
    }
    
    impl_->session->StopBlocking();
    
    // If not writing to file, read trace data
    if (!impl_->config.write_to_file && !impl_->config.output_file.empty()) {
        std::vector<char> trace_data(impl_->session->ReadTraceBlocking());
        
        std::ofstream out(impl_->config.output_file, std::ios::binary);
        if (out) {
            out.write(trace_data.data(), trace_data.size());
        }
    }
    
    impl_->session.reset();
    impl_->is_initialized = false;
}

void PerfettoProtoExporter::emitEvent(const TraceEvent& event) {
    if (!impl_ || !impl_->is_initialized) {
        return;
    }
    
    // Get track for this event
    perfetto::Track track = getOrCreateTrackForEvent(event);
    
    // Map event type to Perfetto event type
    auto perfetto_type = mapEventTypeToPerfetto(event.type);
    
    // Emit event based on type
    switch (perfetto_type) {
        case PerfettoEventType::SliceBegin: {
            TRACE_EVENT_BEGIN(
                "gpu", 
                getTrackNameForEvent(event),
                track,
                event.timestamp
            );
            break;
        }
        
        case PerfettoEventType::SliceEnd: {
            TRACE_EVENT_END(
                "gpu",
                track,
                event.timestamp + event.duration
            );
            break;
        }
        
        case PerfettoEventType::Instant: {
            TRACE_EVENT_INSTANT(
                "gpu",
                event.name.c_str(),
                track,
                event.timestamp
            );
            break;
        }
        
        case PerfettoEventType::Counter:
            // Handled separately via emitCounter()
            break;
    }
}

void PerfettoProtoExporter::addGPUTrack(const std::string& track_name, uint32_t device_id) {
    GPUTrack track;
    track.name = track_name;
    track.device_id = device_id;
    track.uuid = static_cast<uint64_t>(device_id) << 32 | gpu_tracks_.size();
    gpu_tracks_.push_back(track);
}

void PerfettoProtoExporter::addCounterTrack(const std::string& counter_name, uint32_t track_id) {
    CounterTrack track;
    track.name = counter_name;
    track.track_id = track_id;
    track.uuid = static_cast<uint64_t>(track_id) << 32 | 0x1000;
    counter_tracks_.push_back(track);
}

void PerfettoProtoExporter::emitCounter(uint32_t track_id, int64_t value, Timestamp timestamp) {
    if (!impl_ || !impl_->is_initialized) {
        return;
    }
    
    // Find counter track
    for (const auto& track : counter_tracks_) {
        if (track.track_id == track_id) {
            perfetto::Track counter_track(track.uuid);
            TRACE_COUNTER("gpu_metrics", counter_track, timestamp ? timestamp : getCurrentTimestamp(), value);
            return;
        }
    }
}

perfetto::Track PerfettoProtoExporter::getOrCreateTrackForEvent(const TraceEvent& event) {
    // Create track UUID from device_id and stream_id
    uint64_t uuid = (static_cast<uint64_t>(event.device_id) << 32) | event.stream_id;
    return perfetto::Track(uuid);
}

const char* PerfettoProtoExporter::getTrackNameForEvent(const TraceEvent& event) {
    return event.name.c_str();
}

PerfettoProtoExporter::PerfettoEventType 
PerfettoProtoExporter::mapEventTypeToPerfetto(EventType type) {
    switch (type) {
        case EventType::KernelLaunch:
            return PerfettoEventType::SliceBegin;
        case EventType::KernelComplete:
            return PerfettoEventType::SliceEnd;
        case EventType::MemcpyH2D:
        case EventType::MemcpyD2H:
        case EventType::MemcpyD2D:
        case EventType::StreamSync:
        case EventType::DeviceSync:
            // These have duration, use SliceBegin
            return PerfettoEventType::SliceBegin;
        case EventType::MemAlloc:
        case EventType::MemFree:
        case EventType::StreamCreate:
        case EventType::StreamDestroy:
        case EventType::EventRecord:
        case EventType::Marker:
            return PerfettoEventType::Instant;
        default:
            return PerfettoEventType::Instant;
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

} // namespace tracesmith
