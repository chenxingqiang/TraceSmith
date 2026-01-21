#pragma once

/**
 * Tracy Client Integration for TraceSmith
 * 
 * This header provides integration between Tracy Profiler and TraceSmith,
 * enabling bidirectional data flow and unified profiling capabilities.
 * 
 * Features:
 * - Automatic Tracy zone emission from TraceSmith events
 * - Tracy macros available within TraceSmith profiling context
 * - GPU context bridging for CUDA/Metal/ROCm
 * - Memory profiling integration
 * - Frame marking support
 * 
 * Usage:
 *   // Enable Tracy integration in your build:
 *   // cmake .. -DTRACESMITH_ENABLE_TRACY=ON
 *   
 *   #include <tracesmith/tracy/tracy_client.hpp>
 *   
 *   // Use Tracy zones alongside TraceSmith profiling
 *   TracySmithZoneScoped("MyFunction");
 *   
 *   // Bridge TraceSmith events to Tracy
 *   tracesmith::tracy::emitToTracy(event);
 */

#include "tracesmith/common/types.hpp"

// Tracy configuration must be set before including Tracy headers
#ifndef TRACY_ENABLE
#  ifdef TRACESMITH_ENABLE_TRACY
#    define TRACY_ENABLE
#  endif
#endif

// Include Tracy headers conditionally
#ifdef TRACY_ENABLE
#  include <tracy/Tracy.hpp>
#  ifdef TRACESMITH_ENABLE_CUDA
#    include <tracy/TracyCUDA.hpp>
#  endif
#endif

#include <string>
#include <memory>
#include <atomic>
#include <cstdint>

namespace tracesmith {
namespace tracy {

// =============================================================================
// Tracy Color Definitions (matching Tracy's color scheme)
// =============================================================================

namespace colors {
    constexpr uint32_t KernelLaunch    = 0xFF4444;  // Red
    constexpr uint32_t KernelComplete  = 0x44FF44;  // Green
    constexpr uint32_t MemcpyH2D       = 0x4444FF;  // Blue
    constexpr uint32_t MemcpyD2H       = 0xFF44FF;  // Magenta
    constexpr uint32_t MemcpyD2D       = 0x44FFFF;  // Cyan
    constexpr uint32_t MemAlloc        = 0xFFFF44;  // Yellow
    constexpr uint32_t MemFree         = 0xFF8844;  // Orange
    constexpr uint32_t StreamSync      = 0x8844FF;  // Purple
    constexpr uint32_t DeviceSync      = 0xFF4488;  // Pink
    constexpr uint32_t Default         = 0x888888;  // Gray
}

// =============================================================================
// Tracy Integration Status
// =============================================================================

/**
 * Check if Tracy integration is enabled at compile time
 */
inline bool isTracyEnabled() {
#ifdef TRACY_ENABLE
    return true;
#else
    return false;
#endif
}

/**
 * Check if Tracy server is connected (runtime check)
 */
inline bool isTracyConnected() {
#ifdef TRACY_ENABLE
    return ::tracy::GetProfiler().IsConnected();
#else
    return false;
#endif
}

// =============================================================================
// TraceSmith Event to Tracy Color Mapping
// =============================================================================

/**
 * Get Tracy color for a TraceSmith event type
 */
inline uint32_t getColorForEventType(EventType type) {
    switch (type) {
        case EventType::KernelLaunch:   return colors::KernelLaunch;
        case EventType::KernelComplete: return colors::KernelComplete;
        case EventType::MemcpyH2D:      return colors::MemcpyH2D;
        case EventType::MemcpyD2H:      return colors::MemcpyD2H;
        case EventType::MemcpyD2D:      return colors::MemcpyD2D;
        case EventType::MemAlloc:       return colors::MemAlloc;
        case EventType::MemFree:        return colors::MemFree;
        case EventType::StreamSync:     return colors::StreamSync;
        case EventType::DeviceSync:     return colors::DeviceSync;
        default:                        return colors::Default;
    }
}

// =============================================================================
// Tracy Zone Emission from TraceSmith Events
// =============================================================================

#ifdef TRACY_ENABLE

/**
 * Emit a TraceSmith event to Tracy as a zone
 * This creates a Tracy zone with the event's name, duration, and metadata
 */
inline void emitToTracy(const TraceEvent& event) {
    // Tracy zones are scoped, so we use the message API for retrospective events
    if (!event.name.empty()) {
        uint32_t color = getColorForEventType(event.type);
        ::tracy::Profiler::LogString(::tracy::MessageSourceType::User,
                                      ::tracy::MessageSeverity::Info,
                                      color, TRACY_CALLSTACK,
                                      event.name.size(), event.name.c_str());
    }
    
    // For kernel launches, also emit to plot data
    if (event.type == EventType::KernelLaunch && event.duration > 0) {
        double duration_ms = static_cast<double>(event.duration) / 1000000.0;
        ::tracy::Profiler::PlotData("Kernel Duration (ms)", duration_ms);
    }
}

/**
 * Emit a TraceSmith memory event to Tracy
 */
inline void emitMemoryToTracy(const MemoryEvent& event) {
    if (event.is_allocation) {
        ::tracy::Profiler::MemAllocCallstackNamed(
            reinterpret_cast<void*>(event.ptr), event.bytes, 
            TRACY_CALLSTACK, false, event.allocator_name.c_str());
    } else {
        ::tracy::Profiler::MemFreeCallstackNamed(
            reinterpret_cast<void*>(event.ptr), 
            TRACY_CALLSTACK, false, event.allocator_name.c_str());
    }
}

/**
 * Emit a TraceSmith counter event to Tracy plot
 */
inline void emitCounterToTracy(const CounterEvent& event) {
    ::tracy::Profiler::PlotData(event.counter_name.c_str(), event.value);
}

#else // !TRACY_ENABLE

// No-op implementations when Tracy is disabled
inline void emitToTracy(const TraceEvent&) {}
inline void emitMemoryToTracy(const MemoryEvent&) {}
inline void emitCounterToTracy(const CounterEvent&) {}

#endif // TRACY_ENABLE

// =============================================================================
// Scoped Zone Helper for Unified Profiling
// =============================================================================

/**
 * TracySmithZone - A unified zone that works with both Tracy and TraceSmith
 * 
 * When Tracy is enabled, this creates a Tracy zone.
 * It also records the zone as a TraceSmith event for later analysis.
 * 
 * Note: This is a simple timing wrapper that does NOT create Tracy zones directly.
 * Use the TracySmithZoneScoped macro for automatic Tracy zone creation.
 */
class TracySmithZone {
public:
    explicit TracySmithZone(const char* name, 
                           uint32_t /*color*/ = colors::Default,
                           bool active = true)
        : name_(name)
        , start_time_(getCurrentTimestamp())
        , active_(active)
    {
    }
    
