#include "tracesmith/cluster/time_sync.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>

#ifdef TRACESMITH_ENABLE_CUDA
#include <cuda_runtime.h>
#endif

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/maca.h>
#include <mcr/mc_runtime_api.h>
#endif

namespace tracesmith::cluster {

// =============================================================================
// TimeSync Implementation
// =============================================================================

TimeSync::TimeSync(const TimeSyncConfig& config)
    : config_(config) {
}

TimeSync::~TimeSync() {
    finalize();
}

bool TimeSync::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    // Perform initial synchronization
    SyncResult result;
    switch (config_.method) {
        case TimeSyncMethod::SystemClock:
            result = syncSystemClock();
            break;
        case TimeSyncMethod::NTP:
            result = syncNTP();
            break;
        case TimeSyncMethod::PTP:
            result = syncPTP();
            break;
        case TimeSyncMethod::CUDA:
            result = syncCUDA(0);
            break;
        case TimeSyncMethod::MACA:
            result = syncMACA(0);
            break;
        default:
            result = syncSystemClock();
            break;
    }
    
    initialized_ = result.success;
    return initialized_;
}

void TimeSync::finalize() {
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
    sync_history_.clear();
    gpu_offsets_.clear();
    current_offset_.store(0);
}

SyncResult TimeSync::synchronize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SyncResult result;
    switch (config_.method) {
        case TimeSyncMethod::SystemClock:
            result = syncSystemClock();
            break;
        case TimeSyncMethod::NTP:
            result = syncNTP();
            break;
        case TimeSyncMethod::PTP:
            result = syncPTP();
            break;
        case TimeSyncMethod::CUDA:
            result = syncCUDA(0);
            break;
        case TimeSyncMethod::MACA:
            result = syncMACA(0);
            break;
        default:
            result = syncSystemClock();
            break;
    }
    
    if (result.success) {
        current_offset_.store(result.offset_ns);
        sync_history_.push_back(result);
    }
    
    return result;
}

SyncResult TimeSync::synchronizeWithNode(const std::string& node_id) {
    // For now, use system clock sync
    // In future, implement network-based synchronization with specific node
    (void)node_id;
    return synchronize();
}

Timestamp TimeSync::toSynchronizedTime(Timestamp local_time) const {
    int64_t offset = current_offset_.load();
    return local_time + offset;
}

Timestamp TimeSync::toLocalTime(Timestamp sync_time) const {
    int64_t offset = current_offset_.load();
    return sync_time - offset;
}

void TimeSync::setManualOffset(int64_t offset_ns) {
    current_offset_.store(offset_ns);
}

bool TimeSync::correlateGPUTimestamps(uint32_t gpu_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
#ifdef TRACESMITH_ENABLE_CUDA
    // Set device
    cudaError_t err = cudaSetDevice(gpu_id);
    if (err != cudaSuccess) {
        return false;
    }
    
    // Create CUDA events
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    
    // Record CPU time before
    auto cpu_before = std::chrono::high_resolution_clock::now();
    
    // Record GPU events
    cudaEventRecord(start);
    cudaEventSynchronize(start);
    
    // Record CPU time after
    auto cpu_after = std::chrono::high_resolution_clock::now();
    
    // Calculate offset (GPU time relative to CPU time)
    auto cpu_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        (cpu_before.time_since_epoch() + cpu_after.time_since_epoch()) / 2
    ).count();
    
    // Store offset
    gpu_offsets_[gpu_id] = 0;  // GPU events are already relative to CPU
    
    // Cleanup
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    
    return true;
#else
    (void)gpu_id;
    gpu_offsets_[gpu_id] = 0;
    return true;
#endif
}

int64_t TimeSync::getGPUOffset(uint32_t gpu_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gpu_offsets_.find(gpu_id);
    if (it != gpu_offsets_.end()) {
        return it->second;
    }
    return 0;
}

void TimeSync::setGPUOffset(uint32_t gpu_id, int64_t offset_ns) {
    std::lock_guard<std::mutex> lock(mutex_);
    gpu_offsets_[gpu_id] = offset_ns;
}

double TimeSync::getAverageOffset() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (sync_history_.empty()) {
        return 0.0;
    }
    
    double sum = 0.0;
    for (const auto& result : sync_history_) {
        sum += result.offset_ns;
    }
    return sum / sync_history_.size();
}

