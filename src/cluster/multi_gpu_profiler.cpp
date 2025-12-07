/**
 * TraceSmith Multi-GPU Profiler Implementation
 * 
 * Manages profiling across multiple GPUs within a single node.
 */

#include "tracesmith/cluster/multi_gpu_profiler.hpp"
#include "tracesmith/cluster/gpu_topology.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

#ifdef TRACESMITH_ENABLE_CUDA
#include <cuda_runtime.h>
#include "tracesmith/capture/cupti_profiler.hpp"
#endif

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/maca.h>
#include <mcr/mc_runtime_api.h>
#include "tracesmith/capture/mcpti_profiler.hpp"
#endif

namespace tracesmith {
namespace cluster {

// ============================================================================
// MultiGPUProfiler Implementation
// ============================================================================

MultiGPUProfiler::MultiGPUProfiler(const MultiGPUConfig& config)
    : config_(config) {
}

MultiGPUProfiler::~MultiGPUProfiler() {
    finalize();
}

MultiGPUProfiler::MultiGPUProfiler(MultiGPUProfiler&& other) noexcept
    : config_(std::move(other.config_))
    , gpu_contexts_(std::move(other.gpu_contexts_))
    , topology_(std::move(other.topology_))
    , aggregated_events_(std::move(other.aggregated_events_))
    , nvlink_transfers_(std::move(other.nvlink_transfers_))
    , peer_accesses_(std::move(other.peer_accesses_))
    , running_(other.running_.load())
    , initialized_(other.initialized_.load())
    , capturing_(other.capturing_.load())
    , capture_start_time_(other.capture_start_time_)
    , capture_end_time_(other.capture_end_time_)
    , event_callback_(std::move(other.event_callback_))
    , nvlink_callback_(std::move(other.nvlink_callback_))
    , available_gpu_count_(other.available_gpu_count_) {
    other.running_ = false;
    other.initialized_ = false;
    other.capturing_ = false;
}

MultiGPUProfiler& MultiGPUProfiler::operator=(MultiGPUProfiler&& other) noexcept {
    if (this != &other) {
        finalize();
        config_ = std::move(other.config_);
        gpu_contexts_ = std::move(other.gpu_contexts_);
        topology_ = std::move(other.topology_);
        aggregated_events_ = std::move(other.aggregated_events_);
        nvlink_transfers_ = std::move(other.nvlink_transfers_);
        peer_accesses_ = std::move(other.peer_accesses_);
        running_ = other.running_.load();
        initialized_ = other.initialized_.load();
        capturing_ = other.capturing_.load();
        capture_start_time_ = other.capture_start_time_;
        capture_end_time_ = other.capture_end_time_;
        event_callback_ = std::move(other.event_callback_);
        nvlink_callback_ = std::move(other.nvlink_callback_);
        available_gpu_count_ = other.available_gpu_count_;
        other.running_ = false;
        other.initialized_ = false;
        other.capturing_ = false;
    }
    return *this;
}

bool MultiGPUProfiler::initialize() {
    if (initialized_) return true;
    
    int deviceCount = 0;
    
    // Try MACA first (MetaX GPUs)
#ifdef TRACESMITH_ENABLE_MACA
    mcError_t mcErr = mcInit(0);
    if (mcErr == mcSuccess) {
        mcErr = mcGetDeviceCount(&deviceCount);
        if (mcErr == mcSuccess && deviceCount > 0) {
            available_gpu_count_ = deviceCount;
            
            // Discover topology if requested
            if (config_.capture_topology) {
                topology_ = std::make_unique<GPUTopology>();
                if (!topology_->discover()) {
                    std::cerr << "MultiGPUProfiler: Failed to discover GPU topology\n";
                    // Continue anyway - topology is optional
                }
            }
            
            // Add GPUs
            std::vector<uint32_t> gpus_to_add;
            if (config_.gpu_ids.empty()) {
                for (int i = 0; i < deviceCount; ++i) {
                    gpus_to_add.push_back(i);
                }
            } else {
                gpus_to_add = config_.gpu_ids;
            }
            
            for (uint32_t gpu_id : gpus_to_add) {
                if (!addGPU(gpu_id)) {
                    std::cerr << "MultiGPUProfiler: Failed to add GPU " << gpu_id << "\n";
                }
            }
            
            // Setup peer access if enabled
            if (config_.enable_peer_access_tracking) {
                setupPeerAccess();
            }
            
            initialized_ = true;
            return true;
        }
    }
#endif
    
    // Try CUDA (NVIDIA GPUs)
#ifdef TRACESMITH_ENABLE_CUDA
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    if (err != cudaSuccess || deviceCount == 0) {
        std::cerr << "MultiGPUProfiler: No CUDA devices available\n";
        return false;
    }
    available_gpu_count_ = deviceCount;
    
    // Discover topology if requested
    if (config_.capture_topology) {
        topology_ = std::make_unique<GPUTopology>();
        if (!topology_->discover()) {
            std::cerr << "MultiGPUProfiler: Failed to discover GPU topology\n";
            // Continue anyway - topology is optional
        }
    }
    
    // Add GPUs
    std::vector<uint32_t> gpus_to_add;
    if (config_.gpu_ids.empty()) {
        // Add all GPUs
        for (int i = 0; i < deviceCount; ++i) {
            gpus_to_add.push_back(i);
        }
    } else {
        gpus_to_add = config_.gpu_ids;
    }
    
    for (uint32_t gpu_id : gpus_to_add) {
        if (!addGPU(gpu_id)) {
            std::cerr << "MultiGPUProfiler: Failed to add GPU " << gpu_id << "\n";
        }
    }
    
    // Setup peer access if enabled
    if (config_.enable_peer_access_tracking) {
        setupPeerAccess();
    }
    
    initialized_ = true;
    return true;
    
#endif

    std::cerr << "MultiGPUProfiler: No GPU support enabled (CUDA or MACA required)\n";
    return false;
}

void MultiGPUProfiler::finalize() {
    if (!initialized_) return;
    
    // Stop capture if running
    if (capturing_) {
        stopCapture();
    }
    
    // Stop aggregation thread
    running_ = false;
    if (aggregation_thread_.joinable()) {
        aggregation_thread_.join();
    }
    
    // Clean up GPU contexts
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        for (auto& [gpu_id, ctx] : gpu_contexts_) {
            if (ctx->profiler) {
                ctx->profiler->finalize();
            }
        }
        gpu_contexts_.clear();
    }
    