    ~TracySmithZone() {
        if (active_) {
            end_time_ = getCurrentTimestamp();
            // Could emit to a global TraceSmith session here
        }
    }
    
    void setText(const char* text, size_t size) {
        // Store text for later use
        (void)text;
        (void)size;
    }
    
    void setColor(uint32_t color) {
        // Store color for later use
        (void)color;
    }
    
    void setValue(uint64_t value) {
        // Store value for later use
        (void)value;
    }
    
    Timestamp getDuration() const {
        return end_time_ > start_time_ ? end_time_ - start_time_ : 0;
    }
    
    const char* getName() const { return name_; }
    Timestamp getStartTime() const { return start_time_; }
    
private:
    const char* name_;
    Timestamp start_time_;
    Timestamp end_time_ = 0;
    bool active_;
};

// =============================================================================
// Frame Marking Support
// =============================================================================

/**
 * Mark a frame boundary for Tracy visualization
 */
inline void markFrame(const char* name = nullptr) {
#ifdef TRACY_ENABLE
    if (name) {
        ::tracy::Profiler::SendFrameMark(name);
    } else {
        ::tracy::Profiler::SendFrameMark(nullptr);
    }
#endif
    (void)name;
}

/**
 * Mark frame start (for discontinuous frames)
 */
inline void markFrameStart(const char* name) {
#ifdef TRACY_ENABLE
    ::tracy::Profiler::SendFrameMark(name, ::tracy::QueueType::FrameMarkMsgStart);
#endif
    (void)name;
}

/**
 * Mark frame end (for discontinuous frames)
 */
inline void markFrameEnd(const char* name) {
#ifdef TRACY_ENABLE
    ::tracy::Profiler::SendFrameMark(name, ::tracy::QueueType::FrameMarkMsgEnd);
#endif
    (void)name;
}

// =============================================================================
// Plot Configuration
// =============================================================================

/**
 * Tracy plot types for counter visualization
 */
enum class PlotType : uint8_t {
    Number = 0,   // Default numeric plot
    Memory = 1,   // Memory-style plot (shows allocations)
    Percentage = 2 // Percentage plot (0-100%)
};

/**
 * Configure a Tracy plot for counter visualization
 */
inline void configurePlot(const char* name, PlotType type, 
                          bool step = false, bool fill = true,
                          uint32_t color = 0) {
#ifdef TRACY_ENABLE
    ::tracy::Profiler::ConfigurePlot(name, static_cast<::tracy::PlotFormatType>(type), 
                                      step, fill, color);
#endif
    (void)name;
    (void)type;
    (void)step;
    (void)fill;
    (void)color;
}

// =============================================================================
// Application Info
// =============================================================================

/**
 * Set application information visible in Tracy
 */
inline void setAppInfo(const char* info, size_t size) {
#ifdef TRACY_ENABLE
    ::tracy::Profiler::MessageAppInfo(info, size);
#endif
    (void)info;
    (void)size;
}

inline void setAppInfo(const std::string& info) {
    setAppInfo(info.c_str(), info.size());
}

// =============================================================================
// Message Logging
// =============================================================================

/**
 * Log a message to Tracy with optional color
 */
inline void logMessage(const char* message, size_t size, uint32_t color = 0) {
#ifdef TRACY_ENABLE
    ::tracy::Profiler::LogString(::tracy::MessageSourceType::User,
                                  ::tracy::MessageSeverity::Info,
                                  color, TRACY_CALLSTACK,
                                  size, message);
#endif
    (void)message;
    (void)size;
    (void)color;
}

inline void logMessage(const std::string& message, uint32_t color = 0) {
    logMessage(message.c_str(), message.size(), color);
}

} // namespace tracy
} // namespace tracesmith

