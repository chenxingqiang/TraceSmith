#pragma once

#include "tracesmith/capture/profiler.hpp"
#include <mutex>
#include <unordered_map>

#ifdef TRACESMITH_ENABLE_MACA
// MetaX MACA Runtime and Profiling headers
#include <mcr/maca.h>
#include <mcr/mc_runtime_api.h>
#include <mcpti/mcpti.h>
#endif

namespace tracesmith {

/**
 * MCPTI-based GPU Profiler for MetaX GPUs (C500/C550)
 * 
 * Uses MetaX MCPTI (MACA Profiling Tools Interface) to capture:
 * - Kernel launches and completions
 * - Memory operations (H2D, D2H, D2D, memset)
 * - Synchronization events
 * - Stream operations
 * 
 * Requirements:
 * - MetaX GPU (C500, C550, etc.)
 * - MACA SDK with MCPTI headers and library
 * - Driver with profiling permissions
 * 
 * Note: MCPTI API is highly compatible with NVIDIA CUPTI
 */
class MCPTIProfiler : public IPlatformProfiler {
public:
    MCPTIProfiler();
    ~MCPTIProfiler() override;
    
    // IPlatformProfiler interface
    PlatformType platformType() const override { return PlatformType::MACA; }
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

#ifdef TRACESMITH_ENABLE_MACA
    // MCPTI-specific methods
    
    /**
     * Set activity buffer size (default: 32MB)
     */
    void setBufferSize(size_t size_bytes);
    
    /**
     * Enable/disable specific activity types
     */
    void enableActivityKind(MCpti_ActivityKind kind, bool enable);
    
    /**
     * Get MCPTI version
     */
    uint32_t getMcptiVersion() const;
    
private:
    // MCPTI callback handlers (static for C API)
    static void MCPTIAPI bufferRequested(uint8_t** buffer, size_t* size, size_t* maxNumRecords);
    static void MCPTIAPI bufferCompleted(MCcontext ctx, uint32_t streamId, 
                                         uint8_t* buffer, size_t size, size_t validSize);
    static void MCPTIAPI callbackHandler(void* userdata, MCpti_CallbackDomain domain,
                                         MCpti_CallbackId cbid, const void* cbdata);
    
    // Activity processing
    void processActivity(MCpti_Activity* record);
    void processKernelActivity(const MCpti_ActivityKernel4* kernel);
    void processMemcpyActivity(const MCpti_ActivityMemcpy* memcpy);
    void processMemsetActivity(const MCpti_ActivityMemset* memset);
    void processSyncActivity(const MCpti_ActivitySynchronization* sync);
    
    // Event creation helpers
    TraceEvent createKernelEvent(const MCpti_ActivityKernel4* kernel);
    TraceEvent createMemcpyEvent(const MCpti_ActivityMemcpy* memcpy);
    TraceEvent createMemsetEvent(const MCpti_ActivityMemset* memset);
    TraceEvent createSyncEvent(const MCpti_ActivitySynchronization* sync);
    
    // Thread-safe event storage
    void addEvent(TraceEvent&& event);
    
    // MCPTI handles
    MCpti_SubscriberHandle subscriber_;
    
    // Activity buffer management
    size_t buffer_size_;
    static constexpr size_t DEFAULT_BUFFER_SIZE = 32 * 1024 * 1024; // 32MB
    static constexpr size_t ALIGN_SIZE = 8;
    
    // Enabled activity kinds
    std::vector<MCpti_ActivityKind> enabled_activities_;
    
    // Correlation ID tracking (to match kernel launch with completion)
    std::unordered_map<uint64_t, Timestamp> kernel_start_times_;
    // Thread ID tracking for multi-thread support
    std::unordered_map<uint64_t, uint32_t> correlation_thread_ids_;
    std::mutex correlation_mutex_;
    
#endif // TRACESMITH_ENABLE_MACA

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
    static MCPTIProfiler* instance_;
};

/**
 * Check if MACA and MCPTI are available on this system
 */
bool isMACAAvailable();

/**
 * Get MACA driver version
 */
int getMACADriverVersion();

/**
 * Get number of MetaX GPU devices
 */
int getMACADeviceCount();

} // namespace tracesmith
