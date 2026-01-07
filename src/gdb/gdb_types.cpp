/**
 * @file gdb_types.cpp
 * @brief Implementation of GDB types
 */

#include "tracesmith/gdb/gdb_types.hpp"
#include <sstream>
#include <fnmatch.h>

namespace tracesmith {
namespace gdb {

// ============================================================
// GPUBreakpoint Implementation
// ============================================================

bool GPUBreakpoint::matches(const TraceEvent& event) const {
    // Disabled breakpoints never match
    if (!enabled) {
        return false;
    }
    
    // Check event type matches breakpoint type
    bool type_match = false;
    switch (type) {
        case GPUBreakpointType::KernelLaunch:
            type_match = (event.type == EventType::KernelLaunch);
            break;
        case GPUBreakpointType::KernelComplete:
            type_match = (event.type == EventType::KernelComplete);
            break;
        case GPUBreakpointType::MemAlloc:
            type_match = (event.type == EventType::MemAlloc);
            break;
        case GPUBreakpointType::MemFree:
            type_match = (event.type == EventType::MemFree);
            break;
        case GPUBreakpointType::MemcpyH2D:
            type_match = (event.type == EventType::MemcpyH2D);
            break;
        case GPUBreakpointType::MemcpyD2H:
            type_match = (event.type == EventType::MemcpyD2H);
            break;
        case GPUBreakpointType::MemcpyD2D:
            type_match = (event.type == EventType::MemcpyD2D);
            break;
        case GPUBreakpointType::Synchronize:
            type_match = (event.type == EventType::StreamSync || 
                         event.type == EventType::DeviceSync ||
                         event.type == EventType::EventSync);
            break;
        case GPUBreakpointType::AnyEvent:
            type_match = true;
            break;
    }
    
    if (!type_match) {
        return false;
    }
    
    // Check device filter
    if (device_id >= 0 && static_cast<uint32_t>(device_id) != event.device_id) {
        return false;
    }
    
    // For kernel breakpoints, check pattern match
    if (type == GPUBreakpointType::KernelLaunch || 
        type == GPUBreakpointType::KernelComplete) {
        if (!kernel_pattern.empty()) {
            // Use fnmatch for wildcard matching
            if (fnmatch(kernel_pattern.c_str(), event.name.c_str(), 0) != 0) {
                return false;
            }
        }
    }
    
    return true;
}

// ============================================================
// StopEvent Implementation
// ============================================================

std::string StopEvent::description() const {
    std::ostringstream oss;
    
    switch (reason) {
        case StopReason::None:
            oss << "No stop";
            break;
            
        case StopReason::Breakpoint:
            oss << "Breakpoint hit at 0x" << std::hex << pc;
            break;
            
        case StopReason::Watchpoint:
            oss << "Watchpoint triggered at 0x" << std::hex << pc;
            break;
            
        case StopReason::Signal:
            oss << "Signal " << static_cast<int>(signal) << " received";
            break;
            
        case StopReason::Exited:
            oss << "Process exited with code " << exit_code;
            break;
            
        case StopReason::GPUBreakpoint:
            oss << "GPU breakpoint hit";
            if (gpu_breakpoint) {
                oss << " (#" << gpu_breakpoint->id << " " 
                    << gpuBreakpointTypeToString(gpu_breakpoint->type) << ")";
            }
            if (gpu_event) {
                oss << ": " << gpu_event->name;
            }
            break;
            
        case StopReason::GPUEvent:
            oss << "GPU event";
            if (gpu_event) {
                oss << ": " << eventTypeToString(gpu_event->type) 
                    << " " << gpu_event->name;
            }
            break;
    }
    
    if (thread_id > 0) {
        oss << " (thread " << thread_id << ")";
    }
    
    return oss.str();
}

} // namespace gdb
} // namespace tracesmith
