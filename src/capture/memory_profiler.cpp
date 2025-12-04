/**
 * GPU Memory Profiler Implementation
 */

#include "tracesmith/capture/memory_profiler.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace tracesmith {

// ============================================================================
// MemoryProfiler Implementation
// ============================================================================

MemoryProfiler::MemoryProfiler() : config_() {}

MemoryProfiler::MemoryProfiler(const Config& config) : config_(config) {}

MemoryProfiler::~MemoryProfiler() {
    if (active_.load()) {
        stop();
    }
}

void MemoryProfiler::start() {
    if (active_.exchange(true)) {
        return; // Already active
    }
    
    start_time_ = getCurrentTimestamp();
    stop_time_ = 0;
    
    // Take initial snapshot
    takeTimelineSnapshot();
}

void MemoryProfiler::stop() {
    if (!active_.exchange(false)) {
        return; // Already stopped
    }
    
    stop_time_ = getCurrentTimestamp();
    
    // Take final snapshot
    takeTimelineSnapshot();
}

void MemoryProfiler::recordAlloc(uint64_t ptr, uint64_t size, uint32_t device_id,
                                  const std::string& allocator,
                                  const std::string& tag) {
    if (!active_.load()) return;
    
    MemoryAllocation alloc;
    alloc.ptr = ptr;
    alloc.size = size;
    alloc.device_id = device_id;
    alloc.alloc_time = getCurrentTimestamp();
    alloc.free_time = 0;
    alloc.allocator = allocator;
    alloc.tag = tag;
    alloc.call_stack_hash = 0;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        live_allocations_[ptr] = alloc;
    }
    
    // Update statistics
    current_usage_.fetch_add(size);
    total_allocations_.fetch_add(1);
    total_bytes_allocated_.fetch_add(size);
    updatePeakUsage();
    
    // Callback
    if (callback_) {
        MemoryEvent event;
        event.timestamp = alloc.alloc_time;
        event.device_id = device_id;
        event.bytes = size;
        event.ptr = ptr;
        event.is_allocation = true;
        event.allocator_name = allocator;
        callback_(event);
    }
}

void MemoryProfiler::recordFree(uint64_t ptr, uint32_t device_id) {
    if (!active_.load()) return;
    
    Timestamp free_time = getCurrentTimestamp();
    uint64_t size = 0;
    std::string allocator;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = live_allocations_.find(ptr);
        if (it == live_allocations_.end()) {
            // Double free or unknown allocation
            if (config_.detect_double_free) {
                // Could log warning here
            }
            return;
        }
        
        it->second.free_time = free_time;
        size = it->second.size;
        allocator = it->second.allocator;
        
        // Move to freed list
        freed_allocations_.push_back(std::move(it->second));
        live_allocations_.erase(it);
    }
    
    // Update statistics
    current_usage_.fetch_sub(size);
    total_frees_.fetch_add(1);
    total_bytes_freed_.fetch_add(size);
    
    // Callback
    if (callback_) {
        MemoryEvent event;
        event.timestamp = free_time;
        event.device_id = device_id;
        event.bytes = size;
        event.ptr = ptr;
        event.is_allocation = false;
        event.allocator_name = allocator;
        callback_(event);
    }
}

void MemoryProfiler::recordEvent(const MemoryEvent& event) {
    if (event.is_allocation) {
        recordAlloc(event.ptr, event.bytes, event.device_id, 
                   event.allocator_name);
    } else {
        recordFree(event.ptr, event.device_id);
    }
}

uint64_t MemoryProfiler::getLiveAllocationCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return live_allocations_.size();
}

std::vector<MemoryAllocation> MemoryProfiler::getLiveAllocations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<MemoryAllocation> result;
    result.reserve(live_allocations_.size());
    
    for (const auto& [ptr, alloc] : live_allocations_) {
        result.push_back(alloc);
    }
    
    return result;
}

