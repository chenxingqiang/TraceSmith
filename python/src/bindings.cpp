/**
 * TraceSmith Python Bindings
 * 
 * Provides Python access to TraceSmith GPU profiling and replay functionality.
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
        .export_values();
    
    // TraceEvent class
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
        .def("__repr__", [](const TraceEvent& e) {
            return "<TraceEvent " + e.name + " type=" + 
                   std::string(eventTypeToString(e.type)) + ">";
        });
    
    // DeviceInfo class
    py::class_<DeviceInfo>(m, "DeviceInfo")
        .def(py::init<>())
        .def_readwrite("device_id", &DeviceInfo::device_id)
        .def_readwrite("name", &DeviceInfo::name)
        .def_readwrite("vendor", &DeviceInfo::vendor)
        .def_readwrite("total_memory", &DeviceInfo::total_memory)
        .def_readwrite("multiprocessor_count", &DeviceInfo::multiprocessor_count);
    
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
    
    // PerfettoExporter class
    py::class_<PerfettoExporter>(m, "PerfettoExporter")
        .def(py::init<>())
        .def("export_to_file", &PerfettoExporter::exportToFile)
        .def("export_to_string", &PerfettoExporter::exportToString);
    
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
