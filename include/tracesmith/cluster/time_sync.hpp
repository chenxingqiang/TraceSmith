#pragma once

#include "tracesmith/common/types.hpp"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tracesmith::cluster {

/// Time synchronization method
enum class TimeSyncMethod {
    SystemClock,    // Use system clock (basic)
    NTP,            // Network Time Protocol (~1ms accuracy)
    PTP,            // Precision Time Protocol (~1µs accuracy)
    CUDA,           // CUDA event timestamps (GPU-local, NVIDIA)
    MACA,           // MACA event timestamps (GPU-local, MetaX)
    Custom          // Custom sync protocol
};

/// Time sync configuration
struct TimeSyncConfig {
    TimeSyncMethod method = TimeSyncMethod::SystemClock;
    std::string ntp_server = "pool.ntp.org";
    std::string ptp_interface = "eth0";
    uint32_t sync_interval_ms = 1000;
    int64_t max_acceptable_offset_ns = 1000000;  // 1ms default
};

/// Time synchronization result
struct SyncResult {
    bool success = false;
    int64_t offset_ns = 0;          // Offset from reference
    int64_t round_trip_ns = 0;      // RTT (for network methods)
    double uncertainty_ns = 0.0;    // Estimated uncertainty
    Timestamp sync_time = 0;        // When sync was performed
    std::string error_message;
};

/// Time synchronization manager
class TimeSync {
public:
    explicit TimeSync(const TimeSyncConfig& config = {});
    ~TimeSync();
    
    // Initialization
    bool initialize();
    void finalize();
    bool isInitialized() const { return initialized_; }
    
    // Get configuration
    const TimeSyncConfig& getConfig() const { return config_; }
    
    // Synchronization
    SyncResult synchronize();
    SyncResult synchronizeWithNode(const std::string& node_id);
    
    // Timestamp conversion
    Timestamp toSynchronizedTime(Timestamp local_time) const;
    Timestamp toLocalTime(Timestamp sync_time) const;
    
    // Offset management
    int64_t getCurrentOffset() const { return current_offset_.load(); }
    void setManualOffset(int64_t offset_ns);
    
    // GPU timestamp correlation
    bool correlateGPUTimestamps(uint32_t gpu_id);
    int64_t getGPUOffset(uint32_t gpu_id) const;
    void setGPUOffset(uint32_t gpu_id, int64_t offset_ns);
    
    // Statistics
    double getAverageOffset() const;
    double getOffsetStdDev() const;
    size_t getSyncCount() const { return sync_history_.size(); }
    
    // Get last sync result
    SyncResult getLastSyncResult() const;
    
    // Clear history
    void clearHistory();
    
private:
    SyncResult syncSystemClock();
    SyncResult syncNTP();
    SyncResult syncPTP();
    SyncResult syncCUDA(uint32_t gpu_id);
    SyncResult syncMACA(uint32_t gpu_id);
    
    TimeSyncConfig config_;
    std::atomic<int64_t> current_offset_{0};
    std::map<uint32_t, int64_t> gpu_offsets_;
    std::vector<SyncResult> sync_history_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

/// Correlate timestamps from different sources
class ClockCorrelator {
public:
    ClockCorrelator();
    
    /// Correlation point
    struct CorrelationPoint {
        Timestamp source_time;
        Timestamp reference_time;
        Timestamp recorded_at;
    };
    
    // Add correlation points
    void addCorrelationPoint(
        const std::string& source_id,
        Timestamp source_time,
        Timestamp reference_time
    );
    
    // Get correlation points
    std::vector<CorrelationPoint> getCorrelationPoints(const std::string& source_id) const;
    
    // Calculate offset
    int64_t calculateOffset(const std::string& source_id) const;
    
    // Apply correction to events
    void correctTimestamps(
        const std::string& source_id,
        std::vector<TraceEvent>& events
    );
    
    // Linear regression for drift compensation
    struct DriftModel {
        double offset = 0.0;        // Base offset (ns)
        double drift_rate = 0.0;    // ns per second
        double r_squared = 0.0;     // Model quality (0-1)
        bool valid = false;
    };
    DriftModel calculateDriftModel(const std::string& source_id) const;
    
    // Apply drift correction
    Timestamp applyDriftCorrection(
        const std::string& source_id,
        Timestamp source_time
    ) const;
    
    // Clear correlation data
    void clear();
    void clearSource(const std::string& source_id);
    
private:
    std::map<std::string, std::vector<CorrelationPoint>> correlation_data_;
    mutable std::map<std::string, DriftModel> cached_models_;
    mutable std::mutex mutex_;
};

// Utility functions
const char* timeSyncMethodToString(TimeSyncMethod method);
TimeSyncMethod stringToTimeSyncMethod(const std::string& str);

} // namespace tracesmith::cluster

