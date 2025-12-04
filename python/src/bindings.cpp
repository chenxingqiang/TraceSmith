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
#include "tracesmith/cupti_profiler.hpp"
#include "tracesmith/sbt_format.hpp"
#include "tracesmith/timeline_builder.hpp"
#include "tracesmith/perfetto_exporter.hpp"
#include "tracesmith/perfetto_proto_exporter.hpp"
#include "tracesmith/replay_engine.hpp"
#include "tracesmith/frame_capture.hpp"
#include "tracesmith/memory_profiler.hpp"
#include "tracesmith/xray_importer.hpp"
#include "tracesmith/bpf_types.hpp"

namespace py = pybind11;
using namespace tracesmith;

PYBIND11_MODULE(_tracesmith, m) {
    m.doc() = "TraceSmith GPU Profiling & Replay System";
    
    // Version info
    m.attr("__version__") = "0.6.4";
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
        .export_values();
    
    // ProfilerConfig class
    py::class_<ProfilerConfig>(m, "ProfilerConfig")
        .def(py::init<>())
        .def_readwrite("buffer_size", &ProfilerConfig::buffer_size)
        .def_readwrite("capture_callstacks", &ProfilerConfig::capture_callstacks)
        .def_readwrite("capture_kernels", &ProfilerConfig::capture_kernels)
        .def_readwrite("capture_memcpy", &ProfilerConfig::capture_memcpy);
    
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
        .def("export_to_file", 
             py::overload_cast<const std::vector<TraceEvent>&, const std::string&>(
                 &PerfettoExporter::exportToFile),
             py::arg("events"), py::arg("output_file"))
        .def("export_to_file_with_counters",
             py::overload_cast<const std::vector<TraceEvent>&, const std::vector<CounterEvent>&, const std::string&>(
                 &PerfettoExporter::exportToFile),
             py::arg("events"), py::arg("counters"), py::arg("output_file"))
        .def("export_to_string",
             py::overload_cast<const std::vector<TraceEvent>&>(
                 &PerfettoExporter::exportToString),
             py::arg("events"))
        .def("export_to_string_with_counters",
             py::overload_cast<const std::vector<TraceEvent>&, const std::vector<CounterEvent>&>(
                 &PerfettoExporter::exportToString),
             py::arg("events"), py::arg("counters"))
        .def("set_enable_gpu_tracks", &PerfettoExporter::setEnableGPUTracks)
        .def("set_enable_flow_events", &PerfettoExporter::setEnableFlowEvents)
        .def("set_enable_counter_tracks", &PerfettoExporter::setEnableCounterTracks);
    
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
    
    // ========================================================================
    // Frame Capture (RenderDoc-inspired) - v0.5.0
    // ========================================================================
    
    // ResourceType enum
    py::enum_<ResourceType>(m, "ResourceType")
        .value("Unknown", ResourceType::Unknown)
        .value("Buffer", ResourceType::Buffer)
        .value("Texture1D", ResourceType::Texture1D)
        .value("Texture2D", ResourceType::Texture2D)
        .value("Texture3D", ResourceType::Texture3D)
        .value("TextureCube", ResourceType::TextureCube)
        .value("Sampler", ResourceType::Sampler)
        .value("Shader", ResourceType::Shader)
        .value("Pipeline", ResourceType::Pipeline)
        .value("DescriptorSet", ResourceType::DescriptorSet)
        .value("CommandBuffer", ResourceType::CommandBuffer)
        .value("QueryPool", ResourceType::QueryPool)
        .export_values();
    
    // CaptureState enum
    py::enum_<CaptureState>(m, "CaptureState")
        .value("Idle", CaptureState::Idle)
        .value("Armed", CaptureState::Armed)
        .value("Capturing", CaptureState::Capturing)
        .value("Processing", CaptureState::Processing)
        .value("Complete", CaptureState::Complete)
        .export_values();
    
    // ResourceState class
    py::class_<ResourceState>(m, "ResourceState")
        .def(py::init<>())
        .def_readwrite("resource_id", &ResourceState::resource_id)
        .def_readwrite("type", &ResourceState::type)
        .def_readwrite("name", &ResourceState::name)
        .def_readwrite("address", &ResourceState::address)
        .def_readwrite("size", &ResourceState::size)
        .def_readwrite("width", &ResourceState::width)
        .def_readwrite("height", &ResourceState::height)
        .def_readwrite("depth", &ResourceState::depth)
        .def_readwrite("format", &ResourceState::format)
        .def_readwrite("readable", &ResourceState::readable)
        .def_readwrite("writable", &ResourceState::writable)
        .def_readwrite("bound_as_input", &ResourceState::bound_as_input)
        .def_readwrite("bound_as_output", &ResourceState::bound_as_output)
        .def_readwrite("last_modified", &ResourceState::last_modified);
    
    // DrawCallInfo class
    py::class_<DrawCallInfo>(m, "DrawCallInfo")
        .def(py::init<>())
        .def_readwrite("call_id", &DrawCallInfo::call_id)
        .def_readwrite("name", &DrawCallInfo::name)
        .def_readwrite("timestamp", &DrawCallInfo::timestamp)
        .def_readwrite("vertex_count", &DrawCallInfo::vertex_count)
        .def_readwrite("instance_count", &DrawCallInfo::instance_count)
        .def_readwrite("index_count", &DrawCallInfo::index_count)
        .def_readwrite("group_count_x", &DrawCallInfo::group_count_x)
        .def_readwrite("group_count_y", &DrawCallInfo::group_count_y)
        .def_readwrite("group_count_z", &DrawCallInfo::group_count_z)
        .def_readwrite("pipeline_id", &DrawCallInfo::pipeline_id)
        .def_readwrite("vertex_shader", &DrawCallInfo::vertex_shader)
        .def_readwrite("fragment_shader", &DrawCallInfo::fragment_shader)
        .def_readwrite("compute_shader", &DrawCallInfo::compute_shader);
    
    // CapturedFrame class
    py::class_<CapturedFrame>(m, "CapturedFrame")
        .def(py::init<>())
        .def_readonly("frame_number", &CapturedFrame::frame_number)
        .def_readonly("start_time", &CapturedFrame::start_time)
        .def_readonly("end_time", &CapturedFrame::end_time)
        .def_readonly("events", &CapturedFrame::events)
        .def_readonly("draw_calls", &CapturedFrame::draw_calls)
        .def_readonly("total_draw_calls", &CapturedFrame::total_draw_calls)
        .def_readonly("total_dispatches", &CapturedFrame::total_dispatches)
        .def_readonly("total_memory_ops", &CapturedFrame::total_memory_ops)
        .def_readonly("total_sync_ops", &CapturedFrame::total_sync_ops)
        .def("duration", &CapturedFrame::duration)
        .def("get_resource_state_at", &CapturedFrame::getResourceStateAt,
             py::arg("resource_id"), py::arg("draw_call_id"));
    
    // FrameCaptureConfig class
    py::class_<FrameCaptureConfig>(m, "FrameCaptureConfig")
        .def(py::init<>())
        .def_readwrite("capture_on_keypress", &FrameCaptureConfig::capture_on_keypress)
        .def_readwrite("capture_after_present", &FrameCaptureConfig::capture_after_present)
        .def_readwrite("frames_to_capture", &FrameCaptureConfig::frames_to_capture)
        .def_readwrite("capture_api_calls", &FrameCaptureConfig::capture_api_calls)
        .def_readwrite("capture_resource_state", &FrameCaptureConfig::capture_resource_state)
        .def_readwrite("capture_buffer_contents", &FrameCaptureConfig::capture_buffer_contents)
        .def_readwrite("capture_texture_contents", &FrameCaptureConfig::capture_texture_contents)
        .def_readwrite("max_buffer_capture_size", &FrameCaptureConfig::max_buffer_capture_size)
        .def_readwrite("max_texture_capture_size", &FrameCaptureConfig::max_texture_capture_size);
    
    // FrameCapture class
    py::class_<FrameCapture>(m, "FrameCapture")
        .def(py::init<>())
        .def(py::init<const FrameCaptureConfig&>(), py::arg("config"))
        .def("set_config", &FrameCapture::setConfig, py::arg("config"))
        .def("get_config", &FrameCapture::getConfig, py::return_value_policy::reference)
        .def("trigger_capture", &FrameCapture::triggerCapture,
             "Trigger frame capture (like pressing F12 in RenderDoc)")
        .def("is_capturing", &FrameCapture::isCapturing)
        .def("get_state", &FrameCapture::getState)
        .def("on_frame_end", &FrameCapture::onFrameEnd,
             "Signal end of frame (call on Present/SwapBuffers)")
        .def("record_draw_call", &FrameCapture::recordDrawCall, py::arg("draw"))
        .def("record_dispatch", &FrameCapture::recordDispatch, py::arg("dispatch"))
        .def("record_resource_create", &FrameCapture::recordResourceCreate, py::arg("resource"))
        .def("record_event", &FrameCapture::recordEvent, py::arg("event"))
        .def("get_captured_frames", &FrameCapture::getCapturedFrames,
             py::return_value_policy::reference_internal)
        .def("get_frame", &FrameCapture::getFrame, py::arg("frame_number"),
             py::return_value_policy::reference_internal)
        .def("get_resource", &FrameCapture::getResource, py::arg("resource_id"),
             py::return_value_policy::reference_internal)
        .def("get_resources", &FrameCapture::getResources,
             py::return_value_policy::reference_internal)
        .def("replay_to_draw_call", &FrameCapture::replayToDrawCall,
             py::arg("frame_number"), py::arg("draw_call_id"))
        .def("export_to_perfetto", &FrameCapture::exportToPerfetto,
             py::arg("filename"), py::arg("frame_number"))
        .def("clear", &FrameCapture::clear);
    
    // ResourceTracker class
    py::class_<ResourceTracker>(m, "ResourceTracker")
        .def(py::init<>())
        .def("register_resource", &ResourceTracker::registerResource,
             py::arg("id"), py::arg("type"), py::arg("name") = "")
        .def("update_resource_binding", &ResourceTracker::updateResourceBinding,
             py::arg("id"), py::arg("address"), py::arg("size"))
        .def("mark_modified", &ResourceTracker::markModified,
             py::arg("id"), py::arg("when"))
        .def("destroy_resource", &ResourceTracker::destroyResource, py::arg("id"))
        .def("get_resource", &ResourceTracker::getResource, py::arg("id"),
             py::return_value_policy::reference_internal)
        .def("get_live_resources", &ResourceTracker::getLiveResources)
        .def("get_modified_since", &ResourceTracker::getModifiedSince, py::arg("since"));
    
    // Resource type to string helper
    m.def("resource_type_to_string", &resourceTypeToString,
          "Convert ResourceType to string");
    
    // ========================================================================
    // Memory Profiler - v0.6.0
    // ========================================================================
    
    // MemoryAllocation struct
    py::class_<MemoryAllocation>(m, "MemoryAllocation")
        .def(py::init<>())
        .def_readwrite("ptr", &MemoryAllocation::ptr)
        .def_readwrite("size", &MemoryAllocation::size)
        .def_readwrite("device_id", &MemoryAllocation::device_id)
        .def_readwrite("alloc_time", &MemoryAllocation::alloc_time)
        .def_readwrite("free_time", &MemoryAllocation::free_time)
        .def_readwrite("allocator", &MemoryAllocation::allocator)
        .def_readwrite("tag", &MemoryAllocation::tag)
        .def("is_live", &MemoryAllocation::is_live)
        .def("lifetime_ns", &MemoryAllocation::lifetime_ns);
    
    // MemorySnapshot struct
    py::class_<MemorySnapshot>(m, "MemorySnapshot")
        .def(py::init<>())
        .def_readwrite("timestamp", &MemorySnapshot::timestamp)
        .def_readwrite("total_allocated", &MemorySnapshot::total_allocated)
        .def_readwrite("total_freed", &MemorySnapshot::total_freed)
        .def_readwrite("live_allocations", &MemorySnapshot::live_allocations)
        .def_readwrite("live_bytes", &MemorySnapshot::live_bytes)
        .def_readwrite("peak_bytes", &MemorySnapshot::peak_bytes)
        .def_readwrite("device_usage", &MemorySnapshot::device_usage)
        .def_readwrite("allocator_usage", &MemorySnapshot::allocator_usage);
    
    // MemoryLeak struct
    py::class_<MemoryLeak>(m, "MemoryLeak")
        .def(py::init<>())
        .def_readwrite("ptr", &MemoryLeak::ptr)
        .def_readwrite("size", &MemoryLeak::size)
        .def_readwrite("alloc_time", &MemoryLeak::alloc_time)
        .def_readwrite("allocator", &MemoryLeak::allocator)
        .def_readwrite("tag", &MemoryLeak::tag)
        .def_readwrite("lifetime_ns", &MemoryLeak::lifetime_ns);
    
    // MemoryReport struct
    py::class_<MemoryReport>(m, "MemoryReport")
        .def(py::init<>())
        .def_readonly("total_allocations", &MemoryReport::total_allocations)
        .def_readonly("total_frees", &MemoryReport::total_frees)
        .def_readonly("total_bytes_allocated", &MemoryReport::total_bytes_allocated)
        .def_readonly("total_bytes_freed", &MemoryReport::total_bytes_freed)
        .def_readonly("peak_memory_usage", &MemoryReport::peak_memory_usage)
        .def_readonly("current_memory_usage", &MemoryReport::current_memory_usage)
        .def_readonly("profile_duration_ns", &MemoryReport::profile_duration_ns)
        .def_readonly("min_allocation_size", &MemoryReport::min_allocation_size)
        .def_readonly("max_allocation_size", &MemoryReport::max_allocation_size)
        .def_readonly("avg_allocation_size", &MemoryReport::avg_allocation_size)
        .def_readonly("potential_leaks", &MemoryReport::potential_leaks)
        .def_readonly("timeline", &MemoryReport::timeline)
        .def("summary", &MemoryReport::summary)
        .def("to_json", &MemoryReport::toJSON);
    
    // MemoryProfiler::Config
    py::class_<MemoryProfiler::Config>(m, "MemoryProfilerConfig")
        .def(py::init<>())
        .def_readwrite("snapshot_interval_ms", &MemoryProfiler::Config::snapshot_interval_ms)
        .def_readwrite("leak_threshold_ns", &MemoryProfiler::Config::leak_threshold_ns)
        .def_readwrite("track_call_stacks", &MemoryProfiler::Config::track_call_stacks)
        .def_readwrite("detect_double_free", &MemoryProfiler::Config::detect_double_free)
        .def_readwrite("max_timeline_samples", &MemoryProfiler::Config::max_timeline_samples);
    
    // MemoryProfiler class
    py::class_<MemoryProfiler>(m, "MemoryProfiler")
        .def(py::init<>())
        .def(py::init<const MemoryProfiler::Config&>(), py::arg("config"))
        .def("start", &MemoryProfiler::start)
        .def("stop", &MemoryProfiler::stop)
        .def("is_active", &MemoryProfiler::isActive)
        .def("record_alloc", &MemoryProfiler::recordAlloc,
             py::arg("ptr"), py::arg("size"), py::arg("device_id") = 0,
             py::arg("allocator") = "default", py::arg("tag") = "")
        .def("record_free", &MemoryProfiler::recordFree,
             py::arg("ptr"), py::arg("device_id") = 0)
        .def("record_event", &MemoryProfiler::recordEvent, py::arg("event"))
        .def("get_current_usage", &MemoryProfiler::getCurrentUsage)
        .def("get_peak_usage", &MemoryProfiler::getPeakUsage)
        .def("get_live_allocation_count", &MemoryProfiler::getLiveAllocationCount)
        .def("get_live_allocations", &MemoryProfiler::getLiveAllocations)
        .def("take_snapshot", &MemoryProfiler::takeSnapshot)
        .def("generate_report", &MemoryProfiler::generateReport)
        .def("clear", &MemoryProfiler::clear)
        .def("to_counter_events", &MemoryProfiler::toCounterEvents)
        .def("to_memory_events", &MemoryProfiler::toMemoryEvents);
    
    // Utility functions
    m.def("format_bytes", &formatBytes, "Format bytes to human-readable string");
    m.def("format_duration", &formatDuration, "Format nanoseconds to human-readable string");
    
    // ========================================================================
    // XRay Importer - v0.4.0
    // ========================================================================
    
    // XRayEntryType enum
    py::enum_<XRayEntryType>(m, "XRayEntryType")
        .value("FunctionEnter", XRayEntryType::FunctionEnter)
        .value("FunctionExit", XRayEntryType::FunctionExit)
        .value("TailExit", XRayEntryType::TailExit)
        .value("CustomEvent", XRayEntryType::CustomEvent)
        .value("TypedEvent", XRayEntryType::TypedEvent)
        .export_values();
    
    // XRayFileHeader
    py::class_<XRayFileHeader>(m, "XRayFileHeader")
        .def(py::init<>())
        .def_readonly("version", &XRayFileHeader::version)
        .def_readonly("type", &XRayFileHeader::type)
        .def_readonly("cycle_frequency", &XRayFileHeader::cycle_frequency)
        .def_readonly("num_records", &XRayFileHeader::num_records);
    
    // XRayImporter::Config
    py::class_<XRayImporter::Config>(m, "XRayImporterConfig")
        .def(py::init<>())
        .def_readwrite("resolve_symbols", &XRayImporter::Config::resolve_symbols)
        .def_readwrite("include_custom_events", &XRayImporter::Config::include_custom_events)
        .def_readwrite("filter_short_calls", &XRayImporter::Config::filter_short_calls)
        .def_readwrite("min_duration_ns", &XRayImporter::Config::min_duration_ns)
        .def_readwrite("symbol_file", &XRayImporter::Config::symbol_file);
    
    // XRayImporter::Statistics
    py::class_<XRayImporter::Statistics>(m, "XRayStatistics")
        .def(py::init<>())
        .def_readonly("records_read", &XRayImporter::Statistics::records_read)
        .def_readonly("records_converted", &XRayImporter::Statistics::records_converted)
        .def_readonly("records_filtered", &XRayImporter::Statistics::records_filtered)
        .def_readonly("custom_events", &XRayImporter::Statistics::custom_events)
        .def_readonly("functions_identified", &XRayImporter::Statistics::functions_identified)
        .def_readonly("total_duration_ms", &XRayImporter::Statistics::total_duration_ms);
    
    // XRayFunctionRecord
    py::class_<XRayFunctionRecord>(m, "XRayFunctionRecord")
        .def(py::init<>())
        .def_readwrite("function_id", &XRayFunctionRecord::function_id)
        .def_readwrite("timestamp", &XRayFunctionRecord::timestamp)
        .def_readwrite("type", &XRayFunctionRecord::type)
        .def_readwrite("thread_id", &XRayFunctionRecord::thread_id)
        .def_readwrite("cpu_id", &XRayFunctionRecord::cpu_id)
        .def_readwrite("function_name", &XRayFunctionRecord::function_name)
        .def_readwrite("file_name", &XRayFunctionRecord::file_name)
        .def_readwrite("line_number", &XRayFunctionRecord::line_number);
    
    // XRayImporter class
    py::class_<XRayImporter>(m, "XRayImporter")
        .def(py::init<>())
        .def(py::init<const XRayImporter::Config&>(), py::arg("config"))
        .def("import_file", &XRayImporter::importFile, py::arg("filename"),
             "Import XRay log file and return TraceEvents")
        .def("import_buffer", &XRayImporter::importBuffer,
             py::arg("data"), py::arg("size"))
        .def("get_raw_records", &XRayImporter::getRawRecords,
             py::return_value_policy::reference_internal)
        .def("get_statistics", &XRayImporter::getStatistics,
             py::return_value_policy::reference_internal)
        .def("get_header", &XRayImporter::getHeader,
             py::return_value_policy::reference_internal)
        .def("set_symbol_file", &XRayImporter::setSymbolFile, py::arg("path"))
        .def("set_config", &XRayImporter::setConfig, py::arg("config"))
        .def_static("is_available", &XRayImporter::isAvailable);
    
    // ========================================================================
    // BPF Types - v0.4.0
    // ========================================================================
    
    // BPFEventType enum
    py::enum_<BPFEventType>(m, "BPFEventType")
        .value("Unknown", BPFEventType::Unknown)
        .value("CudaLaunchKernel", BPFEventType::CudaLaunchKernel)
        .value("CudaMemcpy", BPFEventType::CudaMemcpy)
        .value("CudaMalloc", BPFEventType::CudaMalloc)
        .value("CudaFree", BPFEventType::CudaFree)
        .value("CudaSynchronize", BPFEventType::CudaSynchronize)
        .value("UvmFault", BPFEventType::UvmFault)
        .value("UvmMigrate", BPFEventType::UvmMigrate)
        .value("HipLaunchKernel", BPFEventType::HipLaunchKernel)
        .value("HipMemcpy", BPFEventType::HipMemcpy)
        .export_values();
    
    // BPFEventRecord struct
    py::class_<BPFEventRecord>(m, "BPFEventRecord")
        .def(py::init<>())
        .def_readwrite("timestamp_ns", &BPFEventRecord::timestamp_ns)
        .def_readwrite("pid", &BPFEventRecord::pid)
        .def_readwrite("tid", &BPFEventRecord::tid)
        .def_readwrite("cpu", &BPFEventRecord::cpu)
        .def_readwrite("type", &BPFEventRecord::type);
    
    // BPFTracer class
    py::class_<BPFTracer>(m, "BPFTracer")
        .def(py::init<>())
        .def("load_program", &BPFTracer::loadProgram, py::arg("path"))
        .def("attach", &BPFTracer::attach, py::arg("pattern"))
        .def("detach", &BPFTracer::detach)
        .def("start", &BPFTracer::start)
        .def("stop", &BPFTracer::stop)
        .def("poll_events", &BPFTracer::pollEvents, py::arg("max_events") = 1000)
        .def("get_statistics", &BPFTracer::getStatistics)
        .def_static("is_available", &BPFTracer::isAvailable,
                   "Check if BPF is available (Linux only)")
        .def_static("get_gpu_tracepoints", &BPFTracer::getGPUTracepoints);
    
    m.def("bpf_event_type_to_string", &bpfEventTypeToString,
          "Convert BPFEventType to string");
    m.def("bpf_event_to_trace_event", &bpfEventToTraceEvent,
          "Convert BPFEventRecord to TraceEvent");
    
    // Helper functions
    m.def("get_current_timestamp", &getCurrentTimestamp,
          "Get current timestamp in nanoseconds");
    
    m.def("event_type_to_string", &eventTypeToString,
          "Convert EventType to string");
    
    m.def("create_profiler", [](PlatformType type) {
        return createProfiler(type);
    }, py::arg("platform") = PlatformType::Unknown,
    "Create a profiler for the specified platform (CUDA, ROCm, Metal, or auto-detect with Unknown)");
    
    // Platform detection functions
    m.def("is_cuda_available", &isCUDAAvailable,
          "Check if CUDA/CUPTI is available on this system");
    
    m.def("get_cuda_device_count", &getCUDADeviceCount,
          "Get number of CUDA-capable devices");
    
    m.def("get_cuda_driver_version", &getCUDADriverVersion,
          "Get CUDA driver version");
    
    m.def("detect_platform", &detectPlatform,
          "Auto-detect the best available GPU platform");
}