MemorySnapshot MemoryProfiler::takeSnapshot() const {
    MemorySnapshot snapshot;
    snapshot.timestamp = getCurrentTimestamp();
    snapshot.total_allocated = total_bytes_allocated_.load();
    snapshot.total_freed = total_bytes_freed_.load();
    snapshot.live_bytes = current_usage_.load();
    snapshot.peak_bytes = peak_usage_.load();
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.live_allocations = live_allocations_.size();
        
        // Per-device usage
        for (const auto& [ptr, alloc] : live_allocations_) {
            snapshot.device_usage[alloc.device_id] += alloc.size;
            snapshot.allocator_usage[alloc.allocator] += alloc.size;
        }
    }
    
    return snapshot;
}

void MemoryProfiler::takeTimelineSnapshot() {
    if (timeline_.size() >= config_.max_timeline_samples) {
        // Remove oldest samples if we hit the limit
        timeline_.erase(timeline_.begin());
    }
    
    timeline_.push_back(takeSnapshot());
}

void MemoryProfiler::updatePeakUsage() {
    uint64_t current = current_usage_.load();
    uint64_t peak = peak_usage_.load();
    
    while (current > peak) {
        if (peak_usage_.compare_exchange_weak(peak, current)) {
            break;
        }
    }
}

FragmentationInfo MemoryProfiler::calculateFragmentation() const {
    FragmentationInfo info;
    info.total_free_bytes = 0;
    info.largest_free_block = 0;
    info.free_block_count = 0;
    info.fragmentation_ratio = 0.0;
    
    // In a real implementation, this would query the GPU allocator
    // For now, we estimate based on allocation patterns
    
    return info;
}

std::vector<MemoryLeak> MemoryProfiler::detectLeaks() const {
    std::vector<MemoryLeak> leaks;
    Timestamp now = getCurrentTimestamp();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [ptr, alloc] : live_allocations_) {
        uint64_t lifetime = now - alloc.alloc_time;
        
        if (lifetime > config_.leak_threshold_ns) {
            MemoryLeak leak;
            leak.ptr = alloc.ptr;
            leak.size = alloc.size;
            leak.alloc_time = alloc.alloc_time;
            leak.allocator = alloc.allocator;
            leak.tag = alloc.tag;
            leak.lifetime_ns = lifetime;
            leaks.push_back(leak);
        }
    }
    
    // Sort by lifetime (longest first)
    std::sort(leaks.begin(), leaks.end(), 
              [](const MemoryLeak& a, const MemoryLeak& b) {
                  return a.lifetime_ns > b.lifetime_ns;
              });
    
    return leaks;
}

MemoryReport MemoryProfiler::generateReport() const {
    MemoryReport report;
    
    // Summary
    report.total_allocations = total_allocations_.load();
    report.total_frees = total_frees_.load();
    report.total_bytes_allocated = total_bytes_allocated_.load();
    report.total_bytes_freed = total_bytes_freed_.load();
    report.peak_memory_usage = peak_usage_.load();
    report.current_memory_usage = current_usage_.load();
    
    Timestamp end_time = stop_time_ > 0 ? stop_time_ : getCurrentTimestamp();
    report.profile_duration_ns = end_time - start_time_;
    
    // Allocation statistics
    report.min_allocation_size = UINT64_MAX;
    report.max_allocation_size = 0;
    uint64_t total_size = 0;
    uint64_t total_lifetime = 0;
    size_t count = 0;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Process live allocations
        for (const auto& [ptr, alloc] : live_allocations_) {
            report.min_allocation_size = std::min(report.min_allocation_size, alloc.size);
            report.max_allocation_size = std::max(report.max_allocation_size, alloc.size);
            total_size += alloc.size;
            
            // Histogram
            uint64_t bucket = alloc.size;
            // Round to nearest power of 2 for histogram
            uint64_t power = 1;
            while (power < bucket) power *= 2;
            report.allocation_size_histogram[power]++;
            
            // Per-allocator stats
            auto& stats = report.allocator_stats[alloc.allocator];
            stats.allocations++;
            stats.bytes_allocated += alloc.size;
            stats.current_usage += alloc.size;
            
            count++;
        }
        
        // Process freed allocations
        for (const auto& alloc : freed_allocations_) {
            report.min_allocation_size = std::min(report.min_allocation_size, alloc.size);
            report.max_allocation_size = std::max(report.max_allocation_size, alloc.size);
            total_size += alloc.size;
            total_lifetime += alloc.lifetime_ns();
            
            // Histogram
            uint64_t bucket = alloc.size;
            uint64_t power = 1;
            while (power < bucket) power *= 2;
            report.allocation_size_histogram[power]++;
            
            // Per-allocator stats
            auto& stats = report.allocator_stats[alloc.allocator];
            stats.frees++;
            stats.bytes_freed += alloc.size;
            
            count++;
        }
    }
    
    if (count > 0) {
        report.avg_allocation_size = static_cast<double>(total_size) / count;
    }
    
    if (total_frees_.load() > 0) {
        report.avg_allocation_lifetime_ns = 
            static_cast<double>(total_lifetime) / total_frees_.load();
    }
    
    if (report.min_allocation_size == UINT64_MAX) {
        report.min_allocation_size = 0;
    }
    
    // Potential issues
    report.potential_leaks = detectLeaks();
    report.fragmentation = calculateFragmentation();
    
    // Timeline
    report.timeline = timeline_;
    
    return report;
}

