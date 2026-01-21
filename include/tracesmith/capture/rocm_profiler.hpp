#pragma once

#include "tracesmith/capture/profiler.hpp"
#include <mutex>
#include <unordered_map>
#include <atomic>

#ifdef TRACESMITH_ENABLE_ROCM
#include <hip/hip_runtime.h>
#include <roctracer/roctracer.h>
#include <roctracer/roctracer_hip.h>
#include <roctracer/roctracer_hsa.h>
#endif

namespace tracesmith {

/**
 * ROCm Profiler for AMD GPUs
 * 
 * Uses AMD roctracer API to capture:
 * - HIP kernel launches and completions
 * - Memory operations (H2D, D2H, D2D, memset)
 * - Synchronization events
 * - Stream operations
 * 
 * Requirements:
 * - AMD GPU with ROCm support (gfx9xx, gfx10xx, etc.)
 * - ROCm SDK with roctracer headers and library
 * - Driver with profiling permissions (may require root or video group membership)
 * 
 * Supported GPUs:
 * - AMD Radeon Instinct series (MI50, MI100, MI200, MI300)
 * - AMD Radeon Pro series (W6800, W7900)
 * - AMD Radeon RX series (RX 6000, RX 7000)
 */
class ROCmProfiler : public IPlatformProfiler {
public:
    ROCmProfiler();
    ~ROCmProfiler() override;
    
    // IPlatformProfiler interface
    PlatformType platformType() const override { return PlatformType::ROCm; }
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

#ifdef TRACESMITH_ENABLE_ROCM
    // ROCm-specific methods
    
    /**
     * Set activity buffer size (default: 32MB)
     */
    void setBufferSize(size_t size_bytes);
    
    /**
     * Enable/disable HIP API tracing
     */
    void enableHipApiTracing(bool enable);
    
    /**
     * Enable/disable HIP activity (async ops) tracing
     */
    void enableHipActivityTracing(bool enable);
    
    /**
     * Enable/disable HSA API tracing (low-level)
     */
    void enableHsaApiTracing(bool enable);
    
    /**
     * Get roctracer version
     */
    uint32_t getRoctracerVersion() const;
    
private:
    // roctracer callback handlers (static for C API)
    static void hipApiCallback(uint32_t domain, uint32_t cid, 
                               const void* callback_data, void* arg);
    static void hipActivityCallback(const char* begin, const char* end, void* arg);
    static void hsaApiCallback(uint32_t domain, uint32_t cid,
                               const void* callback_data, void* arg);
    
    // Activity processing
    void processHipActivity(const roctracer_record_t* record);
    void processKernelActivity(const roctracer_record_t* record);
    void processMemcpyActivity(const roctracer_record_t* record);
    void processMemsetActivity(const roctracer_record_t* record);
    void processSyncActivity(const roctracer_record_t* record);
    
    // Event creation helpers
    TraceEvent createKernelEvent(const roctracer_record_t* record);
    TraceEvent createMemcpyEvent(const roctracer_record_t* record);
    TraceEvent createMemsetEvent(const roctracer_record_t* record);
    TraceEvent createSyncEvent(const roctracer_record_t* record);
    
    // Thread-safe event storage
    void addEvent(TraceEvent&& event);
    
    // roctracer handles
    roctracer_pool_t* activity_pool_;
    
    // Activity buffer management
    size_t buffer_size_;
    static constexpr size_t DEFAULT_BUFFER_SIZE = 32 * 1024 * 1024; // 32MB
    static constexpr size_t BUFFER_CALLBACK_SIZE = 8 * 1024 * 1024; // 8MB callback threshold
    
    // Enabled tracing features
    bool hip_api_tracing_enabled_;
    bool hip_activity_tracing_enabled_;
    bool hsa_api_tracing_enabled_;
    
    // Correlation ID tracking (to match kernel launch with completion)
    std::unordered_map<uint64_t, Timestamp> kernel_start_times_;
    // Thread ID tracking for multi-thread support
    std::unordered_map<uint64_t, uint32_t> correlation_thread_ids_;
    std::mutex correlation_mutex_;
    
#endif // TRACESMITH_ENABLE_ROCM

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
    std::atomic<uint64_t> events_captured_;
    std::atomic<uint64_t> events_dropped_;
    
    // Correlation ID counter
    std::atomic<uint64_t> correlation_counter_;
    
    // Singleton instance for static callbacks
    static ROCmProfiler* instance_;
};

/**
 * Check if ROCm is available on this system
 */
bool isROCmAvailable();

/**
 * Get ROCm driver version
 */
int getROCmDriverVersion();

/**
 * Get number of AMD GPU devices
 */
int getROCmDeviceCount();

/**
 * Get AMD GPU architecture string (e.g., "gfx908", "gfx90a")
 */
std::string getROCmGpuArch(int device_id);

} // namespace tracesmith