double TimeSync::getOffsetStdDev() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (sync_history_.size() < 2) {
        return 0.0;
    }
    
    double mean = getAverageOffset();
    double sum_sq = 0.0;
    for (const auto& result : sync_history_) {
        double diff = result.offset_ns - mean;
        sum_sq += diff * diff;
    }
    return std::sqrt(sum_sq / (sync_history_.size() - 1));
}

SyncResult TimeSync::getLastSyncResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (sync_history_.empty()) {
        return SyncResult{};
    }
    return sync_history_.back();
}

void TimeSync::clearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    sync_history_.clear();
}

SyncResult TimeSync::syncSystemClock() {
    SyncResult result;
    result.success = true;
    result.offset_ns = 0;  // System clock is the reference
    result.round_trip_ns = 0;
    result.uncertainty_ns = 1000.0;  // ~1µs uncertainty
    result.sync_time = getCurrentTimestamp();
    return result;
}

SyncResult TimeSync::syncNTP() {
    // Simplified NTP implementation
    // In production, use actual NTP client
    SyncResult result;
    result.success = true;
    result.offset_ns = 0;  // Placeholder
    result.round_trip_ns = 0;
    result.uncertainty_ns = 1000000.0;  // ~1ms NTP uncertainty
    result.sync_time = getCurrentTimestamp();
    result.error_message = "NTP sync not fully implemented, using system clock";
    return result;
}

SyncResult TimeSync::syncPTP() {
    // PTP requires hardware support
    // Return system clock as fallback
    SyncResult result;
    result.success = true;
    result.offset_ns = 0;
    result.round_trip_ns = 0;
    result.uncertainty_ns = 1000.0;  // PTP can achieve <1µs
    result.sync_time = getCurrentTimestamp();
    result.error_message = "PTP sync not fully implemented, using system clock";
    return result;
}

SyncResult TimeSync::syncCUDA(uint32_t gpu_id) {
    SyncResult result;
    
#ifdef TRACESMITH_ENABLE_CUDA
    cudaError_t err = cudaSetDevice(gpu_id);
    if (err != cudaSuccess) {
        result.success = false;
        result.error_message = "Failed to set CUDA device";
        return result;
    }
    
    // Synchronize to establish baseline
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        result.success = false;
        result.error_message = "CUDA device synchronization failed";
        return result;
    }
    
    result.success = true;
    result.offset_ns = 0;
    result.uncertainty_ns = 100.0;  // CUDA timestamps are very precise
    result.sync_time = getCurrentTimestamp();
#else
    result.success = false;
    result.error_message = "CUDA not available";
    (void)gpu_id;
#endif
    
    return result;
}

SyncResult TimeSync::syncMACA(uint32_t gpu_id) {
    SyncResult result;
    
#ifdef TRACESMITH_ENABLE_MACA
    mcError_t err = mcSetDevice(gpu_id);
    if (err != mcSuccess) {
        result.success = false;
        result.error_message = "Failed to set MACA device";
        return result;
    }
    
    // Synchronize to establish baseline
    err = mcDeviceSynchronize();
    if (err != mcSuccess) {
        result.success = false;
        result.error_message = "MACA device synchronization failed";
        return result;
    }
    
    result.success = true;
    result.offset_ns = 0;
    result.uncertainty_ns = 100.0;  // MACA timestamps precision
    result.sync_time = getCurrentTimestamp();
#else
    result.success = false;
    result.error_message = "MACA not available";
    (void)gpu_id;
#endif
    
    return result;
}

// =============================================================================
// ClockCorrelator Implementation
// =============================================================================

ClockCorrelator::ClockCorrelator() = default;

void ClockCorrelator::addCorrelationPoint(
    const std::string& source_id,
    Timestamp source_time,
    Timestamp reference_time
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CorrelationPoint point;
    point.source_time = source_time;
    point.reference_time = reference_time;
    point.recorded_at = getCurrentTimestamp();
    
    correlation_data_[source_id].push_back(point);
    
    // Invalidate cached model
    cached_models_.erase(source_id);
}

std::vector<ClockCorrelator::CorrelationPoint> 
ClockCorrelator::getCorrelationPoints(const std::string& source_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = correlation_data_.find(source_id);
    if (it != correlation_data_.end()) {
        return it->second;
    }
    return {};
}

int64_t ClockCorrelator::calculateOffset(const std::string& source_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = correlation_data_.find(source_id);
    if (it == correlation_data_.end() || it->second.empty()) {
        return 0;
    }
    
    // Calculate average offset
    const auto& points = it->second;
    int64_t sum = 0;
    for (const auto& point : points) {
        sum += (point.reference_time - point.source_time);
    }
    return sum / static_cast<int64_t>(points.size());
}

