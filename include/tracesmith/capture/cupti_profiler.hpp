#pragma once

#include "tracesmith/capture/profiler.hpp"
#include <mutex>
#include <unordered_map>

#ifdef TRACESMITH_ENABLE_CUDA
#include <cuda.h>
#include <cupti.h>
#endif

namespace tracesmith {

/**
 * CUPTI-based GPU Profiler for NVIDIA GPUs
 * 
 * Uses NVIDIA CUPTI (CUDA Profiling Tools Interface) to capture:
 * - Kernel launches and completions
 * - Memory operations (H2D, D2H, D2D, memset)
 * - Synchronization events
 * - Stream operations
 * 
 * Requirements:
 * - NVIDIA GPU with CUDA support
 * - CUDA Toolkit with CUPTI headers and library
 * - Driver with profiling permissions (may require admin/root)
 */
class CUPTIProfiler : public IPlatformProfiler {
public:
    CUPTIProfiler();
    ~CUPTIProfiler() override;
    
    // IPlatformProfiler interface
    PlatformType platformType() const override { return PlatformType::CUDA; }
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

#ifdef TRACESMITH_ENABLE_CUDA
    // CUPTI-specific methods
    
    /**
     * Set activity buffer size (default: 32MB)
     */
    void setBufferSize(size_t size_bytes);
    
    /**
     * Enable/disable specific activity types
     */
    void enableActivityKind(CUpti_ActivityKind kind, bool enable);
    
    /**
     * Get CUPTI version
     */
    uint32_t getCuptiVersion() const;
    
private:
    // CUPTI callback handlers (static for C API)
    static void CUPTIAPI bufferRequested(uint8_t** buffer, size_t* size, size_t* maxNumRecords);
    static void CUPTIAPI bufferCompleted(CUcontext ctx, uint32_t streamId, 
                                         uint8_t* buffer, size_t size, size_t validSize);
    static void CUPTIAPI callbackHandler(void* userdata, CUpti_CallbackDomain domain,
                                         CUpti_CallbackId cbid, const void* cbdata);
    
    // Activity processing
    void processActivity(CUpti_Activity* record);
    void processKernelActivity(const CUpti_ActivityKernel4* kernel);
    void processMemcpyActivity(const CUpti_ActivityMemcpy* memcpy);
    void processMemsetActivity(const CUpti_ActivityMemset* memset);
    void processSyncActivity(const CUpti_ActivitySynchronization* sync);
    
    // Event creation helpers
    TraceEvent createKernelEvent(const CUpti_ActivityKernel4* kernel);
    TraceEvent createMemcpyEvent(const CUpti_ActivityMemcpy* memcpy);
    TraceEvent createMemsetEvent(const CUpti_ActivityMemset* memset);
    TraceEvent createSyncEvent(const CUpti_ActivitySynchronization* sync);
    
    // Thread-safe event storage
    void addEvent(TraceEvent&& event);
    
    // CUPTI handles
    CUpti_SubscriberHandle subscriber_;
    
    // Activity buffer management
    size_t buffer_size_;
    static constexpr size_t DEFAULT_BUFFER_SIZE = 32 * 1024 * 1024; // 32MB
    static constexpr size_t ALIGN_SIZE = 8;
    
    // Enabled activity kinds
    std::vector<CUpti_ActivityKind> enabled_activities_;
    
    // Correlation ID tracking (to match kernel launch with completion)
    std::unordered_map<uint64_t, Timestamp> kernel_start_times_;
    // Thread ID tracking for multi-thread support
    std::unordered_map<uint64_t, uint32_t> correlation_thread_ids_;
    std::mutex correlation_mutex_;
    
#endif // TRACESMITH_ENABLE_CUDA

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
    
    // Singleton instance for static callbacks
    static CUPTIProfiler* instance_;
};

/**
 * Check if CUDA and CUPTI are available on this system
 */
bool isCUDAAvailable();

/**
 * Get CUDA driver version
 */
int getCUDADriverVersion();

/**
 * Get number of CUDA-capable devices
 */
int getCUDADeviceCount();

} // namespace tracesmith
