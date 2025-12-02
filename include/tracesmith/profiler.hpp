#pragma once

#include "tracesmith/types.hpp"
#include "tracesmith/ring_buffer.hpp"
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
    Simulation  // For testing without GPU
};

/// Convert PlatformType to string
inline const char* platformTypeToString(PlatformType type) {
    switch (type) {
        case PlatformType::CUDA:       return "CUDA";
        case PlatformType::ROCm:       return "ROCm";
        case PlatformType::Metal:      return "Metal";
        case PlatformType::Simulation: return "Simulation";
        default:                       return "Unknown";
    }
}

/**
 * Abstract interface for GPU profilers.
 * 
 * Implementations should be provided for each supported platform:
 * - CUPTIProfiler for NVIDIA CUDA
 * - ROCmProfiler for AMD ROCm
 * - MetalProfiler for Apple Metal
 * - SimulationProfiler for testing
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
 * Simulation profiler for testing without real GPU hardware.
 * 
 * Generates synthetic GPU events that mimic real profiling data.
 * Useful for development, testing, and demonstrations.
 */
class SimulationProfiler : public IPlatformProfiler {
public:
    SimulationProfiler();
    ~SimulationProfiler() override;
    
    PlatformType platformType() const override { return PlatformType::Simulation; }
    bool isAvailable() const override { return true; }
    
    bool initialize(const ProfilerConfig& config) override;
    void finalize() override;
    
    bool startCapture() override;
    bool stopCapture() override;
    bool isCapturing() const override { return capturing_.load(); }
    
    size_t getEvents(std::vector<TraceEvent>& events, size_t max_count = 0) override;
    std::vector<DeviceInfo> getDeviceInfo() const override;
    
    void setEventCallback(EventCallback callback) override;
    
    uint64_t eventsCaptured() const override { return events_captured_.load(); }
    uint64_t eventsDropped() const override;
    
    // Simulation-specific methods
    void setEventRate(double events_per_second);
    void generateKernelEvent(const std::string& name, uint32_t stream_id = 0);
    void generateMemcpyEvent(EventType type, uint64_t size, uint32_t stream_id = 0);
    
private:
    ProfilerConfig config_;
    std::unique_ptr<RingBuffer<TraceEvent>> buffer_;
    EventCallback callback_;
    
    std::atomic<bool> capturing_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> generator_thread_;
    
    std::atomic<uint64_t> events_captured_;
    std::atomic<uint64_t> correlation_id_;
    
    double event_rate_;
    
    void generatorLoop();
    TraceEvent createRandomEvent();
};

/**
 * Factory function to create a profiler for the available platform.
 */
std::unique_ptr<IPlatformProfiler> createProfiler(PlatformType type = PlatformType::Unknown);

/**
 * Detect available GPU platform.
 */
PlatformType detectPlatform();

} // namespace tracesmith