void MemoryProfiler::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    live_allocations_.clear();
    freed_allocations_.clear();
    timeline_.clear();
    
    current_usage_.store(0);
    peak_usage_.store(0);
    total_allocations_.store(0);
    total_frees_.store(0);
    total_bytes_allocated_.store(0);
    total_bytes_freed_.store(0);
    
    start_time_ = 0;
    stop_time_ = 0;
}

std::vector<CounterEvent> MemoryProfiler::toCounterEvents() const {
    std::vector<CounterEvent> events;
    
    for (const auto& snapshot : timeline_) {
        // Memory usage counter
        events.emplace_back("GPU Memory Usage", 
                           static_cast<double>(snapshot.live_bytes) / (1024*1024*1024),
                           snapshot.timestamp, "GB");
        
        // Live allocations counter
        events.emplace_back("Live Allocations",
                           static_cast<double>(snapshot.live_allocations),
                           snapshot.timestamp, "count");
        
        // Peak memory counter
        events.emplace_back("Peak Memory",
                           static_cast<double>(snapshot.peak_bytes) / (1024*1024*1024),
                           snapshot.timestamp, "GB");
    }
    
    return events;
}

std::vector<MemoryEvent> MemoryProfiler::toMemoryEvents() const {
    std::vector<MemoryEvent> events;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Live allocations
    for (const auto& [ptr, alloc] : live_allocations_) {
        MemoryEvent event;
        event.timestamp = alloc.alloc_time;
        event.device_id = alloc.device_id;
        event.bytes = alloc.size;
        event.ptr = alloc.ptr;
        event.is_allocation = true;
        event.allocator_name = alloc.allocator;
        events.push_back(event);
    }
    
    // Freed allocations (both alloc and free events)
    for (const auto& alloc : freed_allocations_) {
        // Allocation event
        MemoryEvent alloc_event;
        alloc_event.timestamp = alloc.alloc_time;
        alloc_event.device_id = alloc.device_id;
        alloc_event.bytes = alloc.size;
        alloc_event.ptr = alloc.ptr;
        alloc_event.is_allocation = true;
        alloc_event.allocator_name = alloc.allocator;
        events.push_back(alloc_event);
        
        // Free event
        MemoryEvent free_event;
        free_event.timestamp = alloc.free_time;
        free_event.device_id = alloc.device_id;
        free_event.bytes = alloc.size;
        free_event.ptr = alloc.ptr;
        free_event.is_allocation = false;
        free_event.allocator_name = alloc.allocator;
        events.push_back(free_event);
    }
    
    // Sort by timestamp
    std::sort(events.begin(), events.end(),
              [](const MemoryEvent& a, const MemoryEvent& b) {
                  return a.timestamp < b.timestamp;
              });
    
    return events;
}

// ============================================================================
// MemoryReport Implementation
// ============================================================================

