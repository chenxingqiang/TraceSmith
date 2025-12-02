#pragma once

/**
 * TraceSmith - GPU Profiling & Replay System
 * 
 * A cross-platform, high-performance GPU profiling toolkit that supports:
 * - Non-intrusive GPU event capture (10,000+ events/sec)
 * - GPU execution trace serialization
 * - GPU state machine reconstruction
 * - GPU instruction replay
 * - Multi-GPU and multi-stream support
 * 
 * Basic usage:
 * 
 *   #include <tracesmith/tracesmith.hpp>
 *   
 *   tracesmith::ProfilerConfig config;
 *   auto profiler = tracesmith::createProfiler();
 *   
 *   profiler->initialize(config);
 *   profiler->startCapture();
 *   
 *   // ... run GPU code ...
 *   
 *   profiler->stopCapture();
 *   
 *   std::vector<tracesmith::TraceEvent> events;
 *   profiler->getEvents(events);
 *   
 *   tracesmith::SBTWriter writer("trace.sbt");
 *   writer.writeEvents(events);
 *   writer.finalize();
 */

#include "tracesmith/types.hpp"
#include "tracesmith/ring_buffer.hpp"
#include "tracesmith/sbt_format.hpp"
#include "tracesmith/profiler.hpp"

namespace tracesmith {

/// Get version string
inline std::string getVersionString() {
    return std::to_string(VERSION_MAJOR) + "." + 
           std::to_string(VERSION_MINOR) + "." + 
           std::to_string(VERSION_PATCH);
}

} // namespace tracesmith