    topology_.reset();
    initialized_ = false;
}

bool MultiGPUProfiler::addGPU(uint32_t gpu_id) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    // Check if already added
    if (gpu_contexts_.find(gpu_id) != gpu_contexts_.end()) {
        return true;
    }
    
    // Create context
    auto ctx = std::make_unique<GPUContext>();
    ctx->gpu_id = gpu_id;
    ctx->device_index = gpu_id;
    
    // Try MACA first (MetaX GPUs)
#ifdef TRACESMITH_ENABLE_MACA
    {
        int deviceCount = 0;
        mcError_t err = mcGetDeviceCount(&deviceCount);
        if (err == mcSuccess && gpu_id < static_cast<uint32_t>(deviceCount)) {
            // Set device and get info
            mcSetDevice(gpu_id);
            
            mcDeviceProp_t prop;
            if (mcGetDeviceProperties(&prop, gpu_id) == mcSuccess) {
                ctx->device_info.name = prop.name;
                ctx->device_info.compute_major = prop.major;
                ctx->device_info.compute_minor = prop.minor;
                ctx->device_info.total_memory = prop.totalGlobalMem;
                ctx->device_info.multiprocessor_count = prop.multiProcessorCount;
                ctx->device_info.clock_rate = prop.clockRate;
                ctx->device_info.memory_clock_rate = prop.memoryClockRate;
                ctx->device_info.memory_bus_width = prop.memoryBusWidth;
                ctx->device_info.max_threads_per_mp = prop.maxThreadsPerMultiProcessor;
                ctx->device_info.warp_size = prop.warpSize;
                ctx->device_info.vendor = "MetaX";
                
                // Create MCPTI profiler for this GPU
                ctx->profiler = std::make_unique<MCPTIProfiler>();
                
                ProfilerConfig prof_config;
                prof_config.buffer_size = config_.per_gpu_buffer_size;
                prof_config.overflow_policy = config_.overflow_policy;
                
                if (!ctx->profiler->initialize(prof_config)) {
                    std::cerr << "MultiGPUProfiler: Failed to initialize MCPTI profiler for GPU " << gpu_id << "\n";
                    return false;
                }
                
                ctx->local_events.reserve(config_.per_gpu_buffer_size);
                ctx->active = true;
                
                gpu_contexts_[gpu_id] = std::move(ctx);
                return true;
            }
        }
    }
#endif
    
