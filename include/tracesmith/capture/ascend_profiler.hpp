/**
 * TraceSmith - Huawei Ascend NPU Profiler
 * 
 * Provides integration with Huawei CANN (Compute Architecture for Neural Networks)
 * for profiling Ascend NPU operations using ACL Profiling API.
 * 
 * Requirements:
 *   - Huawei Ascend driver installed
 *   - CANN toolkit (8.0+) with ACL profiling support
 *   - Environment: source /usr/local/Ascend/ascend-toolkit/set_env.sh
 */

#pragma once

#include <tracesmith/common/types.hpp>
#include <tracesmith/capture/profiler.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

#ifdef TRACESMITH_ENABLE_ASCEND
#include <acl/acl.h>
#include <acl/acl_prof.h>
#endif

namespace tracesmith {

/// Ascend profiling configuration
struct AscendProfilerConfig {
    /// Output directory for profiling results
    std::string output_dir = "./ascend_profiling";
    
    /// Device IDs to profile (empty = all devices)
    std::vector<uint32_t> device_ids;
    
    /// Profiling data types
    bool capture_acl_api = true;       // ACL API calls
    bool capture_task_time = true;     // Task execution time
    bool capture_aicore_metrics = true; // AI Core metrics
    bool capture_aicpu = true;         // AI CPU operations
    bool capture_hccl_trace = true;    // HCCL collective operations
    bool capture_memory = true;        // Memory operations
    
    /// AI Core metrics type
    enum class AicoreMetrics {
        ArithmeticUtilization = 0,
        PipeUtilization = 1,
        MemoryBandwidth = 2,
        L0BAndWidth = 3,
        ResourceConflictRatio = 4,
        MemoryUB = 5,
        L2Cache = 6,
        PipeExecuteUtilization = 7,
        MemoryAccess = 8,
        None = 0xFF
    };
    AicoreMetrics aicore_metrics = AicoreMetrics::ArithmeticUtilization;
    
    /// Storage limit in MB (0 = unlimited)
    uint64_t storage_limit_mb = 0;
};

/// Ascend device information
struct AscendDeviceInfo {
    uint32_t device_id = 0;
    std::string name;
    std::string chip_name;          // e.g., "Ascend 910B"
    uint64_t total_memory = 0;      // Total HBM memory in bytes
    uint64_t free_memory = 0;       // Available memory in bytes
    uint32_t ai_core_count = 0;     // Number of AI Cores
    uint32_t ai_cpu_count = 0;      // Number of AI CPUs
    std::string driver_version;
    std::string cann_version;
};

/**
 * Ascend NPU Profiler using CANN ACL Profiling API
 * 
 * Usage:
 *   AscendProfiler profiler;
 *   AscendProfilerConfig config;
 *   profiler.configure(config);
 *   ProfilerConfig prof_config;
 *   profiler.initialize(prof_config);
 *   profiler.startCapture();
 *   // ... run NPU operations ...
 *   profiler.stopCapture();
 *   std::vector<TraceEvent> events;
 *   profiler.getEvents(events);
 */
class AscendProfiler : public IPlatformProfiler {
public:
    AscendProfiler();
    ~AscendProfiler() override;
    
    /// Configure Ascend-specific profiler settings
    void configure(const AscendProfilerConfig& config);
    
    /// IPlatformProfiler interface
    PlatformType platformType() const override { return PlatformType::Ascend; }
    bool isAvailable() const override;
    bool initialize(const ProfilerConfig& config) override;
    void finalize() override;
    bool startCapture() override;
    bool stopCapture() override;
    bool isCapturing() const override { return is_running_; }
    size_t getEvents(std::vector<TraceEvent>& events, size_t max_count = 0) override;
    std::vector<DeviceInfo> getDeviceInfo() const override;
    
    /// Event callback (not used for Ascend profiling)
    void setEventCallback(EventCallback callback) override { event_callback_ = callback; }
    uint64_t eventsCaptured() const override { return stats_.total_events; }
    uint64_t eventsDropped() const override { return 0; }
    
    /// Get profiling statistics
    struct Statistics {
        uint64_t total_events = 0;
        uint64_t kernel_count = 0;
        uint64_t memcpy_count = 0;
        uint64_t hccl_count = 0;
        double total_kernel_time_ms = 0.0;
        double total_memcpy_time_ms = 0.0;
    };
    Statistics get_statistics() const;
    
    /// Static utility functions
    static bool is_available();
    static std::string get_cann_version();
    static uint32_t get_device_count();
    static AscendDeviceInfo get_device_info(uint32_t device_id);
    static std::vector<AscendDeviceInfo> get_all_device_info();
    
    /// Parse msprof output directory
    bool parse_profiling_output(const std::string& output_dir);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    AscendProfilerConfig config_;
    std::vector<TraceEvent> events_;
    std::atomic<bool> is_running_{false};
    Statistics stats_;
    EventCallback event_callback_;
    
#ifdef TRACESMITH_ENABLE_ASCEND
    aclprofConfig* prof_config_ = nullptr;
    
    uint64_t build_data_type_config() const;
    void parse_summary_file(const std::string& filepath);
    void parse_timeline_file(const std::string& filepath);
#endif
};

/// Check if Ascend/CANN is available
bool is_ascend_available();

/// Get CANN version string
std::string get_cann_version();

/// Get number of Ascend NPU devices
uint32_t get_ascend_device_count();

} // namespace tracesmith
