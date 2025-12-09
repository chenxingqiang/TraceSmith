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
#include <sstream>

// Common
#include "tracesmith/common/types.hpp"

// Capture
#include "tracesmith/capture/profiler.hpp"
#include "tracesmith/capture/cupti_profiler.hpp"
#include "tracesmith/capture/memory_profiler.hpp"
#include "tracesmith/capture/bpf_types.hpp"

// Format
#include "tracesmith/format/sbt_format.hpp"

// State
#include "tracesmith/state/timeline_builder.hpp"
#include "tracesmith/state/timeline_viewer.hpp"
#include "tracesmith/state/perfetto_exporter.hpp"
#include "tracesmith/state/perfetto_proto_exporter.hpp"

// Replay
#include "tracesmith/replay/replay_engine.hpp"
#include "tracesmith/replay/frame_capture.hpp"

// Common (additional)
#include "tracesmith/common/xray_importer.hpp"
#include "tracesmith/state/gpu_state_machine.hpp"
#include "tracesmith/state/instruction_stream.hpp"
#include "tracesmith/common/stack_capture.hpp"
#include "tracesmith/common/ring_buffer.hpp"

// Cluster (v0.7.0)
#include "tracesmith/cluster/gpu_topology.hpp"
#include "tracesmith/cluster/multi_gpu_profiler.hpp"
#include "tracesmith/cluster/time_sync.hpp"
#include "tracesmith/cluster/nccl_tracker.hpp"

namespace py = pybind11;
using namespace tracesmith;