void ClockCorrelator::correctTimestamps(
    const std::string& source_id,
    std::vector<TraceEvent>& events
) {
    auto model = calculateDriftModel(source_id);
    
    if (!model.valid) {
        // Use simple offset correction
        int64_t offset = calculateOffset(source_id);
        for (auto& event : events) {
            event.timestamp += offset;
        }
        return;
    }
    
    // Apply drift correction
    for (auto& event : events) {
        event.timestamp = applyDriftCorrection(source_id, event.timestamp);
    }
}

ClockCorrelator::DriftModel ClockCorrelator::calculateDriftModel(
    const std::string& source_id
) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check cache
    auto cache_it = cached_models_.find(source_id);
    if (cache_it != cached_models_.end()) {
        return cache_it->second;
    }
    
    DriftModel model;
    
    auto it = correlation_data_.find(source_id);
    if (it == correlation_data_.end() || it->second.size() < 2) {
        return model;
    }
    
    const auto& points = it->second;
    
    // Linear regression: reference_time = offset + drift_rate * source_time
    // Using least squares
    
    double n = static_cast<double>(points.size());
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    
    for (const auto& point : points) {
        double x = static_cast<double>(point.source_time) / 1e9;  // Convert to seconds
        double y = static_cast<double>(point.reference_time - point.source_time);
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
    }
    
    double denom = n * sum_xx - sum_x * sum_x;
    if (std::abs(denom) < 1e-10) {
        return model;
    }
    
    model.drift_rate = (n * sum_xy - sum_x * sum_y) / denom;
    model.offset = (sum_y - model.drift_rate * sum_x) / n;
    
    // Calculate R-squared
    double mean_y = sum_y / n;
    double ss_tot = 0, ss_res = 0;
    
    for (const auto& point : points) {
        double x = static_cast<double>(point.source_time) / 1e9;
        double y = static_cast<double>(point.reference_time - point.source_time);
        double y_pred = model.offset + model.drift_rate * x;
        
        ss_tot += (y - mean_y) * (y - mean_y);
        ss_res += (y - y_pred) * (y - y_pred);
    }
    
    model.r_squared = (ss_tot > 0) ? 1.0 - (ss_res / ss_tot) : 0.0;
    model.valid = true;
    
    // Cache the model
    cached_models_[source_id] = model;
    
    return model;
}

Timestamp ClockCorrelator::applyDriftCorrection(
    const std::string& source_id,
    Timestamp source_time
) const {
    auto model = calculateDriftModel(source_id);
    
    if (!model.valid) {
        return source_time + calculateOffset(source_id);
    }
    
    double t_sec = static_cast<double>(source_time) / 1e9;
    double correction = model.offset + model.drift_rate * t_sec;
    
    return source_time + static_cast<int64_t>(correction);
}

void ClockCorrelator::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    correlation_data_.clear();
    cached_models_.clear();
}

void ClockCorrelator::clearSource(const std::string& source_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    correlation_data_.erase(source_id);
    cached_models_.erase(source_id);
}

// =============================================================================
// Utility Functions
// =============================================================================

const char* timeSyncMethodToString(TimeSyncMethod method) {
    switch (method) {
        case TimeSyncMethod::SystemClock: return "SystemClock";
        case TimeSyncMethod::NTP: return "NTP";
        case TimeSyncMethod::PTP: return "PTP";
        case TimeSyncMethod::CUDA: return "CUDA";
        case TimeSyncMethod::MACA: return "MACA";
        case TimeSyncMethod::Custom: return "Custom";
        default: return "Unknown";
    }
}

TimeSyncMethod stringToTimeSyncMethod(const std::string& str) {
    if (str == "SystemClock" || str == "system") return TimeSyncMethod::SystemClock;
    if (str == "NTP" || str == "ntp") return TimeSyncMethod::NTP;
    if (str == "PTP" || str == "ptp") return TimeSyncMethod::PTP;
    if (str == "CUDA" || str == "cuda") return TimeSyncMethod::CUDA;
    if (str == "MACA" || str == "maca") return TimeSyncMethod::MACA;
    if (str == "Custom" || str == "custom") return TimeSyncMethod::Custom;
    return TimeSyncMethod::SystemClock;
}

} // namespace tracesmith::cluster

