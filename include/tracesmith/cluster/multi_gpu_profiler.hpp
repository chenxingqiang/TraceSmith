/**
 * TraceSmith Multi-GPU Profiler
 * 
 * Provides unified profiling across multiple GPUs within a single node.
 * Supports NVML topology discovery, NVLink tracking, and event aggregation.
 */

#pragma once

#include "tracesmith/capture/profiler.hpp"
#include "tracesmith/common/types.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

namespace tracesmith {
namespace cluster {

// Forward declaration
class GPUTopology;

/**
 * Per-GPU profiler context
 */
struct GPUContext {
    uint32_t gpu_id;                                  // Logical GPU ID
    uint32_t device_index;                            // CUDA device index
    std::unique_ptr<IPlatformProfiler> profiler;      // Platform profiler
    std::vector<TraceEvent> local_events;             // Local event buffer
    std::atomic<uint64_t> event_count{0};             // Events captured
    std::atomic<uint64_t> events_dropped{0};          // Events dropped
    DeviceInfo device_info;                           // Device information
    bool active = false;                              // Profiler active state
};

/**
 * NVLink transfer record
 */
struct NVLinkTransfer {
    uint32_t src_gpu;           // Source GPU ID
    uint32_t dst_gpu;           // Destination GPU ID
    size_t bytes;               // Transfer size in bytes
    Timestamp timestamp;        // When transfer occurred
    uint64_t duration_ns;       // Transfer duration
    uint32_t link_id;           // NVLink ID (0-N)
};

/**
 * Peer memory access record
 */
struct PeerAccess {
    uint32_t src_gpu;           // GPU performing access
    uint32_t dst_gpu;           // GPU being accessed
    uint64_t address;           // Virtual address
    size_t bytes;               // Access size
    bool is_write;              // Write or read
    Timestamp timestamp;        // When access occurred
};

/**
 * Configuration for multi-GPU profiling
 */
struct MultiGPUConfig {
    std::vector<uint32_t> gpu_ids;              // GPUs to profile (empty = all)
    size_t per_gpu_buffer_size = 1024 * 1024;   // Events per GPU buffer
    bool enable_nvlink_tracking = true;         // Track NVLink transfers
    bool enable_peer_access_tracking = true;    // Track peer memory access
    uint32_t aggregation_interval_ms = 100;     // Event aggregation interval
    bool unified_timestamps = true;             // Use unified timestamp domain
    bool capture_topology = true;               // Capture GPU topology
    OverflowPolicy overflow_policy = OverflowPolicy::DropOldest;
};

/**
 * Multi-GPU profiling statistics
 */
struct MultiGPUStats {
    uint64_t total_events = 0;
    uint64_t total_dropped = 0;
    uint64_t nvlink_transfers = 0;
    uint64_t nvlink_bytes = 0;
    uint64_t peer_accesses = 0;
    std::map<uint32_t, uint64_t> events_per_gpu;
    std::map<uint32_t, uint64_t> dropped_per_gpu;
    double capture_duration_ms = 0;
};

/**
 * Multi-GPU Profiler Manager
 * 
 * Manages profiling across multiple GPUs within a single node.
 * Provides unified event collection, NVLink tracking, and topology-aware analysis.
 */
class MultiGPUProfiler {
public:
    /**
     * Construct multi-GPU profiler with configuration
     */
    explicit MultiGPUProfiler(const MultiGPUConfig& config = {});
    
    /**
     * Destructor - ensures cleanup
     */
    ~MultiGPUProfiler();
    
    // Non-copyable, movable
    MultiGPUProfiler(const MultiGPUProfiler&) = delete;
    MultiGPUProfiler& operator=(const MultiGPUProfiler&) = delete;
    MultiGPUProfiler(MultiGPUProfiler&&) noexcept;
    MultiGPUProfiler& operator=(MultiGPUProfiler&&) noexcept;
    
    // =========================================================================
    // Initialization
    // =========================================================================
    
    /**
     * Initialize the multi-GPU profiler
     * Discovers GPUs, creates profilers, and sets up NVLink tracking
     * @return true if initialization succeeded
     */
    bool initialize();
    
    /**
     * Finalize and clean up resources
     */
    void finalize();
    
    /**
     * Check if profiler is initialized
     */
    bool isInitialized() const { return initialized_; }
    
    // =========================================================================
    // GPU Management
    // =========================================================================
    
