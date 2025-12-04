#pragma once

/**
 * GPU Memory Profiler
 * 
 * Comprehensive GPU memory tracking and analysis:
 * - Real-time allocation/deallocation monitoring
 * - Memory usage timeline
 * - Leak detection
 * - Fragmentation analysis
 * - Peak memory tracking
 * 
 * Usage:
 *   MemoryProfiler profiler;
 *   profiler.start();
 *   // ... GPU operations ...
 *   profiler.stop();
 *   auto report = profiler.generateReport();
 */

#include "tracesmith/common/types.hpp"
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <string>
#include <functional>

namespace tracesmith {

/// Memory allocation record
struct MemoryAllocation {
    uint64_t ptr;                   // Memory address
    uint64_t size;                  // Allocation size in bytes
    uint32_t device_id;             // GPU device
    Timestamp alloc_time;           // When allocated
    Timestamp free_time;            // When freed (0 if still live)
    std::string allocator;          // Allocator name
    std::string tag;                // User-defined tag
    uint32_t call_stack_hash;       // Hash of allocation call stack
    
    bool is_live() const { return free_time == 0; }
    uint64_t lifetime_ns() const { 
        return free_time > alloc_time ? free_time - alloc_time : 0; 
    }
};

/// Memory usage snapshot at a point in time
struct MemorySnapshot {
    Timestamp timestamp;
    uint64_t total_allocated;       // Total bytes allocated
    uint64_t total_freed;           // Total bytes freed
    uint64_t live_allocations;      // Current live allocation count
    uint64_t live_bytes;            // Current live bytes
    uint64_t peak_bytes;            // Peak memory usage so far
    
    // Per-device breakdown
    std::map<uint32_t, uint64_t> device_usage;
    
    // Per-allocator breakdown  
    std::map<std::string, uint64_t> allocator_usage;
};

/// Memory fragmentation info
struct FragmentationInfo {
    uint64_t total_free_bytes;      // Total free memory
    uint64_t largest_free_block;    // Largest contiguous free block
    uint64_t free_block_count;      // Number of free blocks
    double fragmentation_ratio;     // 1 - (largest_free / total_free)
};

/// Potential memory leak
struct MemoryLeak {
    uint64_t ptr;
    uint64_t size;
    Timestamp alloc_time;
    std::string allocator;
    std::string tag;
    uint64_t lifetime_ns;           // How long it's been allocated
};

/// Memory profiler report
struct MemoryReport {
    // Summary
    uint64_t total_allocations;
    uint64_t total_frees;
    uint64_t total_bytes_allocated;
    uint64_t total_bytes_freed;
    uint64_t peak_memory_usage;
    uint64_t current_memory_usage;
    Timestamp profile_duration_ns;
    
    // Allocation statistics
    uint64_t min_allocation_size;
    uint64_t max_allocation_size;
    double avg_allocation_size;
    double avg_allocation_lifetime_ns;
    
    // Potential issues
    std::vector<MemoryLeak> potential_leaks;
    FragmentationInfo fragmentation;
    
    // Timeline (sampled snapshots)
    std::vector<MemorySnapshot> timeline;
    
    // Hot spots (most frequent allocation sizes)
    std::map<uint64_t, uint64_t> allocation_size_histogram;
    
    // Per-allocator stats
    struct AllocatorStats {
        uint64_t allocations;
        uint64_t frees;
        uint64_t bytes_allocated;
        uint64_t bytes_freed;
        uint64_t current_usage;
    };
    std::map<std::string, AllocatorStats> allocator_stats;
    
    /// Generate text summary
    std::string summary() const;
    
    /// Export to JSON
    std::string toJSON() const;
};

/// Memory event callback
using MemoryEventCallback = std::function<void(const MemoryEvent&)>;

/// GPU Memory Profiler
class MemoryProfiler {
public:
    /// Configuration
    struct Config {
        uint32_t snapshot_interval_ms = 100;  // Timeline snapshot interval
        uint64_t leak_threshold_ns = 5000000000ULL;  // 5 seconds
        bool track_call_stacks = false;
        bool detect_double_free = true;
        size_t max_timeline_samples = 1000;
        
        Config() = default;
    };
    
    MemoryProfiler();
    explicit MemoryProfiler(const Config& config);
    ~MemoryProfiler();
    
    // Non-copyable
    MemoryProfiler(const MemoryProfiler&) = delete;
    MemoryProfiler& operator=(const MemoryProfiler&) = delete;
    
    /// Start profiling
    void start();
    
    /// Stop profiling
    void stop();
    
    /// Check if profiling is active
    bool isActive() const { return active_.load(); }
    
    /// Record memory allocation
    void recordAlloc(uint64_t ptr, uint64_t size, uint32_t device_id = 0,
                     const std::string& allocator = "default",
                     const std::string& tag = "");
    
    /// Record memory free
    void recordFree(uint64_t ptr, uint32_t device_id = 0);
    
    /// Record a MemoryEvent (for integration with existing profilers)
    void recordEvent(const MemoryEvent& event);
    
    /// Get current memory usage
    uint64_t getCurrentUsage() const { return current_usage_.load(); }
    
    /// Get peak memory usage
    uint64_t getPeakUsage() const { return peak_usage_.load(); }
    
    /// Get live allocation count
    uint64_t getLiveAllocationCount() const;
    
    /// Get all live allocations
    std::vector<MemoryAllocation> getLiveAllocations() const;
    
    /// Take a memory snapshot
    MemorySnapshot takeSnapshot() const;
    
    /// Generate full report
    MemoryReport generateReport() const;
    
    /// Clear all recorded data
    void clear();
    
    /// Set callback for memory events
    void setCallback(MemoryEventCallback callback) { callback_ = std::move(callback); }
    
    /// Convert to CounterEvents for Perfetto export
    std::vector<CounterEvent> toCounterEvents() const;
    
    /// Convert to MemoryEvents
    std::vector<MemoryEvent> toMemoryEvents() const;
    
    /// Detect potential memory leaks based on leak threshold
    std::vector<MemoryLeak> detectLeaks() const;
    
private:
    Config config_;
    std::atomic<bool> active_{false};
    Timestamp start_time_ = 0;
    Timestamp stop_time_ = 0;
    
    // Allocation tracking
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, MemoryAllocation> live_allocations_;
    std::vector<MemoryAllocation> freed_allocations_;
    
    // Statistics
    std::atomic<uint64_t> current_usage_{0};
    std::atomic<uint64_t> peak_usage_{0};
    std::atomic<uint64_t> total_allocations_{0};
    std::atomic<uint64_t> total_frees_{0};
    std::atomic<uint64_t> total_bytes_allocated_{0};
    std::atomic<uint64_t> total_bytes_freed_{0};
    
    // Timeline
    std::vector<MemorySnapshot> timeline_;
    
    // Callback
    MemoryEventCallback callback_;
    
    // Internal helpers
    void updatePeakUsage();
    void takeTimelineSnapshot();
    FragmentationInfo calculateFragmentation() const;
};

// ============================================================================
// Utility Functions
// ============================================================================

/// Format bytes to human-readable string
inline std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit_idx]);
    return buf;
}

/// Format nanoseconds to human-readable string
inline std::string formatDuration(uint64_t ns) {
    if (ns < 1000) return std::to_string(ns) + " ns";
    if (ns < 1000000) return std::to_string(ns / 1000) + " µs";
    if (ns < 1000000000) return std::to_string(ns / 1000000) + " ms";
    return std::to_string(ns / 1000000000) + " s";
}

} // namespace tracesmith