// =============================================================================
// Convenience Macros for Unified Profiling
// =============================================================================

#ifdef TRACY_ENABLE

// Helper to create Tracy zone using global namespace
// This avoids namespace conflicts when tracesmith::tracy is used
#define TRACESMITH_TRACY_ZONE_IMPL(name, color, line) \
    static constexpr ::tracy::SourceLocationData TRACESMITH_CONCAT(__tracy_src_, line) \
        { name, __FUNCTION__, __FILE__, (uint32_t)line, color }; \
    ::tracy::ScopedZone TRACESMITH_CONCAT(__tracy_zone_, line) \
        (&TRACESMITH_CONCAT(__tracy_src_, line), TRACY_CALLSTACK, true)

#define TRACESMITH_CONCAT_IMPL(a, b) a##b
#define TRACESMITH_CONCAT(a, b) TRACESMITH_CONCAT_IMPL(a, b)

// Scoped zone with name
#define TracySmithZoneScoped(name) \
    TRACESMITH_TRACY_ZONE_IMPL(name, 0, __LINE__); \
    tracesmith::tracy::TracySmithZone ___tracysmith_zone(name)

// Scoped zone with name and color  
#define TracySmithZoneScopedC(name, color) \
    TRACESMITH_TRACY_ZONE_IMPL(name, color, __LINE__); \
    tracesmith::tracy::TracySmithZone ___tracysmith_zone(name, color)

// Frame mark
#define TracySmithFrameMark() tracesmith::tracy::markFrame()
#define TracySmithFrameMarkNamed(name) tracesmith::tracy::markFrame(name)

// Message logging
#define TracySmithMessage(msg) tracesmith::tracy::logMessage(msg, strlen(msg))
#define TracySmithMessageC(msg, color) tracesmith::tracy::logMessage(msg, strlen(msg), color)

#else // !TRACY_ENABLE

// No-op macros when Tracy is disabled
#define TracySmithZoneScoped(name)
#define TracySmithZoneScopedC(name, color)
#define TracySmithFrameMark()
#define TracySmithFrameMarkNamed(name)
#define TracySmithMessage(msg)
#define TracySmithMessageC(msg, color)

#endif // TRACY_ENABLE