    /**
     * Add a GPU to profiling
     * @param gpu_id CUDA device index
     * @return true if GPU was added successfully
     */
    bool addGPU(uint32_t gpu_id);
    
    /**
     * Remove a GPU from profiling
     * @param gpu_id CUDA device index
     * @return true if GPU was removed
     */
    bool removeGPU(uint32_t gpu_id);
    
    /**
     * Get list of active GPU IDs
     */
    std::vector<uint32_t> getActiveGPUs() const;
    
    /**
     * Get total number of available GPUs in the system
     */
    uint32_t getAvailableGPUCount() const;
    
    // =========================================================================
    // Capture Control
    // =========================================================================
    
    /**
     * Start capturing events on all active GPUs
     * @return true if capture started successfully
     */
    bool startCapture();
    
    /**
     * Stop capturing events
     * @return true if capture stopped successfully
     */
    bool stopCapture();
    
    /**
     * Check if capture is active
     */
    bool isCapturing() const { return capturing_; }
    
    // =========================================================================
    // Event Retrieval
    // =========================================================================
    
    /**
     * Get all captured events from all GPUs (merged and sorted)
     * @param events Output vector for events
     * @param max_count Maximum events to return (0 = all)
     * @return Number of events returned
     */
    size_t getEvents(std::vector<TraceEvent>& events, size_t max_count = 0);
    
    /**
     * Get events from a specific GPU
     * @param gpu_id GPU device index
     * @param events Output vector for events
     * @return Number of events returned
     */
    size_t getEventsFromGPU(uint32_t gpu_id, std::vector<TraceEvent>& events);
    
    /**
     * Get NVLink transfer records
     */
    std::vector<NVLinkTransfer> getNVLinkTransfers() const;
    
    /**
     * Get peer memory access records
     */
    std::vector<PeerAccess> getPeerAccesses() const;
    
    // =========================================================================
    // Statistics
    // =========================================================================
    
    /**
     * Get total events captured across all GPUs
     */
    uint64_t totalEventsCaptured() const;
    
    /**
     * Get events captured by a specific GPU
     */
    uint64_t eventsFromGPU(uint32_t gpu_id) const;
    
    /**
     * Get comprehensive statistics
     */
    MultiGPUStats getStatistics() const;
    
    // =========================================================================
    // Device Information
    // =========================================================================
    
    /**
     * Get device info for all active GPUs
     */
    std::vector<DeviceInfo> getAllDeviceInfo() const;
    
    /**
     * Get device info for specific GPU
     */
    DeviceInfo getDeviceInfo(uint32_t gpu_id) const;
    
    /**
     * Get GPU topology
     */
    const GPUTopology* getTopology() const { return topology_.get(); }
    
    // =========================================================================
    // Callbacks
    // =========================================================================
    
    using EventCallback = std::function<void(uint32_t gpu_id, const TraceEvent& event)>;
    using NVLinkCallback = std::function<void(const NVLinkTransfer& transfer)>;
    
    /**
     * Set callback for real-time event processing
     */
    void setEventCallback(EventCallback callback);
    
    /**
     * Set callback for NVLink transfer events
     */
    void setNVLinkCallback(NVLinkCallback callback);
    
private:
    // Internal methods
    void aggregationLoop();
    void collectEventsFromGPU(uint32_t gpu_id);
    void trackNVLinkEvents();
    void setupPeerAccess();
    void mergeAndSortEvents();
    
    // Configuration
    MultiGPUConfig config_;
    
    // GPU contexts
    std::map<uint32_t, std::unique_ptr<GPUContext>> gpu_contexts_;
    mutable std::mutex contexts_mutex_;
    
    // Topology
    std::unique_ptr<GPUTopology> topology_;
    
    // Aggregated data
    std::vector<TraceEvent> aggregated_events_;
    std::vector<NVLinkTransfer> nvlink_transfers_;
    std::vector<PeerAccess> peer_accesses_;
    mutable std::mutex events_mutex_;
    mutable std::mutex nvlink_mutex_;
    mutable std::mutex peer_mutex_;
    
    // Aggregation thread
    std::thread aggregation_thread_;
    std::atomic<bool> running_{false};
    
    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> capturing_{false};
    Timestamp capture_start_time_ = 0;
    Timestamp capture_end_time_ = 0;
    
    // Callbacks
    EventCallback event_callback_;
    NVLinkCallback nvlink_callback_;
    
    // Available GPU count (cached)
    uint32_t available_gpu_count_ = 0;
};

} // namespace cluster
} // namespace tracesmith

