#pragma once

#include "tracesmith/common/types.hpp"
#include "tracesmith/common/ring_buffer.hpp"
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace tracesmith {

/// Profiler configuration options
struct ProfilerConfig {
    size_t buffer_size = 1024 * 1024;  // Ring buffer size (number of events)
    OverflowPolicy overflow_policy = OverflowPolicy::DropOldest;
    bool capture_callstacks = true;
    uint32_t callstack_depth = 32;
    bool capture_kernel_params = true;
    bool capture_memory_params = true;
    
    // Event filtering
    bool capture_kernels = true;
    bool capture_memcpy = true;
    bool capture_memset = true;
    bool capture_sync = true;
    bool capture_alloc = true;
};

/// Callback type for event notification
using EventCallback = std::function<void(const TraceEvent&)>;

/// Platform type enumeration
enum class PlatformType {
    Unknown,
    CUDA,
    ROCm,
    Metal,
    MACA    // MetaX MACA (C500, C550, etc.)
};

/// Convert PlatformType to string
inline const char* platformTypeToString(PlatformType type) {
    switch (type) {
        case PlatformType::CUDA:  return "CUDA";
        case PlatformType::ROCm:  return "ROCm";
        case PlatformType::Metal: return "Metal";
        case PlatformType::MACA:  return "MACA";
        default:                  return "Unknown";
    }
}

/**
 * Abstract interface for GPU profilers.
 * 
 * Implementations should be provided for each supported platform:
 * - CUPTIProfiler for NVIDIA CUDA
 * - ROCmProfiler for AMD ROCm
 * - MetalProfiler for Apple Metal
 */
class IPlatformProfiler {
public:
    virtual ~IPlatformProfiler() = default;
    
    /// Get the platform type
    virtual PlatformType platformType() const = 0;
    
    /// Check if the platform is available on this system
    virtual bool isAvailable() const = 0;
    
    /// Initialize the profiler
    virtual bool initialize(const ProfilerConfig& config) = 0;
    
    /// Finalize and cleanup
    virtual void finalize() = 0;
    
    /// Start capturing events
    virtual bool startCapture() = 0;
    
    /// Stop capturing events
    virtual bool stopCapture() = 0;
    
    /// Check if currently capturing
    virtual bool isCapturing() const = 0;
    
    /// Get captured events (drains the internal buffer)
    virtual size_t getEvents(std::vector<TraceEvent>& events, size_t max_count = 0) = 0;
    
    /// Get device information
    virtual std::vector<DeviceInfo> getDeviceInfo() const = 0;
    
    /// Set event callback (called for each event as it's captured)
    virtual void setEventCallback(EventCallback callback) = 0;
    
    /// Get statistics
    virtual uint64_t eventsCaptured() const = 0;
    virtual uint64_t eventsDropped() const = 0;
};

/**
 * Factory function to create a profiler for the available platform.
 */
std::unique_ptr<IPlatformProfiler> createProfiler(PlatformType type = PlatformType::Unknown);

/**
 * Detect available GPU platform.
 */
PlatformType detectPlatform();

/**
 * Platform detection functions (always available for Python bindings)
 */
bool isCUDAAvailable();
int getCUDADeviceCount();
int getCUDADriverVersion();

bool isMetalAvailable();
int getMetalDeviceCount();

bool isMACAAvailable();
int getMACADriverVersion();
int getMACADeviceCount();

} // namespace tracesmith