    // Try CUDA (NVIDIA GPUs)
#ifdef TRACESMITH_ENABLE_CUDA
    {
        // Validate GPU ID
        int deviceCount = 0;
        cudaGetDeviceCount(&deviceCount);
        if (gpu_id >= static_cast<uint32_t>(deviceCount)) {
            std::cerr << "MultiGPUProfiler: Invalid GPU ID " << gpu_id << "\n";
            return false;
        }
        
        // Set device and get info
        cudaSetDevice(gpu_id);
        
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, gpu_id);
        
        ctx->device_info.name = prop.name;
        ctx->device_info.compute_major = prop.major;
        ctx->device_info.compute_minor = prop.minor;
        ctx->device_info.total_memory = prop.totalGlobalMem;
        ctx->device_info.multiprocessor_count = prop.multiProcessorCount;
        ctx->device_info.clock_rate = prop.clockRate;
        ctx->device_info.memory_clock_rate = prop.memoryClockRate;
        ctx->device_info.memory_bus_width = prop.memoryBusWidth;
        ctx->device_info.max_threads_per_mp = prop.maxThreadsPerMultiProcessor;
        ctx->device_info.warp_size = prop.warpSize;
        ctx->device_info.vendor = "NVIDIA";
        
        // Create CUPTI profiler for this GPU
        ctx->profiler = std::make_unique<CUPTIProfiler>();
        
        ProfilerConfig prof_config;
        prof_config.buffer_size = config_.per_gpu_buffer_size;
        prof_config.overflow_policy = config_.overflow_policy;
        
        if (!ctx->profiler->initialize(prof_config)) {
            std::cerr << "MultiGPUProfiler: Failed to initialize profiler for GPU " << gpu_id << "\n";
            return false;
        }
        
        ctx->local_events.reserve(config_.per_gpu_buffer_size);
        ctx->active = true;
        
        gpu_contexts_[gpu_id] = std::move(ctx);
        return true;
    }
#endif
    
    std::cerr << "MultiGPUProfiler: No GPU support available\n";
    return false;
}

bool MultiGPUProfiler::removeGPU(uint32_t gpu_id) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto it = gpu_contexts_.find(gpu_id);
    if (it == gpu_contexts_.end()) {
        return false;
    }
    
    // Stop profiler
    if (it->second->profiler) {
        if (capturing_) {
            it->second->profiler->stopCapture();
        }
        it->second->profiler->finalize();
    }
    
    gpu_contexts_.erase(it);
    return true;
}

std::vector<uint32_t> MultiGPUProfiler::getActiveGPUs() const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    std::vector<uint32_t> gpus;
    for (const auto& [gpu_id, ctx] : gpu_contexts_) {
        if (ctx->active) {
            gpus.push_back(gpu_id);
        }
    }
    return gpus;
}

uint32_t MultiGPUProfiler::getAvailableGPUCount() const {
    return available_gpu_count_;
}

bool MultiGPUProfiler::startCapture() {
    if (!initialized_ || capturing_) return false;
    
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    // Start capture on all GPUs
    for (auto& [gpu_id, ctx] : gpu_contexts_) {
        if (ctx->profiler && ctx->active) {
#ifdef TRACESMITH_ENABLE_CUDA
            cudaSetDevice(gpu_id);
#endif
            if (!ctx->profiler->startCapture()) {
                std::cerr << "MultiGPUProfiler: Failed to start capture on GPU " << gpu_id << "\n";
                // Continue with other GPUs
            }
        }
    }
    
    capture_start_time_ = getCurrentTimestamp();
    capturing_ = true;
    
    // Start aggregation thread
    running_ = true;
    aggregation_thread_ = std::thread(&MultiGPUProfiler::aggregationLoop, this);
    
    return true;
}