std::string MemoryReport::summary() const {
    std::ostringstream oss;
    
    oss << "═══════════════════════════════════════════════════\n";
    oss << "           GPU Memory Profiler Report\n";
    oss << "═══════════════════════════════════════════════════\n\n";
    
    oss << "Summary\n";
    oss << "───────────────────────────────────────────────────\n";
    oss << "  Duration:           " << formatDuration(profile_duration_ns) << "\n";
    oss << "  Total Allocations:  " << total_allocations << "\n";
    oss << "  Total Frees:        " << total_frees << "\n";
    oss << "  Bytes Allocated:    " << formatBytes(total_bytes_allocated) << "\n";
    oss << "  Bytes Freed:        " << formatBytes(total_bytes_freed) << "\n";
    oss << "  Peak Usage:         " << formatBytes(peak_memory_usage) << "\n";
    oss << "  Current Usage:      " << formatBytes(current_memory_usage) << "\n";
    
    oss << "\nAllocation Statistics\n";
    oss << "───────────────────────────────────────────────────\n";
    oss << "  Min Size:           " << formatBytes(min_allocation_size) << "\n";
    oss << "  Max Size:           " << formatBytes(max_allocation_size) << "\n";
    oss << "  Avg Size:           " << formatBytes(static_cast<uint64_t>(avg_allocation_size)) << "\n";
    oss << "  Avg Lifetime:       " << formatDuration(static_cast<uint64_t>(avg_allocation_lifetime_ns)) << "\n";
    
    if (!potential_leaks.empty()) {
        oss << "\n⚠️  Potential Memory Leaks (" << potential_leaks.size() << ")\n";
        oss << "───────────────────────────────────────────────────\n";
        
        size_t shown = 0;
        for (const auto& leak : potential_leaks) {
            if (shown++ >= 5) {
                oss << "  ... and " << (potential_leaks.size() - 5) << " more\n";
                break;
            }
            oss << "  • 0x" << std::hex << leak.ptr << std::dec 
                << " (" << formatBytes(leak.size) << ")"
                << " - " << formatDuration(leak.lifetime_ns) << " old\n";
        }
    }
    
    if (!allocator_stats.empty()) {
        oss << "\nPer-Allocator Statistics\n";
        oss << "───────────────────────────────────────────────────\n";
        
        for (const auto& [name, stats] : allocator_stats) {
            oss << "  " << name << ":\n";
            oss << "    Allocs: " << stats.allocations 
                << ", Frees: " << stats.frees
                << ", Current: " << formatBytes(stats.current_usage) << "\n";
        }
    }
    
    oss << "\n═══════════════════════════════════════════════════\n";
    
    return oss.str();
}

std::string MemoryReport::toJSON() const {
    std::ostringstream oss;
    
    oss << "{\n";
    oss << "  \"summary\": {\n";
    oss << "    \"total_allocations\": " << total_allocations << ",\n";
    oss << "    \"total_frees\": " << total_frees << ",\n";
    oss << "    \"total_bytes_allocated\": " << total_bytes_allocated << ",\n";
    oss << "    \"total_bytes_freed\": " << total_bytes_freed << ",\n";
    oss << "    \"peak_memory_usage\": " << peak_memory_usage << ",\n";
    oss << "    \"current_memory_usage\": " << current_memory_usage << ",\n";
    oss << "    \"profile_duration_ns\": " << profile_duration_ns << "\n";
    oss << "  },\n";
    
    oss << "  \"allocation_stats\": {\n";
    oss << "    \"min_size\": " << min_allocation_size << ",\n";
    oss << "    \"max_size\": " << max_allocation_size << ",\n";
    oss << "    \"avg_size\": " << avg_allocation_size << ",\n";
    oss << "    \"avg_lifetime_ns\": " << avg_allocation_lifetime_ns << "\n";
    oss << "  },\n";
    
    oss << "  \"potential_leaks\": " << potential_leaks.size() << ",\n";
    
    oss << "  \"timeline_samples\": " << timeline.size() << "\n";
    oss << "}\n";
    
    return oss.str();
}

} // namespace tracesmith

