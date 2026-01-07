/**
 * @file gdb_types.hpp
 * @brief Common types for GDB RSP backend integration
 * @version 0.10.0
 */

#pragma once

#include "tracesmith/common/types.hpp"
#include "tracesmith/state/gpu_state_machine.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace tracesmith {
namespace gdb {

/// GDB signal codes (prefixed with Sig_ to avoid macro conflicts)
enum class Signal : int {
    None = 0,
    Sig_HUP = 1,
    Sig_INT = 2,
    Sig_QUIT = 3,
    Sig_TRAP = 5,
    Sig_ABRT = 6,
    Sig_KILL = 9,
    Sig_SEGV = 11,
    Sig_TERM = 15,
    Sig_CONT = 18,
    Sig_STOP = 19
};

/// Stop reason for target
enum class StopReason {
    None,
    Breakpoint,         // CPU breakpoint hit
    Watchpoint,         // Memory watchpoint triggered
    Signal,             // Signal received
    Exited,             // Process exited
    GPUBreakpoint,      // GPU breakpoint hit
    GPUEvent            // GPU event occurred
};

/// GPU breakpoint types
enum class GPUBreakpointType : uint8_t {
    KernelLaunch,       // Break on kernel launch
    KernelComplete,     // Break on kernel completion
    MemAlloc,           // Break on GPU memory allocation
    MemFree,            // Break on GPU memory free
    MemcpyH2D,          // Break on host-to-device copy
    MemcpyD2H,          // Break on device-to-host copy
    MemcpyD2D,          // Break on device-to-device copy
    Synchronize,        // Break on device synchronization
    AnyEvent            // Break on any GPU event
};

/// Convert GPUBreakpointType to string
inline const char* gpuBreakpointTypeToString(GPUBreakpointType type) {
    switch (type) {
        case GPUBreakpointType::KernelLaunch:   return "KernelLaunch";
        case GPUBreakpointType::KernelComplete: return "KernelComplete";
        case GPUBreakpointType::MemAlloc:       return "MemAlloc";
        case GPUBreakpointType::MemFree:        return "MemFree";
        case GPUBreakpointType::MemcpyH2D:      return "MemcpyH2D";
        case GPUBreakpointType::MemcpyD2H:      return "MemcpyD2H";
        case GPUBreakpointType::MemcpyD2D:      return "MemcpyD2D";
        case GPUBreakpointType::Synchronize:    return "Synchronize";
        case GPUBreakpointType::AnyEvent:       return "AnyEvent";
        default:                                return "Unknown";
    }
}

/// GPU breakpoint definition
struct GPUBreakpoint {
    int id = -1;
    GPUBreakpointType type = GPUBreakpointType::KernelLaunch;
    std::string kernel_pattern;   // Wildcard pattern for kernel name
    int device_id = -1;           // -1 means any device
    bool enabled = true;
    uint64_t hit_count = 0;
    
    /// Check if this breakpoint matches the given event
    bool matches(const TraceEvent& event) const;
};

/// GPU state snapshot
struct GPUStateSnapshot {
    Timestamp timestamp = 0;
    std::vector<DeviceInfo> devices;
    
    /// Memory state per device
    struct DeviceMemoryState {
        uint32_t device_id = 0;
        uint64_t total_memory = 0;
        uint64_t used_memory = 0;
        uint64_t free_memory = 0;
        size_t allocation_count = 0;
    };
    std::vector<DeviceMemoryState> memory_states;
    
    /// Stream states
    struct StreamState {
        uint32_t device_id = 0;
        uint32_t stream_id = 0;
        GPUState state = GPUState::Idle;
        size_t pending_operations = 0;
    };
    std::vector<StreamState> stream_states;
    
    /// Active kernels
    std::vector<TraceEvent> active_kernels;
    
    /// Recent events (last N)
    std::vector<TraceEvent> recent_events;
};

/// Kernel call info for history
struct KernelCallInfo {
    uint64_t call_id = 0;
    std::string kernel_name;
    Timestamp launch_time = 0;
    Timestamp complete_time = 0;        // 0 if still running
    uint32_t device_id = 0;
    uint32_t stream_id = 0;
    KernelParams params;
    std::optional<CallStack> host_callstack;
    
    /// Check if kernel execution is complete
    bool isComplete() const { return complete_time > 0; }
    
    /// Get duration (0 if not complete)
    Timestamp duration() const { 
        return isComplete() ? (complete_time - launch_time) : 0; 
    }
};

/// Stop event from target
struct StopEvent {
    StopReason reason = StopReason::None;
    Signal signal = Signal::None;
    int exit_code = 0;
    
    /// For GPU events
    std::optional<TraceEvent> gpu_event;
    std::optional<GPUBreakpoint> gpu_breakpoint;
    
    /// Location info
    uint64_t pc = 0;          // Program counter
    pid_t thread_id = 0;
    
    /// Get human-readable description
    std::string description() const;
};

/// Trace replay control
struct ReplayControl {
    enum class Command {
        Start,
        Stop,
        Pause,
        Resume,
        StepEvent,
        StepKernel,
        GotoTimestamp,
        GotoEvent
    };
    
    Command command = Command::Start;
    uint64_t target_timestamp = 0;
    size_t target_event_index = 0;
};

/// Replay state
struct ReplayState {
    bool active = false;
    bool paused = false;
    size_t current_event_index = 0;
    size_t total_events = 0;
    Timestamp current_timestamp = 0;
    Timestamp total_duration = 0;
    std::string trace_file;
};

} // namespace gdb
} // namespace tracesmith