bool MultiGPUProfiler::stopCapture() {
    if (!capturing_) return false;
    
    // Stop aggregation first
    running_ = false;
    if (aggregation_thread_.joinable()) {
        aggregation_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    // Stop capture on all GPUs
    for (auto& [gpu_id, ctx] : gpu_contexts_) {
        if (ctx->profiler && ctx->active) {
#ifdef TRACESMITH_ENABLE_CUDA
            cudaSetDevice(gpu_id);
#endif
            ctx->profiler->stopCapture();
            
            // Collect remaining events
            collectEventsFromGPU(gpu_id);
        }
    }
    
    capture_end_time_ = getCurrentTimestamp();
    capturing_ = false;
    
    // Final merge and sort
    mergeAndSortEvents();
    
    return true;
}

void MultiGPUProfiler::aggregationLoop() {
    while (running_) {
        // Collect events from all GPUs
        {
            std::lock_guard<std::mutex> lock(contexts_mutex_);
            for (auto& [gpu_id, ctx] : gpu_contexts_) {
                if (ctx->active) {
                    collectEventsFromGPU(gpu_id);
                }
            }
        }
        
        // Track NVLink if enabled
        if (config_.enable_nvlink_tracking) {
            trackNVLinkEvents();
        }
        
        // Sleep for aggregation interval
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.aggregation_interval_ms));
    }
}

void MultiGPUProfiler::collectEventsFromGPU(uint32_t gpu_id) {
    auto it = gpu_contexts_.find(gpu_id);
    if (it == gpu_contexts_.end()) return;
    
    auto& ctx = it->second;
    if (!ctx->profiler) return;
    
#ifdef TRACESMITH_ENABLE_CUDA
    cudaSetDevice(gpu_id);
#endif
    
    std::vector<TraceEvent> events;
    size_t count = ctx->profiler->getEvents(events, 10000);
    
    if (count > 0) {
        // Tag events with GPU ID
        for (auto& event : events) {
            event.device_id = gpu_id;
        }
        
        // Add to local buffer
        ctx->local_events.insert(ctx->local_events.end(),
                                  events.begin(), events.end());
        ctx->event_count += count;
        
        // Call event callback if set
        if (event_callback_) {
            for (const auto& event : events) {
                event_callback_(gpu_id, event);
            }
        }
    }
    
    // Check for dropped events
    ctx->events_dropped = ctx->profiler->eventsDropped();
}

void MultiGPUProfiler::trackNVLinkEvents() {
    // NVLink tracking would use CUPTI's NVLink tracking APIs
    // This is a placeholder for future implementation
}

void MultiGPUProfiler::setupPeerAccess() {
#ifdef TRACESMITH_ENABLE_CUDA
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    // Enable peer access between all GPU pairs
    std::vector<uint32_t> gpus;
    for (const auto& [gpu_id, ctx] : gpu_contexts_) {
        gpus.push_back(gpu_id);
    }
    
    for (size_t i = 0; i < gpus.size(); ++i) {
        for (size_t j = i + 1; j < gpus.size(); ++j) {
            int canAccess = 0;
            cudaDeviceCanAccessPeer(&canAccess, gpus[i], gpus[j]);
            
            if (canAccess) {
                cudaSetDevice(gpus[i]);
                cudaDeviceEnablePeerAccess(gpus[j], 0);
                
                cudaSetDevice(gpus[j]);
                cudaDeviceEnablePeerAccess(gpus[i], 0);
            }
        }
    }
#endif
}

