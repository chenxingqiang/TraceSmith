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

// =============================================================================
// Common - Core types and utilities
// =============================================================================
#include "tracesmith/common/types.hpp"
#include "tracesmith/common/ring_buffer.hpp"
#include "tracesmith/common/stack_capture.hpp"
#include "tracesmith/common/xray_importer.hpp"

// =============================================================================
// Capture - GPU profiling backends
// =============================================================================
#include "tracesmith/capture/profiler.hpp"
#include "tracesmith/capture/memory_profiler.hpp"
#include "tracesmith/capture/bpf_types.hpp"

// Platform-specific profilers (conditionally included)
#ifdef TRACESMITH_ENABLE_CUDA
#include "tracesmith/capture/cupti_profiler.hpp"
#endif

#ifdef TRACESMITH_ENABLE_MACA
#include "tracesmith/capture/mcpti_profiler.hpp"
#endif

// =============================================================================
// Format - Trace file I/O
// =============================================================================
#include "tracesmith/format/sbt_format.hpp"

// =============================================================================
// State - GPU state reconstruction and visualization
// =============================================================================
#include "tracesmith/state/gpu_state_machine.hpp"
#include "tracesmith/state/instruction_stream.hpp"
#include "tracesmith/state/timeline_builder.hpp"
#include "tracesmith/state/timeline_viewer.hpp"
#include "tracesmith/state/perfetto_exporter.hpp"
#include "tracesmith/state/perfetto_proto_exporter.hpp"

// =============================================================================
// Replay - GPU trace replay engine
// =============================================================================
#include "tracesmith/replay/replay_engine.hpp"
#include "tracesmith/replay/replay_config.hpp"
#include "tracesmith/replay/determinism_checker.hpp"
#include "tracesmith/replay/operation_executor.hpp"
#include "tracesmith/replay/stream_scheduler.hpp"
#include "tracesmith/replay/frame_capture.hpp"

// =============================================================================
// Cluster - Multi-GPU profiling (v0.7.0+)
// =============================================================================
#include "tracesmith/cluster/gpu_topology.hpp"
#include "tracesmith/cluster/multi_gpu_profiler.hpp"
#include "tracesmith/cluster/time_sync.hpp"
#include "tracesmith/cluster/nccl_tracker.hpp"

namespace tracesmith {

/// Get version string
inline std::string getVersionString() {
    return std::to_string(VERSION_MAJOR) + "." + 
           std::to_string(VERSION_MINOR) + "." + 
           std::to_string(VERSION_PATCH);
}

} // namespace tracesmith
