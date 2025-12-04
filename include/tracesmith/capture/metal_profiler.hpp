#pragma once

#include "tracesmith/capture/profiler.hpp"
#include <mutex>
#include <unordered_map>

#ifdef TRACESMITH_ENABLE_METAL
// Forward declarations for Objective-C types
// We store all Metal objects as void* to avoid type conflicts
#endif

namespace tracesmith {

/**
 * Metal-based GPU Profiler for Apple GPUs (macOS/iOS)
 * 
 * Uses Apple Metal Performance Profiling APIs to capture:
 * - Compute kernel dispatches
 * - Memory operations (buffer copies, texture operations)
 * - Command buffer execution
 * - GPU counters and statistics
 * 
 * Requirements:
 * - macOS 10.15+ or iOS 13+ with Metal support
 * - Metal framework
 * - GPU with performance counter support
 */
class MetalProfiler : public IPlatformProfiler {
public:
    MetalProfiler();
    ~MetalProfiler() override;
    
    // IPlatformProfiler interface
    PlatformType platformType() const override { return PlatformType::Metal; }
    bool isAvailable() const override;
    
    bool initialize(const ProfilerConfig& config) override;
    void finalize() override;
    
    bool startCapture() override;
    bool stopCapture() override;
    bool isCapturing() const override { return capturing_; }
    
    size_t getEvents(std::vector<TraceEvent>& events, size_t max_count = 0) override;
    std::vector<DeviceInfo> getDeviceInfo() const override;
    
    void setEventCallback(EventCallback callback) override;
    
    uint64_t eventsCaptured() const override { return events_captured_; }
    uint64_t eventsDropped() const override { return events_dropped_; }

#ifdef TRACESMITH_ENABLE_METAL
    // Metal-specific methods
    
    /**
     * Get the Metal device being profiled
     */
    void* getDevice() const;
    
    /**
     * Set whether to capture GPU counters (requires additional permissions)
     */
    void setCaptureCounters(bool enable);
    
    /**
     * Get Metal feature set information
     */
    std::string getFeatureSet() const;
    
    /**
     * Track a command buffer for profiling
     * Call this to manually instrument command buffers
     */
    void trackCommandBuffer(void* commandBuffer);
    
private:
    // Metal objects (Objective-C types, stored as void*)
    void* device_;              // MTLDevice*
    void* command_queue_;       // MTLCommandQueue*
    void* capture_manager_;     // MTLCaptureManager*
    void* capture_descriptor_;  // MTLCaptureDescriptor*
    
    // Command buffer tracking
    struct CommandBufferInfo {
        uint64_t correlation_id;
        Timestamp start_time;
        std::string label;
    };
    
    std::unordered_map<void*, CommandBufferInfo> tracked_buffers_;
    std::mutex tracking_mutex_;
    
    // Event processing
    void processCommandBuffer(void* buffer);
    void addEventForCommandBuffer(void* buffer, const std::string& label, 
                                   Timestamp start, Timestamp end);
    
    // Helper methods
    void setupCaptureManager();
    void cleanupCaptureManager();
    bool supportsGPUCapture() const;
    
    // Counter capture
    bool capture_counters_;
    
#endif // TRACESMITH_ENABLE_METAL

    // Configuration
    ProfilerConfig config_;
    
    // State
    bool initialized_;
    bool capturing_;
    
    // Event storage
    std::vector<TraceEvent> events_;
    std::mutex events_mutex_;
    EventCallback callback_;
    
    // Statistics
    uint64_t events_captured_;
    uint64_t events_dropped_;
    
    // Correlation ID counter
    std::atomic<uint64_t> correlation_counter_;
};

/**
 * Check if Metal is available on this system
 */
bool isMetalAvailable();

/**
 * Get list of Metal-capable devices
 */
int getMetalDeviceCount();

/**
 * Get Metal version string
 */
std::string getMetalVersion();

/**
 * Check if system supports GPU frame capture
 */
bool supportsMetalCapture();

} // namespace tracesmith