void MultiGPUProfiler::mergeAndSortEvents() {
    std::lock_guard<std::mutex> events_lock(events_mutex_);
    std::lock_guard<std::mutex> contexts_lock(contexts_mutex_);
    
    aggregated_events_.clear();
    
    // Merge all local events
    for (auto& [gpu_id, ctx] : gpu_contexts_) {
        aggregated_events_.insert(aggregated_events_.end(),
                                   ctx->local_events.begin(),
                                   ctx->local_events.end());
    }
    
    // Sort by timestamp
    std::sort(aggregated_events_.begin(), aggregated_events_.end(),
              [](const TraceEvent& a, const TraceEvent& b) {
                  return a.timestamp < b.timestamp;
              });
}

size_t MultiGPUProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
    std::lock_guard<std::mutex> lock(events_mutex_);
    
    if (max_count == 0 || max_count > aggregated_events_.size()) {
        max_count = aggregated_events_.size();
    }
    
    events.insert(events.end(),
                  aggregated_events_.begin(),
                  aggregated_events_.begin() + max_count);
    
    return max_count;
}

size_t MultiGPUProfiler::getEventsFromGPU(uint32_t gpu_id, std::vector<TraceEvent>& events) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto it = gpu_contexts_.find(gpu_id);
    if (it == gpu_contexts_.end()) return 0;
    
    events = it->second->local_events;
    return events.size();
}

std::vector<NVLinkTransfer> MultiGPUProfiler::getNVLinkTransfers() const {
    std::lock_guard<std::mutex> lock(nvlink_mutex_);
    return nvlink_transfers_;
}

std::vector<PeerAccess> MultiGPUProfiler::getPeerAccesses() const {
    std::lock_guard<std::mutex> lock(peer_mutex_);
    return peer_accesses_;
}

uint64_t MultiGPUProfiler::totalEventsCaptured() const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    uint64_t total = 0;
    for (const auto& [gpu_id, ctx] : gpu_contexts_) {
        total += ctx->event_count.load();
    }
    return total;
}

uint64_t MultiGPUProfiler::eventsFromGPU(uint32_t gpu_id) const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto it = gpu_contexts_.find(gpu_id);
    if (it == gpu_contexts_.end()) return 0;
    
    return it->second->event_count.load();
}

MultiGPUStats MultiGPUProfiler::getStatistics() const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    MultiGPUStats stats;
    
    for (const auto& [gpu_id, ctx] : gpu_contexts_) {
        uint64_t events = ctx->event_count.load();
        uint64_t dropped = ctx->events_dropped.load();
        
        stats.events_per_gpu[gpu_id] = events;
        stats.dropped_per_gpu[gpu_id] = dropped;
        stats.total_events += events;
        stats.total_dropped += dropped;
    }
    
    {
        std::lock_guard<std::mutex> nvlink_lock(nvlink_mutex_);
        stats.nvlink_transfers = nvlink_transfers_.size();
        for (const auto& transfer : nvlink_transfers_) {
            stats.nvlink_bytes += transfer.bytes;
        }
    }
    
    {
        std::lock_guard<std::mutex> peer_lock(peer_mutex_);
        stats.peer_accesses = peer_accesses_.size();
    }
    
    if (capture_end_time_ > capture_start_time_) {
        stats.capture_duration_ms = 
            static_cast<double>(capture_end_time_ - capture_start_time_) / 1e6;
    }
    
    return stats;
}

std::vector<DeviceInfo> MultiGPUProfiler::getAllDeviceInfo() const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    std::vector<DeviceInfo> devices;
    for (const auto& [gpu_id, ctx] : gpu_contexts_) {
        devices.push_back(ctx->device_info);
    }
    return devices;
}

DeviceInfo MultiGPUProfiler::getDeviceInfo(uint32_t gpu_id) const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto it = gpu_contexts_.find(gpu_id);
    if (it == gpu_contexts_.end()) return {};
    
    return it->second->device_info;
}

void MultiGPUProfiler::setEventCallback(EventCallback callback) {
    event_callback_ = std::move(callback);
}

void MultiGPUProfiler::setNVLinkCallback(NVLinkCallback callback) {
    nvlink_callback_ = std::move(callback);
}

} // namespace cluster
} // namespace tracesmith