PYBIND11_MODULE(_tracesmith, m) {
    m.doc() = "TraceSmith GPU Profiling & Replay System";
    
    // Version info
    m.attr("__version__") = "0.8.3";
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
    
    // StackFrame struct
    py::class_<StackFrame>(m, "StackFrame")
        .def(py::init<>())
        .def(py::init<uint64_t>())
        .def_readwrite("address", &StackFrame::address)
        .def_readwrite("function_name", &StackFrame::function_name)
        .def_readwrite("file_name", &StackFrame::file_name)
        .def_readwrite("line_number", &StackFrame::line_number)
        .def("__repr__", [](const StackFrame& f) {
            return "<StackFrame 0x" + 
                   ([](uint64_t v) { 
                       std::ostringstream os; 
                       os << std::hex << v; 
                       return os.str(); 
                   })(f.address) + 
                   " " + (f.function_name.empty() ? "<unknown>" : f.function_name) + ">";
        });
    
    // CallStack struct
    py::class_<CallStack>(m, "CallStack")
        .def(py::init<>())
        .def_readwrite("frames", &CallStack::frames)
        .def_readwrite("thread_id", &CallStack::thread_id)
        .def("empty", &CallStack::empty)
        .def("depth", &CallStack::depth)
        .def("__len__", &CallStack::depth)
        .def("__repr__", [](const CallStack& cs) {
            return "<CallStack thread=" + std::to_string(cs.thread_id) + 
                   " frames=" + std::to_string(cs.frames.size()) + ">";
        });
    
    // StackCaptureConfig struct
    py::class_<StackCaptureConfig>(m, "StackCaptureConfig")
        .def(py::init<>())
        .def_readwrite("max_depth", &StackCaptureConfig::max_depth)
        .def_readwrite("skip_frames", &StackCaptureConfig::skip_frames)
        .def_readwrite("async_signal_safe", &StackCaptureConfig::async_signal_safe)
        .def_readwrite("resolve_symbols", &StackCaptureConfig::resolve_symbols)
        .def_readwrite("demangle", &StackCaptureConfig::demangle);
    
    // StackCapture class
    py::class_<StackCapture>(m, "StackCapture")
        .def(py::init<const StackCaptureConfig&>(), py::arg("config") = StackCaptureConfig())
        .def("capture", [](StackCapture& self) {
            CallStack cs;
            self.capture(cs);
            return cs;
        }, "Capture the current call stack")
        .def("capture_with_thread_id", [](StackCapture& self, uint64_t thread_id) {
            CallStack cs;
            self.captureWithThreadId(thread_id, cs);
            return cs;
        }, py::arg("thread_id"), "Capture call stack with a specific thread ID")
        .def("resolve_symbols", &StackCapture::resolveSymbols,
             "Resolve symbols for captured addresses")
        .def_static("get_current_thread_id", &StackCapture::getCurrentThreadId,
                    "Get the current thread ID")
        .def_static("is_available", &StackCapture::isAvailable,
                    "Check if stack capture is available on this platform");
    
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
        // Call stack (v0.7.0)
        .def_property("call_stack",
            [](const TraceEvent& e) -> py::object {
                if (e.call_stack.has_value()) {
                    return py::cast(e.call_stack.value());
                }
                return py::none();
            },
            [](TraceEvent& e, const CallStack& stack) {
                e.call_stack = stack;
            })
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
        .def_readwrite("compute_major", &DeviceInfo::compute_major)
        .def_readwrite("compute_minor", &DeviceInfo::compute_minor)
        .def_readwrite("total_memory", &DeviceInfo::total_memory)
        .def_readwrite("memory_clock_rate", &DeviceInfo::memory_clock_rate)
        .def_readwrite("memory_bus_width", &DeviceInfo::memory_bus_width)
        .def_readwrite("multiprocessor_count", &DeviceInfo::multiprocessor_count)
        .def_readwrite("max_threads_per_mp", &DeviceInfo::max_threads_per_mp)
        .def_readwrite("clock_rate", &DeviceInfo::clock_rate)
        .def_readwrite("warp_size", &DeviceInfo::warp_size);
    
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
        .def_readwrite("end_time", &TraceMetadata::end_time)
        .def_readwrite("hostname", &TraceMetadata::hostname)
        .def_readwrite("process_id", &TraceMetadata::process_id)
        .def_readwrite("devices", &TraceMetadata::devices);
    
    // PlatformType enum
    py::enum_<PlatformType>(m, "PlatformType")
        .value("Unknown", PlatformType::Unknown)
        .value("CUDA", PlatformType::CUDA)
        .value("ROCm", PlatformType::ROCm)
        .value("Metal", PlatformType::Metal)
        .value("MACA", PlatformType::MACA)
        .export_values();
    
    // Platform type to string helper
    m.def("platform_type_to_string", &platformTypeToString, 
          "Convert PlatformType to human-readable string");
    
    // OverflowPolicy enum
    py::enum_<OverflowPolicy>(m, "OverflowPolicy")
        .value("DropOldest", OverflowPolicy::DropOldest)
        .value("DropNewest", OverflowPolicy::DropNewest)
        .value("Block", OverflowPolicy::Block)
        .export_values();
    
    // ProfilerConfig class
    py::class_<ProfilerConfig>(m, "ProfilerConfig",
        "Configuration options for GPU profilers")
        .def(py::init<>())
        .def_readwrite("buffer_size", &ProfilerConfig::buffer_size,
                       "Ring buffer size (number of events)")
        .def_readwrite("overflow_policy", &ProfilerConfig::overflow_policy,
                       "Policy when buffer overflows")
        .def_readwrite("capture_callstacks", &ProfilerConfig::capture_callstacks,
                       "Whether to capture call stacks")
        .def_readwrite("callstack_depth", &ProfilerConfig::callstack_depth,
                       "Maximum call stack depth")
        .def_readwrite("capture_kernel_params", &ProfilerConfig::capture_kernel_params,
                       "Whether to capture kernel parameters")
        .def_readwrite("capture_memory_params", &ProfilerConfig::capture_memory_params,
                       "Whether to capture memory operation parameters")
        .def_readwrite("capture_kernels", &ProfilerConfig::capture_kernels,
                       "Whether to capture kernel events")
        .def_readwrite("capture_memcpy", &ProfilerConfig::capture_memcpy,
                       "Whether to capture memory copy events")
        .def_readwrite("capture_memset", &ProfilerConfig::capture_memset,
                       "Whether to capture memory set events")
        .def_readwrite("capture_sync", &ProfilerConfig::capture_sync,
                       "Whether to capture synchronization events")
        .def_readwrite("capture_alloc", &ProfilerConfig::capture_alloc,
                       "Whether to capture allocation events");
    
    // SBTResult struct
    py::class_<SBTResult>(m, "SBTResult",
        "Result type for SBT operations")
        .def(py::init<>())
        .def(py::init<bool>())
        .def(py::init<const std::string&>())
        .def_readwrite("success", &SBTResult::success)
        .def_readwrite("error_message", &SBTResult::error_message)
        .def("__bool__", [](const SBTResult& r) { return r.success; });
    
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
            auto result = r.readAll(record);
            if (!result.success) {
                throw std::runtime_error(result.error_message);
            }
            return record.events();
        }, "Read all events from the SBT file")
        .def("read_metadata", [](SBTReader& r) {
            TraceMetadata metadata;
            auto result = r.readMetadata(metadata);
            if (!result.success) {
                return py::make_tuple(SBTResult(false), metadata);
            }
            return py::make_tuple(result, metadata);
        }, "Read metadata from the SBT file")
        .def("read_events", [](SBTReader& r, size_t offset, size_t count) {
            std::vector<TraceEvent> events;
            auto result = r.readEvents(events, offset, count);
            if (!result.success) {
                throw std::runtime_error(result.error_message);
            }
            return events;
        }, py::arg("offset") = 0, py::arg("count") = 0,
           "Read events from the SBT file with pagination");
    
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
        .def_readwrite("validate_timing", &ReplayConfig::validate_timing)
        .def_readwrite("compute_checksums", &ReplayConfig::compute_checksums)
        .def_readwrite("verbose", &ReplayConfig::verbose)
        .def_readwrite("pause_on_error", &ReplayConfig::pause_on_error)
        .def_readwrite("time_scale", &ReplayConfig::time_scale)
        .def_readwrite("stream_id", &ReplayConfig::stream_id);
    
    // ReplayResult class
    py::class_<ReplayResult>(m, "ReplayResult")
        .def(py::init<>())
        .def_readwrite("success", &ReplayResult::success)
        .def_readwrite("deterministic", &ReplayResult::deterministic)
        .def_readwrite("operations_total", &ReplayResult::operations_total)
        .def_readwrite("operations_executed", &ReplayResult::operations_executed)
        .def_readwrite("operations_skipped", &ReplayResult::operations_skipped)
        .def_readwrite("operations_failed", &ReplayResult::operations_failed)
        .def_readwrite("replay_duration", &ReplayResult::replay_duration)
        .def_readwrite("original_duration", &ReplayResult::original_duration)
        .def_readwrite("order_violations", &ReplayResult::order_violations)
        .def_readwrite("dependency_violations", &ReplayResult::dependency_violations)
        .def_readwrite("timing_violations", &ReplayResult::timing_violations)
        .def_readwrite("errors", &ReplayResult::errors)
        .def_readwrite("warnings", &ReplayResult::warnings)
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
        .def_readwrite("first_vertex", &DrawCallInfo::first_vertex)
        .def_readwrite("first_instance", &DrawCallInfo::first_instance)
        .def_readwrite("index_count", &DrawCallInfo::index_count)
        .def_readwrite("first_index", &DrawCallInfo::first_index)
        .def_readwrite("vertex_offset", &DrawCallInfo::vertex_offset)
        .def_readwrite("group_count_x", &DrawCallInfo::group_count_x)
        .def_readwrite("group_count_y", &DrawCallInfo::group_count_y)
        .def_readwrite("group_count_z", &DrawCallInfo::group_count_z)
        .def_readwrite("input_resources", &DrawCallInfo::input_resources)
        .def_readwrite("output_resources", &DrawCallInfo::output_resources)
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
        .def_property_readonly("state", &FrameCapture::getState,
             "Current capture state")
        .def_property_readonly("captured_frames", &FrameCapture::getCapturedFrames,
             py::return_value_policy::reference_internal)
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
        .def("detect_leaks", &MemoryProfiler::detectLeaks)
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
    
    // ========================================================================
    // IPlatformProfiler binding
    // ========================================================================
    
    py::class_<IPlatformProfiler, std::shared_ptr<IPlatformProfiler>>(m, "IPlatformProfiler",
        "Abstract interface for GPU profilers")
        .def("platform_type", &IPlatformProfiler::platformType,
             "Get the platform type")
        .def("is_available", &IPlatformProfiler::isAvailable,
             "Check if the platform is available on this system")
        .def("initialize", &IPlatformProfiler::initialize,
             py::arg("config"),
             "Initialize the profiler with configuration")
        .def("finalize", &IPlatformProfiler::finalize,
             "Finalize and cleanup the profiler")
        .def("start_capture", &IPlatformProfiler::startCapture,
             "Start capturing events")
        .def("stop_capture", &IPlatformProfiler::stopCapture,
             "Stop capturing events")
        .def("is_capturing", &IPlatformProfiler::isCapturing,
             "Check if currently capturing")
        .def("get_events", [](IPlatformProfiler& self, size_t max_count) {
            std::vector<TraceEvent> events;
            self.getEvents(events, max_count);
            return events;
        }, py::arg("max_count") = 0,
           "Get captured events (drains the internal buffer)")
        .def("get_device_info", &IPlatformProfiler::getDeviceInfo,
             "Get device information")
        .def("events_captured", &IPlatformProfiler::eventsCaptured,
             "Get number of events captured")
        .def("events_dropped", &IPlatformProfiler::eventsDropped,
             "Get number of events dropped");
    
    m.def("create_profiler", [](PlatformType type) -> std::shared_ptr<IPlatformProfiler> {
        return createProfiler(type);
    }, py::arg("platform") = PlatformType::Unknown,
    "Create a profiler for the specified platform (CUDA, ROCm, Metal, MACA, or auto-detect with Unknown)");
    
    // Platform detection functions
    m.def("is_cuda_available", &isCUDAAvailable,
          "Check if CUDA/CUPTI is available on this system");
    
    m.def("get_cuda_device_count", &getCUDADeviceCount,
          "Get number of CUDA-capable devices");
    
    m.def("get_cuda_driver_version", &getCUDADriverVersion,
          "Get CUDA driver version");
    
    m.def("is_metal_available", &isMetalAvailable,
          "Check if Metal is available on this system (macOS only)");

    m.def("get_metal_device_count", &getMetalDeviceCount,
          "Get number of Metal-capable devices");

    // MetaX MACA functions
    m.def("is_maca_available", &isMACAAvailable,
          "Check if MetaX MACA is available on this system");

    m.def("get_maca_driver_version", &getMACADriverVersion,
          "Get MetaX MACA driver version");

    m.def("get_maca_device_count", &getMACADeviceCount,
          "Get number of MetaX GPU devices");
    
    m.def("detect_platform", &detectPlatform,
          "Auto-detect the best available GPU platform");
    
    // ============================================================================
    // State Module Bindings (v0.6.6)
    // ============================================================================
    
    // GPUState enum
    py::enum_<GPUState>(m, "GPUState")
        .value("Idle", GPUState::Idle)
        .value("Queued", GPUState::Queued)
        .value("Running", GPUState::Running)
        .value("Waiting", GPUState::Waiting)
        .value("Complete", GPUState::Complete)
        .export_values();
    
    // StateTransition struct
    py::class_<StateTransition>(m, "StateTransition")
        .def(py::init<>())
        .def_readwrite("from_state", &StateTransition::from)
        .def_readwrite("to_state", &StateTransition::to)
        .def_readwrite("when", &StateTransition::when)
        .def_readwrite("correlation_id", &StateTransition::correlation_id)
        .def_readwrite("reason", &StateTransition::reason);
    
    // GPUStreamState class
    py::class_<GPUStreamState>(m, "GPUStreamState")
        .def(py::init<uint32_t, uint32_t>(),
             py::arg("stream_id"), py::arg("device_id") = 0)
        .def("current_state", &GPUStreamState::currentState)
        .def("transition_to", &GPUStreamState::transitionTo,
             py::arg("new_state"), py::arg("when"),
             py::arg("correlation_id") = 0, py::arg("reason") = "")
        .def("process_event", &GPUStreamState::processEvent)
        .def("state_at", &GPUStreamState::stateAt)
        .def("transitions", &GPUStreamState::transitions)
        .def("time_in_states", &GPUStreamState::timeInStates)
        .def("utilization", &GPUStreamState::utilization)
        .def("stream_id", &GPUStreamState::streamId)
        .def("device_id", &GPUStreamState::deviceId)
        .def("reset", &GPUStreamState::reset);
    
    // GPUStateMachine::Statistics
    py::class_<GPUStateMachine::Statistics>(m, "GPUStateMachineStatistics")
        .def(py::init<>())
        .def_readwrite("total_events", &GPUStateMachine::Statistics::total_events)
        .def_readwrite("total_transitions", &GPUStateMachine::Statistics::total_transitions)
        .def_readwrite("transitions_per_stream", &GPUStateMachine::Statistics::transitions_per_stream)
        .def_readwrite("total_time_per_state", &GPUStateMachine::Statistics::total_time_per_state)
        .def_readwrite("overall_utilization", &GPUStateMachine::Statistics::overall_utilization);
    
    // GPUStateMachine::StateHistory
    py::class_<GPUStateMachine::StateHistory>(m, "GPUStateHistory")
        .def(py::init<>())
        .def_readwrite("device_id", &GPUStateMachine::StateHistory::device_id)
        .def_readwrite("stream_id", &GPUStateMachine::StateHistory::stream_id)
        .def_readwrite("transitions", &GPUStateMachine::StateHistory::transitions);
    
    // GPUStateMachine class
    py::class_<GPUStateMachine>(m, "GPUStateMachine")
        .def(py::init<>())
        .def("process_event", &GPUStateMachine::processEvent)
        .def("process_events", &GPUStateMachine::processEvents)
        .def("get_stream_state", 
             static_cast<GPUStreamState* (GPUStateMachine::*)(uint32_t, uint32_t)>(&GPUStateMachine::getStreamState),
             py::return_value_policy::reference_internal,
             py::arg("device_id"), py::arg("stream_id"))
        .def("get_all_streams", &GPUStateMachine::getAllStreams)
        .def("get_statistics", &GPUStateMachine::getStatistics)
        .def("export_history", &GPUStateMachine::exportHistory)
        .def("reset", &GPUStateMachine::reset);
    
    // DependencyType enum
    py::enum_<DependencyType>(m, "DependencyType")
        .value("None_", DependencyType::None)
        .value("Sequential", DependencyType::Sequential)
        .value("Synchronization", DependencyType::Synchronization)
        .value("HostBarrier", DependencyType::HostBarrier)
        .value("Memory", DependencyType::Memory)
        .export_values();
    
    // OperationDependency struct
    py::class_<OperationDependency>(m, "OperationDependency")
        .def(py::init<uint64_t, uint64_t, DependencyType, const std::string&>(),
             py::arg("from_id"), py::arg("to_id"), py::arg("type"), py::arg("desc") = "")
        .def_readwrite("from_correlation_id", &OperationDependency::from_correlation_id)
        .def_readwrite("to_correlation_id", &OperationDependency::to_correlation_id)
        .def_readwrite("type", &OperationDependency::type)
        .def_readwrite("description", &OperationDependency::description);
    
    // InstructionNode struct
    py::class_<InstructionNode>(m, "InstructionNode")
        .def(py::init<>())
        .def(py::init<const TraceEvent&>())
        .def_readwrite("event", &InstructionNode::event)
        .def_readwrite("dependencies", &InstructionNode::dependencies)
        .def_readwrite("dependents", &InstructionNode::dependents);
    
    // InstructionStreamBuilder::Statistics
    py::class_<InstructionStreamBuilder::Statistics>(m, "InstructionStreamStatistics")
        .def(py::init<>())
        .def_readwrite("total_operations", &InstructionStreamBuilder::Statistics::total_operations)
        .def_readwrite("kernel_launches", &InstructionStreamBuilder::Statistics::kernel_launches)
        .def_readwrite("memory_operations", &InstructionStreamBuilder::Statistics::memory_operations)
        .def_readwrite("synchronizations", &InstructionStreamBuilder::Statistics::synchronizations)
        .def_readwrite("total_dependencies", &InstructionStreamBuilder::Statistics::total_dependencies)
        .def_readwrite("operations_per_stream", &InstructionStreamBuilder::Statistics::operations_per_stream);
    
    // InstructionStreamBuilder class
    py::class_<InstructionStreamBuilder>(m, "InstructionStreamBuilder")
        .def(py::init<>())
        .def("add_event", &InstructionStreamBuilder::addEvent)
        .def("add_events", &InstructionStreamBuilder::addEvents)
        .def("analyze", &InstructionStreamBuilder::analyze)
        .def("get_execution_order", &InstructionStreamBuilder::getExecutionOrder)
        .def("get_dependencies", &InstructionStreamBuilder::getDependencies)
        .def("get_stream_operations", &InstructionStreamBuilder::getStreamOperations)
        .def("has_dependency", &InstructionStreamBuilder::hasDependency)
        .def("get_statistics", &InstructionStreamBuilder::getStatistics)
        .def("export_to_dot", &InstructionStreamBuilder::exportToDot)
        .def("clear", &InstructionStreamBuilder::clear);
    
    // TimelineViewer::ViewConfig
    py::class_<TimelineViewer::ViewConfig>(m, "TimelineViewConfig")
        .def(py::init<>())
        .def_readwrite("width", &TimelineViewer::ViewConfig::width)
        .def_readwrite("max_rows", &TimelineViewer::ViewConfig::max_rows)
        .def_readwrite("show_timestamps", &TimelineViewer::ViewConfig::show_timestamps)
        .def_readwrite("show_duration", &TimelineViewer::ViewConfig::show_duration)
        .def_readwrite("fill_char", &TimelineViewer::ViewConfig::fill_char);
    
    // TimelineViewer class
    py::class_<TimelineViewer>(m, "TimelineViewer")
        .def(py::init<>())
        .def(py::init<const TimelineViewer::ViewConfig&>())
        .def("render", &TimelineViewer::render)
        .def("render_stream", &TimelineViewer::renderStream)
        .def("render_stats", &TimelineViewer::renderStats);
    
    // =========================================================================
    // Cluster Module - Multi-GPU Profiling (v0.7.0)
    // =========================================================================
    
    // GPULinkType enum
    py::enum_<cluster::GPULinkType>(m, "GPULinkType")
        .value("None_", cluster::GPULinkType::None)
        .value("PCIe", cluster::GPULinkType::PCIe)
        .value("NVLink1", cluster::GPULinkType::NVLink1)
        .value("NVLink2", cluster::GPULinkType::NVLink2)
        .value("NVLink3", cluster::GPULinkType::NVLink3)
        .value("NVLink4", cluster::GPULinkType::NVLink4)
        .value("NVSwitch", cluster::GPULinkType::NVSwitch)
        .export_values();
    
    // GPULink struct
    py::class_<cluster::GPULink>(m, "GPULink")
        .def(py::init<>())
        .def_readwrite("gpu_a", &cluster::GPULink::gpu_a)
        .def_readwrite("gpu_b", &cluster::GPULink::gpu_b)
        .def_readwrite("type", &cluster::GPULink::type)
        .def_readwrite("link_count", &cluster::GPULink::link_count)
        .def_readwrite("bandwidth_gbps", &cluster::GPULink::bandwidth_gbps)
        .def_readwrite("measured_bandwidth", &cluster::GPULink::measured_bandwidth)
        .def_readwrite("bidirectional", &cluster::GPULink::bidirectional);
    
    // GPUDeviceTopology struct
    py::class_<cluster::GPUDeviceTopology>(m, "GPUDeviceTopology")
        .def(py::init<>())
        .def_readwrite("gpu_id", &cluster::GPUDeviceTopology::gpu_id)
        .def_readwrite("name", &cluster::GPUDeviceTopology::name)
        .def_readwrite("pci_bus_id", &cluster::GPUDeviceTopology::pci_bus_id)
        .def_readwrite("numa_node", &cluster::GPUDeviceTopology::numa_node)
        .def_readwrite("has_nvlink", &cluster::GPUDeviceTopology::has_nvlink)
        .def_readwrite("nvlink_count", &cluster::GPUDeviceTopology::nvlink_count);
    
    // GPUTopologyInfo struct
    py::class_<cluster::GPUTopologyInfo>(m, "GPUTopologyInfo")
        .def(py::init<>())
        .def_readwrite("gpu_count", &cluster::GPUTopologyInfo::gpu_count)
        .def_readwrite("has_nvswitch", &cluster::GPUTopologyInfo::has_nvswitch)
        .def_readwrite("devices", &cluster::GPUTopologyInfo::devices)
        .def_readwrite("links", &cluster::GPUTopologyInfo::links);
    
    // GPUTopology class
    py::class_<cluster::GPUTopology>(m, "GPUTopology")
        .def(py::init<>())
        .def("discover", &cluster::GPUTopology::discover)
        .def("is_discovered", &cluster::GPUTopology::isDiscovered)
        .def("get_topology", &cluster::GPUTopology::getTopology)
        .def("get_gpu_count", &cluster::GPUTopology::getGPUCount)
        .def("get_link_type", &cluster::GPUTopology::getLinkType)
        .def("get_bandwidth", &cluster::GPUTopology::getBandwidth)
        .def("can_access_peer", &cluster::GPUTopology::canAccessPeer)
        .def("get_nvlink_count", &cluster::GPUTopology::getNVLinkCount)
        .def("is_directly_connected", &cluster::GPUTopology::isDirectlyConnected)
        .def("get_connected_gpus", &cluster::GPUTopology::getConnectedGPUs)
        .def("get_device_info", &cluster::GPUTopology::getDeviceInfo)
        .def("get_optimal_path", &cluster::GPUTopology::getOptimalPath)
        .def("estimate_transfer_time", &cluster::GPUTopology::estimateTransferTime)
        .def("to_ascii", &cluster::GPUTopology::toASCII)
        .def("to_graphviz", &cluster::GPUTopology::toGraphviz)
        .def("to_json", &cluster::GPUTopology::toJSON)
        .def("print_summary", &cluster::GPUTopology::printSummary);
    
    // Utility functions
    m.def("is_nvml_available", &cluster::isNVMLAvailable);
    m.def("get_nvml_version", &cluster::getNVMLVersion);
    m.def("link_type_to_string", &cluster::linkTypeToString);
    m.def("get_link_bandwidth", &cluster::getLinkBandwidth);
    
    // NVLinkTransfer struct
    py::class_<cluster::NVLinkTransfer>(m, "NVLinkTransfer")
        .def(py::init<>())
        .def_readwrite("src_gpu", &cluster::NVLinkTransfer::src_gpu)
        .def_readwrite("dst_gpu", &cluster::NVLinkTransfer::dst_gpu)
        .def_readwrite("bytes", &cluster::NVLinkTransfer::bytes)
        .def_readwrite("timestamp", &cluster::NVLinkTransfer::timestamp)
        .def_readwrite("duration_ns", &cluster::NVLinkTransfer::duration_ns)
        .def_readwrite("link_id", &cluster::NVLinkTransfer::link_id);
    
    // PeerAccess struct
    py::class_<cluster::PeerAccess>(m, "PeerAccess")
        .def(py::init<>())
        .def_readwrite("src_gpu", &cluster::PeerAccess::src_gpu)
        .def_readwrite("dst_gpu", &cluster::PeerAccess::dst_gpu)
        .def_readwrite("address", &cluster::PeerAccess::address)
        .def_readwrite("bytes", &cluster::PeerAccess::bytes)
        .def_readwrite("is_write", &cluster::PeerAccess::is_write)
        .def_readwrite("timestamp", &cluster::PeerAccess::timestamp);
    
    // MultiGPUConfig struct
    py::class_<cluster::MultiGPUConfig>(m, "MultiGPUConfig")
        .def(py::init<>())
        .def_readwrite("gpu_ids", &cluster::MultiGPUConfig::gpu_ids)
        .def_readwrite("per_gpu_buffer_size", &cluster::MultiGPUConfig::per_gpu_buffer_size)
        .def_readwrite("enable_nvlink_tracking", &cluster::MultiGPUConfig::enable_nvlink_tracking)
        .def_readwrite("enable_peer_access_tracking", &cluster::MultiGPUConfig::enable_peer_access_tracking)
        .def_readwrite("aggregation_interval_ms", &cluster::MultiGPUConfig::aggregation_interval_ms)
        .def_readwrite("unified_timestamps", &cluster::MultiGPUConfig::unified_timestamps)
        .def_readwrite("capture_topology", &cluster::MultiGPUConfig::capture_topology)
        .def_readwrite("overflow_policy", &cluster::MultiGPUConfig::overflow_policy);
    
    // MultiGPUStats struct
    py::class_<cluster::MultiGPUStats>(m, "MultiGPUStats")
        .def(py::init<>())
        .def_readwrite("total_events", &cluster::MultiGPUStats::total_events)
        .def_readwrite("total_dropped", &cluster::MultiGPUStats::total_dropped)
        .def_readwrite("nvlink_transfers", &cluster::MultiGPUStats::nvlink_transfers)
        .def_readwrite("nvlink_bytes", &cluster::MultiGPUStats::nvlink_bytes)
        .def_readwrite("peer_accesses", &cluster::MultiGPUStats::peer_accesses)
        .def_readwrite("events_per_gpu", &cluster::MultiGPUStats::events_per_gpu)
        .def_readwrite("dropped_per_gpu", &cluster::MultiGPUStats::dropped_per_gpu)
        .def_readwrite("capture_duration_ms", &cluster::MultiGPUStats::capture_duration_ms);
    
    // MultiGPUProfiler class
    py::class_<cluster::MultiGPUProfiler>(m, "MultiGPUProfiler")
        .def(py::init<>())
        .def(py::init<const cluster::MultiGPUConfig&>())
        .def("initialize", &cluster::MultiGPUProfiler::initialize)
        .def("finalize", &cluster::MultiGPUProfiler::finalize)
        .def("is_initialized", &cluster::MultiGPUProfiler::isInitialized)
        .def("add_gpu", &cluster::MultiGPUProfiler::addGPU)
        .def("remove_gpu", &cluster::MultiGPUProfiler::removeGPU)
        .def("get_active_gpus", &cluster::MultiGPUProfiler::getActiveGPUs)
        .def("get_available_gpu_count", &cluster::MultiGPUProfiler::getAvailableGPUCount)
        .def("start_capture", &cluster::MultiGPUProfiler::startCapture)
        .def("stop_capture", &cluster::MultiGPUProfiler::stopCapture)
        .def("is_capturing", &cluster::MultiGPUProfiler::isCapturing)
        .def("get_events", [](cluster::MultiGPUProfiler& self, size_t max_count) {
            std::vector<TraceEvent> events;
            self.getEvents(events, max_count);
            return events;
        }, py::arg("max_count") = 0)
        .def("get_events_from_gpu", [](cluster::MultiGPUProfiler& self, uint32_t gpu_id) {
            std::vector<TraceEvent> events;
            self.getEventsFromGPU(gpu_id, events);
            return events;
        })
        .def("get_nvlink_transfers", &cluster::MultiGPUProfiler::getNVLinkTransfers)
        .def("get_peer_accesses", &cluster::MultiGPUProfiler::getPeerAccesses)
        .def("total_events_captured", &cluster::MultiGPUProfiler::totalEventsCaptured)
        .def("events_from_gpu", &cluster::MultiGPUProfiler::eventsFromGPU)
        .def("get_statistics", &cluster::MultiGPUProfiler::getStatistics)
        .def("get_all_device_info", &cluster::MultiGPUProfiler::getAllDeviceInfo)
        .def("get_device_info", &cluster::MultiGPUProfiler::getDeviceInfo);
    
    // =========================================================================
    // Time Synchronization (v0.7.1)
    // =========================================================================
    
    // TimeSyncMethod enum
    py::enum_<cluster::TimeSyncMethod>(m, "TimeSyncMethod")
        .value("SystemClock", cluster::TimeSyncMethod::SystemClock)
        .value("NTP", cluster::TimeSyncMethod::NTP)
        .value("PTP", cluster::TimeSyncMethod::PTP)
        .value("CUDA", cluster::TimeSyncMethod::CUDA)
        .value("Custom", cluster::TimeSyncMethod::Custom)
        .export_values();
    
    // TimeSyncConfig struct
    py::class_<cluster::TimeSyncConfig>(m, "TimeSyncConfig")
        .def(py::init<>())
        .def_readwrite("method", &cluster::TimeSyncConfig::method)
        .def_readwrite("ntp_server", &cluster::TimeSyncConfig::ntp_server)
        .def_readwrite("ptp_interface", &cluster::TimeSyncConfig::ptp_interface)
        .def_readwrite("sync_interval_ms", &cluster::TimeSyncConfig::sync_interval_ms)
        .def_readwrite("max_acceptable_offset_ns", &cluster::TimeSyncConfig::max_acceptable_offset_ns);
    
    // SyncResult struct
    py::class_<cluster::SyncResult>(m, "SyncResult")
        .def(py::init<>())
        .def_readwrite("success", &cluster::SyncResult::success)
        .def_readwrite("offset_ns", &cluster::SyncResult::offset_ns)
        .def_readwrite("round_trip_ns", &cluster::SyncResult::round_trip_ns)
        .def_readwrite("uncertainty_ns", &cluster::SyncResult::uncertainty_ns)
        .def_readwrite("sync_time", &cluster::SyncResult::sync_time)
        .def_readwrite("error_message", &cluster::SyncResult::error_message);
    
    // TimeSync class
    py::class_<cluster::TimeSync>(m, "TimeSync")
        .def(py::init<>())
        .def(py::init<const cluster::TimeSyncConfig&>())
        .def("initialize", &cluster::TimeSync::initialize)
        .def("finalize", &cluster::TimeSync::finalize)
        .def("is_initialized", &cluster::TimeSync::isInitialized)
        .def("get_config", &cluster::TimeSync::getConfig)
        .def("synchronize", &cluster::TimeSync::synchronize)
        .def("synchronize_with_node", &cluster::TimeSync::synchronizeWithNode)
        .def("to_synchronized_time", &cluster::TimeSync::toSynchronizedTime)
        .def("to_local_time", &cluster::TimeSync::toLocalTime)
        .def("get_current_offset", &cluster::TimeSync::getCurrentOffset)
        .def("set_manual_offset", &cluster::TimeSync::setManualOffset)
        .def("correlate_gpu_timestamps", &cluster::TimeSync::correlateGPUTimestamps)
        .def("get_gpu_offset", &cluster::TimeSync::getGPUOffset)
        .def("set_gpu_offset", &cluster::TimeSync::setGPUOffset)
        .def("get_average_offset", &cluster::TimeSync::getAverageOffset)
        .def("get_offset_std_dev", &cluster::TimeSync::getOffsetStdDev)
        .def("get_sync_count", &cluster::TimeSync::getSyncCount)
        .def("get_last_sync_result", &cluster::TimeSync::getLastSyncResult)
        .def("clear_history", &cluster::TimeSync::clearHistory);
    
    // ClockCorrelator::DriftModel struct
    py::class_<cluster::ClockCorrelator::DriftModel>(m, "DriftModel")
        .def(py::init<>())
        .def_readwrite("offset", &cluster::ClockCorrelator::DriftModel::offset)
        .def_readwrite("drift_rate", &cluster::ClockCorrelator::DriftModel::drift_rate)
        .def_readwrite("r_squared", &cluster::ClockCorrelator::DriftModel::r_squared)
        .def_readwrite("valid", &cluster::ClockCorrelator::DriftModel::valid);
    
    // ClockCorrelator class
    py::class_<cluster::ClockCorrelator>(m, "ClockCorrelator")
        .def(py::init<>())
        .def("add_correlation_point", &cluster::ClockCorrelator::addCorrelationPoint)
        .def("calculate_offset", &cluster::ClockCorrelator::calculateOffset)
        .def("correct_timestamps", &cluster::ClockCorrelator::correctTimestamps)
        .def("calculate_drift_model", &cluster::ClockCorrelator::calculateDriftModel)
        .def("apply_drift_correction", &cluster::ClockCorrelator::applyDriftCorrection)
        .def("clear", &cluster::ClockCorrelator::clear)
        .def("clear_source", &cluster::ClockCorrelator::clearSource);
    
    // Utility functions
    m.def("time_sync_method_to_string", &cluster::timeSyncMethodToString);
    m.def("string_to_time_sync_method", &cluster::stringToTimeSyncMethod);
    
    // =========================================================================
    // NCCL Tracking (v0.7.1)
    // =========================================================================
    
    // NCCLOpType enum
    py::enum_<cluster::NCCLOpType>(m, "NCCLOpType")
        .value("Unknown", cluster::NCCLOpType::Unknown)
        .value("AllReduce", cluster::NCCLOpType::AllReduce)
        .value("AllGather", cluster::NCCLOpType::AllGather)
        .value("ReduceScatter", cluster::NCCLOpType::ReduceScatter)
        .value("Broadcast", cluster::NCCLOpType::Broadcast)
        .value("Reduce", cluster::NCCLOpType::Reduce)
        .value("AllToAll", cluster::NCCLOpType::AllToAll)
        .value("Send", cluster::NCCLOpType::Send)
        .value("Recv", cluster::NCCLOpType::Recv)
        .value("GroupStart", cluster::NCCLOpType::GroupStart)
        .value("GroupEnd", cluster::NCCLOpType::GroupEnd)
        .export_values();
    
    // NCCLRedOp enum
    py::enum_<cluster::NCCLRedOp>(m, "NCCLRedOp")
        .value("Sum", cluster::NCCLRedOp::Sum)
        .value("Prod", cluster::NCCLRedOp::Prod)
        .value("Max", cluster::NCCLRedOp::Max)
        .value("Min", cluster::NCCLRedOp::Min)
        .value("Avg", cluster::NCCLRedOp::Avg)
        .export_values();
    
    // NCCLDataType enum
    py::enum_<cluster::NCCLDataType>(m, "NCCLDataType")
        .value("Int8", cluster::NCCLDataType::Int8)
        .value("Uint8", cluster::NCCLDataType::Uint8)
        .value("Int32", cluster::NCCLDataType::Int32)
        .value("Uint32", cluster::NCCLDataType::Uint32)
        .value("Int64", cluster::NCCLDataType::Int64)
        .value("Uint64", cluster::NCCLDataType::Uint64)
        .value("Float16", cluster::NCCLDataType::Float16)
        .value("Float32", cluster::NCCLDataType::Float32)
        .value("Float64", cluster::NCCLDataType::Float64)
        .value("BFloat16", cluster::NCCLDataType::BFloat16)
        .export_values();
    
    // NCCLOperation struct
    py::class_<cluster::NCCLOperation>(m, "NCCLOperation")
        .def(py::init<>())
        .def_readwrite("op_id", &cluster::NCCLOperation::op_id)
        .def_readwrite("op_type", &cluster::NCCLOperation::op_type)
        .def_readwrite("red_op", &cluster::NCCLOperation::red_op)
        .def_readwrite("data_type", &cluster::NCCLOperation::data_type)
        .def_readwrite("comm_id", &cluster::NCCLOperation::comm_id)
        .def_readwrite("rank", &cluster::NCCLOperation::rank)
        .def_readwrite("world_size", &cluster::NCCLOperation::world_size)
        .def_readwrite("count", &cluster::NCCLOperation::count)
        .def_readwrite("data_size", &cluster::NCCLOperation::data_size)
        .def_readwrite("start_time", &cluster::NCCLOperation::start_time)
        .def_readwrite("end_time", &cluster::NCCLOperation::end_time)
        .def_readwrite("duration_ns", &cluster::NCCLOperation::duration_ns)
        .def_readwrite("peer_rank", &cluster::NCCLOperation::peer_rank)
        .def_readwrite("cuda_stream", &cluster::NCCLOperation::cuda_stream)
        .def_readwrite("correlation_id", &cluster::NCCLOperation::correlation_id)
        .def_readwrite("completed", &cluster::NCCLOperation::completed);
    
    // NCCLTrackerConfig struct
    py::class_<cluster::NCCLTrackerConfig>(m, "NCCLTrackerConfig")
        .def(py::init<>())
        .def_readwrite("hook_enabled", &cluster::NCCLTrackerConfig::hook_enabled)
        .def_readwrite("track_all_comms", &cluster::NCCLTrackerConfig::track_all_comms)
        .def_readwrite("comm_filter", &cluster::NCCLTrackerConfig::comm_filter)
        .def_readwrite("capture_call_stack", &cluster::NCCLTrackerConfig::capture_call_stack)
        .def_readwrite("max_operations", &cluster::NCCLTrackerConfig::max_operations);
    
    // NCCLTracker::Statistics struct
    py::class_<cluster::NCCLTracker::Statistics>(m, "NCCLStatistics")
        .def(py::init<>())
        .def_readwrite("total_operations", &cluster::NCCLTracker::Statistics::total_operations)
        .def_readwrite("total_bytes_transferred", &cluster::NCCLTracker::Statistics::total_bytes_transferred)
        .def_readwrite("total_duration_ns", &cluster::NCCLTracker::Statistics::total_duration_ns)
        .def_readwrite("ops_by_type", &cluster::NCCLTracker::Statistics::ops_by_type)
        .def_readwrite("bytes_by_type", &cluster::NCCLTracker::Statistics::bytes_by_type)
        .def_readwrite("duration_by_type", &cluster::NCCLTracker::Statistics::duration_by_type);
    
    // NCCLTracker class
    py::class_<cluster::NCCLTracker>(m, "NCCLTracker")
        .def(py::init<>())
        .def(py::init<const cluster::NCCLTrackerConfig&>())
        .def("install_hooks", &cluster::NCCLTracker::installHooks)
        .def("remove_hooks", &cluster::NCCLTracker::removeHooks)
        .def("is_hooked", &cluster::NCCLTracker::isHooked)
        .def("start_capture", &cluster::NCCLTracker::startCapture)
        .def("stop_capture", &cluster::NCCLTracker::stopCapture)
        .def("is_capturing", &cluster::NCCLTracker::isCapturing)
        .def("clear", &cluster::NCCLTracker::clear)
        .def("record_operation_start", &cluster::NCCLTracker::recordOperationStart,
             py::arg("type"), py::arg("count"), py::arg("dtype"), 
             py::arg("rank"), py::arg("stream") = 0)
        .def("record_operation_end", &cluster::NCCLTracker::recordOperationEnd)
        .def("get_operations", &cluster::NCCLTracker::getOperations)
        .def("get_operations_by_type", &cluster::NCCLTracker::getOperationsByType)
        .def("get_operations_by_comm", &cluster::NCCLTracker::getOperationsByComm)
        .def("get_operation", &cluster::NCCLTracker::getOperation)
        .def("to_trace_events", &cluster::NCCLTracker::toTraceEvents)
        .def("correlate_with_gpu_events", &cluster::NCCLTracker::correlateWithGPUEvents)
        .def("get_statistics", &cluster::NCCLTracker::getStatistics);
    
    // CommAnalysis::CommPattern enum
    py::enum_<cluster::CommAnalysis::CommPattern>(m, "CommPattern")
        .value("Unknown", cluster::CommAnalysis::CommPattern::Unknown)
        .value("AllToAll", cluster::CommAnalysis::CommPattern::AllToAll)
        .value("Ring", cluster::CommAnalysis::CommPattern::Ring)
        .value("Tree", cluster::CommAnalysis::CommPattern::Tree)
        .value("Butterfly", cluster::CommAnalysis::CommPattern::Butterfly)
        .value("PointToPoint", cluster::CommAnalysis::CommPattern::PointToPoint)
        .value("Broadcast", cluster::CommAnalysis::CommPattern::Broadcast)
        .value("Custom", cluster::CommAnalysis::CommPattern::Custom)
        .export_values();
    
    // CommAnalysis::CommMatrix struct
    py::class_<cluster::CommAnalysis::CommMatrix>(m, "CommMatrix")
        .def(py::init<>())
        .def_readwrite("bytes", &cluster::CommAnalysis::CommMatrix::bytes)
        .def_readwrite("count", &cluster::CommAnalysis::CommMatrix::count)
        .def_readwrite("avg_latency", &cluster::CommAnalysis::CommMatrix::avg_latency)
        .def_readwrite("world_size", &cluster::CommAnalysis::CommMatrix::world_size);
    
    // CommAnalysis::Bottleneck struct
    py::class_<cluster::CommAnalysis::Bottleneck>(m, "CommBottleneck")
        .def(py::init<>())
        .def_readwrite("rank_a", &cluster::CommAnalysis::Bottleneck::rank_a)
        .def_readwrite("rank_b", &cluster::CommAnalysis::Bottleneck::rank_b)
        .def_readwrite("utilization", &cluster::CommAnalysis::Bottleneck::utilization)
        .def_readwrite("reason", &cluster::CommAnalysis::Bottleneck::reason);
    
    // CommAnalysis::LoadImbalance struct
    py::class_<cluster::CommAnalysis::LoadImbalance>(m, "LoadImbalance")
        .def(py::init<>())
        .def_readwrite("rank", &cluster::CommAnalysis::LoadImbalance::rank)
        .def_readwrite("deviation", &cluster::CommAnalysis::LoadImbalance::deviation)
        .def_readwrite("total_bytes", &cluster::CommAnalysis::LoadImbalance::total_bytes)
        .def_readwrite("total_time_ns", &cluster::CommAnalysis::LoadImbalance::total_time_ns);
    
    // CommAnalysis class
    py::class_<cluster::CommAnalysis>(m, "CommAnalysis")
        .def(py::init<>())
        .def("add_operations", &cluster::CommAnalysis::addOperations)
        .def("add_operation", &cluster::CommAnalysis::addOperation)
        .def("clear", &cluster::CommAnalysis::clear)
        .def("get_comm_matrix", &cluster::CommAnalysis::getCommMatrix)
        .def("detect_pattern", &cluster::CommAnalysis::detectPattern)
        .def("find_bottlenecks", &cluster::CommAnalysis::findBottlenecks)
        .def("analyze_load_balance", &cluster::CommAnalysis::analyzeLoadBalance)
        .def("matrix_to_ascii", &cluster::CommAnalysis::matrixToASCII)
        .def("matrix_to_heatmap_json", &cluster::CommAnalysis::matrixToHeatmapJSON)
        .def("get_total_bytes", &cluster::CommAnalysis::getTotalBytes)
        .def("get_total_operations", &cluster::CommAnalysis::getTotalOperations)
        .def("get_world_size", &cluster::CommAnalysis::getWorldSize)
        .def_static("pattern_to_string", &cluster::CommAnalysis::patternToString);
    
    // NCCL utility functions
    m.def("nccl_op_type_to_string", &cluster::ncclOpTypeToString);
    m.def("nccl_red_op_to_string", &cluster::ncclRedOpToString);
    m.def("nccl_data_type_to_string", &cluster::ncclDataTypeToString);
    m.def("nccl_data_type_size", &cluster::ncclDataTypeSize);
}
