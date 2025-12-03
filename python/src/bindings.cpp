/**
 * TraceSmith Python Bindings
 * 
 * Provides Python access to TraceSmith GPU profiling and replay functionality.
 * 
 * v0.2.0 Additions:
 * - Kineto schema fields (thread_id, metadata, flow_info)
 * - PerfettoProtoExporter for protobuf export
 * - FlowType enum
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <pybind11/functional.h>

#include "tracesmith/types.hpp"
#include "tracesmith/profiler.hpp"
#include "tracesmith/sbt_format.hpp"
#include "tracesmith/timeline_builder.hpp"
#include "tracesmith/perfetto_exporter.hpp"
#include "tracesmith/perfetto_proto_exporter.hpp"
#include "tracesmith/replay_engine.hpp"

namespace py = pybind11;
using namespace tracesmith;

PYBIND11_MODULE(_tracesmith, m) {
    m.doc() = "TraceSmith GPU Profiling & Replay System";
    
    // Version info
    m.attr("__version__") = "0.1.0";
    m.attr("VERSION_MAJOR") = VERSION_MAJOR;
    m.attr("VERSION_MINOR") = VERSION_MINOR;
    m.attr("VERSION_PATCH") = VERSION_PATCH;
    
    // EventType enum
    py::enum_<EventType>(m, "EventType")
        .value("Unknown", EventType::Unknown)
        .value("KernelLaunch", EventType::KernelLaunch)
        .value("KernelComplete", EventType::KernelComplete)
        .value("MemcpyH2D", EventType::MemcpyH2D)
        .value("MemcpyD2H", EventType::MemcpyD2H)
        .value("MemcpyD2D", EventType::MemcpyD2D)
        .value("MemsetDevice", EventType::MemsetDevice)
        .value("StreamSync", EventType::StreamSync)
        .value("DeviceSync", EventType::DeviceSync)
        .value("EventRecord", EventType::EventRecord)
        .value("EventSync", EventType::EventSync)
        .value("StreamCreate", EventType::StreamCreate)
        .value("StreamDestroy", EventType::StreamDestroy)
        .value("MemAlloc", EventType::MemAlloc)
        .value("MemFree", EventType::MemFree)
        .value("Marker", EventType::Marker)
        .value("RangeStart", EventType::RangeStart)
        .value("RangeEnd", EventType::RangeEnd)
        .value("Custom", EventType::Custom)
        .export_values();
    
    // FlowType enum (Kineto-compatible)
    py::enum_<FlowType>(m, "FlowType")
        .value("NoFlow", FlowType::None)
        .value("FwdBwd", FlowType::FwdBwd)
        .value("AsyncCpuGpu", FlowType::AsyncCpuGpu)
        .value("Custom", FlowType::Custom)
        .export_values();
    
    // FlowInfo class (Kineto-compatible)
    py::class_<FlowInfo>(m, "FlowInfo")
        .def(py::init<>())
        .def(py::init<uint64_t, FlowType, bool>(),
             py::arg("id"), py::arg("type"), py::arg("is_start"))
        .def_readwrite("id", &FlowInfo::id)
        .def_readwrite("type", &FlowInfo::type)
        .def_readwrite("is_start", &FlowInfo::is_start)
        .def("__repr__", [](const FlowInfo& f) {
            return "<FlowInfo id=" + std::to_string(f.id) + 
                   " type=" + std::to_string(static_cast<int>(f.type)) +
                   " is_start=" + (f.is_start ? "True" : "False") + ">";
        });
    
    // TraceEvent class (with Kineto-compatible fields)
    py::class_<TraceEvent>(m, "TraceEvent")
        .def(py::init<>())
        .def(py::init<EventType, Timestamp>())
        .def_readwrite("type", &TraceEvent::type)
        .def_readwrite("timestamp", &TraceEvent::timestamp)
        .def_readwrite("duration", &TraceEvent::duration)
        .def_readwrite("device_id", &TraceEvent::device_id)
        .def_readwrite("stream_id", &TraceEvent::stream_id)
        .def_readwrite("correlation_id", &TraceEvent::correlation_id)
        .def_readwrite("name", &TraceEvent::name)
        // Kineto-compatible fields (v0.2.0)
        .def_readwrite("thread_id", &TraceEvent::thread_id)
        .def_readwrite("metadata", &TraceEvent::metadata)
        .def_readwrite("flow_info", &TraceEvent::flow_info)
        .def("__repr__", [](const TraceEvent& e) {
            return "<TraceEvent " + e.name + " type=" + 
                   std::string(eventTypeToString(e.type)) + 
                   " thread=" + std::to_string(e.thread_id) + ">";
        });
    
    // DeviceInfo class
    py::class_<DeviceInfo>(m, "DeviceInfo")
        .def(py::init<>())
        .def_readwrite("device_id", &DeviceInfo::device_id)
        .def_readwrite("name", &DeviceInfo::name)
        .def_readwrite("vendor", &DeviceInfo::vendor)
        .def_readwrite("total_memory", &DeviceInfo::total_memory)
        .def_readwrite("multiprocessor_count", &DeviceInfo::multiprocessor_count);
    
    // MemoryEvent class (Kineto-compatible, v0.2.0)
    py::enum_<MemoryEvent::Category>(m, "MemoryCategory")
        .value("Unknown", MemoryEvent::Category::Unknown)
        .value("Activation", MemoryEvent::Category::Activation)
        .value("Gradient", MemoryEvent::Category::Gradient)
        .value("Parameter", MemoryEvent::Category::Parameter)
        .value("Temporary", MemoryEvent::Category::Temporary)
        .value("Cached", MemoryEvent::Category::Cached)
        .export_values();
    
    py::class_<MemoryEvent>(m, "MemoryEvent")
        .def(py::init<>())
        .def_readwrite("timestamp", &MemoryEvent::timestamp)
        .def_readwrite("device_id", &MemoryEvent::device_id)
        .def_readwrite("thread_id", &MemoryEvent::thread_id)
        .def_readwrite("bytes", &MemoryEvent::bytes)
        .def_readwrite("ptr", &MemoryEvent::ptr)
        .def_readwrite("is_allocation", &MemoryEvent::is_allocation)
        .def_readwrite("allocator_name", &MemoryEvent::allocator_name)
        .def_readwrite("category", &MemoryEvent::category)
        .def("__repr__", [](const MemoryEvent& e) {
            return "<MemoryEvent " + 
                   std::string(e.is_allocation ? "alloc" : "free") + 
                   " " + std::to_string(e.bytes) + " bytes>";
        });
    
    // CounterEvent class (Kineto-compatible, v0.2.0)
    py::class_<CounterEvent>(m, "CounterEvent")
        .def(py::init<>())
        .def(py::init<const std::string&, double, Timestamp>(),
             py::arg("name"), py::arg("value"), py::arg("timestamp") = 0)
        .def_readwrite("timestamp", &CounterEvent::timestamp)
        .def_readwrite("device_id", &CounterEvent::device_id)
        .def_readwrite("track_id", &CounterEvent::track_id)
        .def_readwrite("counter_name", &CounterEvent::counter_name)
        .def_readwrite("value", &CounterEvent::value)
        .def_readwrite("unit", &CounterEvent::unit)
        .def("__repr__", [](const CounterEvent& e) {
            return "<CounterEvent " + e.counter_name + "=" + 
                   std::to_string(e.value) + " " + e.unit + ">";
        });
    
    // TraceMetadata class
    py::class_<TraceMetadata>(m, "TraceMetadata")
        .def(py::init<>())
        .def_readwrite("application_name", &TraceMetadata::application_name)
        .def_readwrite("command_line", &TraceMetadata::command_line)
        .def_readwrite("start_time", &TraceMetadata::start_time)
        .def_readwrite("end_time", &TraceMetadata::end_time);
    
    // PlatformType enum
    py::enum_<PlatformType>(m, "PlatformType")
        .value("Unknown", PlatformType::Unknown)
        .value("CUDA", PlatformType::CUDA)
        .value("ROCm", PlatformType::ROCm)
        .value("Metal", PlatformType::Metal)
        .value("Simulation", PlatformType::Simulation)
        .export_values();
    
    // ProfilerConfig class
    py::class_<ProfilerConfig>(m, "ProfilerConfig")
        .def(py::init<>())
        .def_readwrite("buffer_size", &ProfilerConfig::buffer_size)
        .def_readwrite("capture_callstacks", &ProfilerConfig::capture_callstacks)
        .def_readwrite("capture_kernels", &ProfilerConfig::capture_kernels)
        .def_readwrite("capture_memcpy", &ProfilerConfig::capture_memcpy);
    
    // SimulationProfiler class
    py::class_<SimulationProfiler>(m, "SimulationProfiler")
        .def(py::init<>())
        .def("initialize", &SimulationProfiler::initialize)
        .def("finalize", &SimulationProfiler::finalize)
        .def("start_capture", &SimulationProfiler::startCapture)
        .def("stop_capture", &SimulationProfiler::stopCapture)
        .def("is_capturing", &SimulationProfiler::isCapturing)
        .def("get_events", [](SimulationProfiler& p) {
            std::vector<TraceEvent> events;
            p.getEvents(events, 0);
            return events;
        })
        .def("generate_kernel_event", &SimulationProfiler::generateKernelEvent,
             py::arg("name"), py::arg("stream_id") = 0)
        .def("generate_memcpy_event", &SimulationProfiler::generateMemcpyEvent,
             py::arg("type"), py::arg("size"), py::arg("stream_id") = 0)
        .def("events_captured", &SimulationProfiler::eventsCaptured)
        .def("events_dropped", &SimulationProfiler::eventsDropped);
    
    // SBTWriter class
    py::class_<SBTWriter>(m, "SBTWriter")
        .def(py::init<const std::string&>())
        .def("is_open", &SBTWriter::isOpen)
        .def("write_metadata", &SBTWriter::writeMetadata)
        .def("write_device_info", &SBTWriter::writeDeviceInfo)
        .def("write_event", &SBTWriter::writeEvent)
        .def("write_events", &SBTWriter::writeEvents)
        .def("finalize", &SBTWriter::finalize)
        .def("event_count", &SBTWriter::eventCount);
    
    // SBTReader class
    py::class_<SBTReader>(m, "SBTReader")
        .def(py::init<const std::string&>())
        .def("is_open", &SBTReader::isOpen)
        .def("is_valid", &SBTReader::isValid)
        .def("event_count", &SBTReader::eventCount)
        .def("read_all", [](SBTReader& r) {
            TraceRecord record;
            r.readAll(record);
            return py::make_tuple(record.metadata(), record.events());
        });
    
    // TimelineSpan class
    py::class_<TimelineSpan>(m, "TimelineSpan")
        .def(py::init<>())
        .def_readwrite("correlation_id", &TimelineSpan::correlation_id)
        .def_readwrite("device_id", &TimelineSpan::device_id)
        .def_readwrite("stream_id", &TimelineSpan::stream_id)
        .def_readwrite("type", &TimelineSpan::type)
        .def_readwrite("name", &TimelineSpan::name)
        .def_readwrite("start_time", &TimelineSpan::start_time)
        .def_readwrite("end_time", &TimelineSpan::end_time);
    
    // Timeline class
    py::class_<Timeline>(m, "Timeline")
        .def(py::init<>())
        .def_readwrite("spans", &Timeline::spans)
        .def_readwrite("total_duration", &Timeline::total_duration)
        .def_readwrite("gpu_utilization", &Timeline::gpu_utilization)
        .def_readwrite("max_concurrent_ops", &Timeline::max_concurrent_ops);
    
    // TimelineBuilder class
    py::class_<TimelineBuilder>(m, "TimelineBuilder")
        .def(py::init<>())
        .def("add_event", &TimelineBuilder::addEvent)
        .def("add_events", &TimelineBuilder::addEvents)
        .def("build", &TimelineBuilder::build)
        .def("clear", &TimelineBuilder::clear);
    
    // PerfettoExporter class (JSON format)
    py::class_<PerfettoExporter>(m, "PerfettoExporter")
        .def(py::init<>())
        .def("export_to_file", &PerfettoExporter::exportToFile)
        .def("export_to_string", &PerfettoExporter::exportToString)
        .def("set_enable_gpu_tracks", &PerfettoExporter::setEnableGPUTracks)
        .def("set_enable_flow_events", &PerfettoExporter::setEnableFlowEvents);
    
    // PerfettoProtoExporter class (Protobuf format - v0.2.0)
    py::enum_<PerfettoProtoExporter::Format>(m, "PerfettoFormat")
        .value("JSON", PerfettoProtoExporter::Format::JSON)
        .value("PROTOBUF", PerfettoProtoExporter::Format::PROTOBUF)
        .export_values();
    
    py::class_<PerfettoProtoExporter>(m, "PerfettoProtoExporter")
        .def(py::init<PerfettoProtoExporter::Format>(),
             py::arg("format") = PerfettoProtoExporter::Format::PROTOBUF)
        .def("export_to_file", &PerfettoProtoExporter::exportToFile,
             py::arg("events"), py::arg("output_file"),
             "Export events to file (auto-detects format from extension)")
        .def("get_format", &PerfettoProtoExporter::getFormat)
        .def_static("is_sdk_available", &PerfettoProtoExporter::isSDKAvailable,
                   "Check if Perfetto SDK is available for protobuf export");
    
    // TracingSession class (Real-time tracing - v0.3.0)
    py::enum_<TracingSession::State>(m, "TracingState")
        .value("Stopped", TracingSession::State::Stopped)
        .value("Starting", TracingSession::State::Starting)
        .value("Running", TracingSession::State::Running)
        .value("Stopping", TracingSession::State::Stopping)
        .export_values();
    
    py::enum_<TracingSession::Mode>(m, "TracingMode")
        .value("InProcess", TracingSession::Mode::InProcess)
        .value("File", TracingSession::Mode::File)
        .export_values();
    
    py::class_<TracingSession::Statistics>(m, "TracingStatistics")
        .def(py::init<>())
        .def_readwrite("events_emitted", &TracingSession::Statistics::events_emitted)
        .def_readwrite("events_dropped", &TracingSession::Statistics::events_dropped)
        .def_readwrite("counters_emitted", &TracingSession::Statistics::counters_emitted)
        .def_readwrite("start_time", &TracingSession::Statistics::start_time)
        .def_readwrite("stop_time", &TracingSession::Statistics::stop_time)
        .def("duration_ms", &TracingSession::Statistics::duration_ms);
    
    py::class_<TracingSession>(m, "TracingSession")
        .def(py::init<>())
        .def(py::init<size_t, size_t>(),
             py::arg("event_buffer_size"), py::arg("counter_buffer_size") = 4096)
        .def("start", &TracingSession::start, py::arg("config"),
             "Start tracing session")
        .def("stop", &TracingSession::stop, "Stop tracing session")
        .def("is_active", &TracingSession::isActive)
        .def("get_state", &TracingSession::getState)
        .def("get_mode", &TracingSession::getMode)
        .def("get_statistics", &TracingSession::getStatistics)
        .def("emit", py::overload_cast<const TraceEvent&>(&TracingSession::emit),
             py::arg("event"), "Emit a trace event (thread-safe)")
        .def("emit_counter", &TracingSession::emitCounter,
             py::arg("name"), py::arg("value"), py::arg("timestamp") = 0,
             "Emit a counter value")
        .def("get_events", &TracingSession::getEvents,
             py::return_value_policy::reference_internal)
        .def("get_counters", &TracingSession::getCounters,
             py::return_value_policy::reference_internal)
        .def("export_to_file", &TracingSession::exportToFile,
             py::arg("filename"), py::arg("use_protobuf") = true,
             "Export session to Perfetto file")
        .def("clear", &TracingSession::clear)
        .def("event_buffer_size", &TracingSession::eventBufferSize)
        .def("event_buffer_capacity", &TracingSession::eventBufferCapacity)
        .def("events_dropped", &TracingSession::eventsDropped);
    
    // ReplayMode enum
    py::enum_<ReplayMode>(m, "ReplayMode")
        .value("Full", ReplayMode::Full)
        .value("Partial", ReplayMode::Partial)
        .value("DryRun", ReplayMode::DryRun)
        .value("StreamSpecific", ReplayMode::StreamSpecific)
        .export_values();
    
    // ReplayConfig class
    py::class_<ReplayConfig>(m, "ReplayConfig")
        .def(py::init<>())
        .def_readwrite("mode", &ReplayConfig::mode)
        .def_readwrite("validate_order", &ReplayConfig::validate_order)
        .def_readwrite("validate_dependencies", &ReplayConfig::validate_dependencies)
        .def_readwrite("verbose", &ReplayConfig::verbose);
    
    // ReplayResult class
    py::class_<ReplayResult>(m, "ReplayResult")
        .def(py::init<>())
        .def_readwrite("success", &ReplayResult::success)
        .def_readwrite("deterministic", &ReplayResult::deterministic)
        .def_readwrite("operations_total", &ReplayResult::operations_total)
        .def_readwrite("operations_executed", &ReplayResult::operations_executed)
        .def_readwrite("operations_failed", &ReplayResult::operations_failed)
        .def_readwrite("replay_duration", &ReplayResult::replay_duration)
        .def_readwrite("errors", &ReplayResult::errors)
        .def("summary", &ReplayResult::summary);
    
    // ReplayEngine class
    py::class_<ReplayEngine>(m, "ReplayEngine")
        .def(py::init<>())
        .def("load_trace", &ReplayEngine::loadTrace)
        .def("load_events", &ReplayEngine::loadEvents)
        .def("replay", &ReplayEngine::replay);
    
    // Helper functions
    m.def("get_current_timestamp", &getCurrentTimestamp,
          "Get current timestamp in nanoseconds");
    
    m.def("event_type_to_string", &eventTypeToString,
          "Convert EventType to string");
    
    m.def("create_profiler", [](PlatformType type) {
        return createProfiler(type);
    }, "Create a profiler for the specified platform");
}
