#include "tracesmith/profiler.hpp"
#include "tracesmith/cupti_profiler.hpp"
#include "tracesmith/stack_capture.hpp"
#ifdef TRACESMITH_ENABLE_METAL
#include "tracesmith/metal_profiler.hpp"
#endif
#include <chrono>
#include <random>

namespace tracesmith {

// ============================================================================
// Platform Detection Functions (always available)
// ============================================================================

#ifndef TRACESMITH_ENABLE_CUDA
// Stub implementations when CUDA is not enabled
bool isCUDAAvailable() { return false; }
int getCUDADriverVersion() { return 0; }
int getCUDADeviceCount() { return 0; }
#endif

// ============================================================================
// SimulationProfiler Implementation
// ============================================================================

SimulationProfiler::SimulationProfiler()
    : capturing_(false)
    , running_(false)
    , events_captured_(0)
    , correlation_id_(0)
    , event_rate_(1000.0) {  // Default: 1000 events per second
}

SimulationProfiler::~SimulationProfiler() {
    finalize();
}

bool SimulationProfiler::initialize(const ProfilerConfig& config) {
    config_ = config;
    buffer_ = std::make_unique<RingBuffer<TraceEvent>>(
        config.buffer_size, config.overflow_policy);
    return true;
}

void SimulationProfiler::finalize() {
    stopCapture();
    buffer_.reset();
}

bool SimulationProfiler::startCapture() {
    if (capturing_.load()) {
        return false;  // Already capturing
    }
    
    if (!buffer_) {
        return false;  // Not initialized
    }
    
    capturing_.store(true);
    running_.store(true);
    
    // Start the event generator thread
    generator_thread_ = std::make_unique<std::thread>(&SimulationProfiler::generatorLoop, this);
    
    return true;
}

bool SimulationProfiler::stopCapture() {
    if (!capturing_.load()) {
        return false;
    }
    
    capturing_.store(false);
    running_.store(false);
    
    if (generator_thread_ && generator_thread_->joinable()) {
        generator_thread_->join();
    }
    generator_thread_.reset();
    
    return true;
}

size_t SimulationProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
    if (!buffer_) {
        return 0;
    }
    
    if (max_count == 0) {
        max_count = buffer_->size();
    }
    
    return buffer_->popBatch(events, max_count);
}

std::vector<DeviceInfo> SimulationProfiler::getDeviceInfo() const {
    // Return simulated device info
    DeviceInfo device;
    device.device_id = 0;
    device.name = "Simulated GPU";
    device.vendor = "TraceSmith Simulation";
    device.compute_major = 8;
    device.compute_minor = 0;
    device.total_memory = 16ULL * 1024 * 1024 * 1024;  // 16 GB
    device.memory_clock_rate = 1215000;  // 1.215 GHz
    device.memory_bus_width = 256;
    device.multiprocessor_count = 68;
    device.max_threads_per_mp = 1536;
    device.clock_rate = 1695000;  // 1.695 GHz
    device.warp_size = 32;
    
    return {device};
}

void SimulationProfiler::setEventCallback(EventCallback callback) {
    callback_ = std::move(callback);
}

uint64_t SimulationProfiler::eventsDropped() const {
    return buffer_ ? buffer_->droppedCount() : 0;
}

void SimulationProfiler::setEventRate(double events_per_second) {
    event_rate_ = events_per_second;
}

void SimulationProfiler::generateKernelEvent(const std::string& name, uint32_t stream_id) {
    TraceEvent event(EventType::KernelLaunch);
    event.name = name;
    event.stream_id = stream_id;
    event.device_id = 0;
    event.correlation_id = correlation_id_.fetch_add(1);
    
    // Random duration between 10us and 10ms
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(10000, 10000000);
    event.duration = dist(gen);
    
    // Add kernel params
    KernelParams kp;
    std::uniform_int_distribution<uint32_t> grid_dist(1, 256);
    std::uniform_int_distribution<uint32_t> block_dist(32, 1024);
    kp.grid_x = grid_dist(gen);
    kp.grid_y = 1;
    kp.grid_z = 1;
    kp.block_x = block_dist(gen);
    kp.block_y = 1;
    kp.block_z = 1;
    kp.shared_mem_bytes = 0;
    kp.registers_per_thread = 32;
    event.kernel_params = kp;
    
    // Capture call stack if enabled
    if (config_.capture_callstacks && StackCapture::isAvailable()) {
        StackCaptureConfig stack_config;
        stack_config.max_depth = config_.callstack_depth;
        StackCapture capturer(stack_config);
        
        CallStack cs;
        if (capturer.capture(cs) > 0) {
            event.call_stack = cs;
        }
    }
    
    if (buffer_) {
        buffer_->push(std::move(event));
        events_captured_.fetch_add(1);
    }
    
    if (callback_) {
        callback_(event);
    }
}

void SimulationProfiler::generateMemcpyEvent(EventType type, uint64_t size, uint32_t stream_id) {
    TraceEvent event(type);
    event.stream_id = stream_id;
    event.device_id = 0;
    event.correlation_id = correlation_id_.fetch_add(1);
    
    // Duration based on size (assume ~500 GB/s bandwidth)
    event.duration = size * 1000 / 500;  // nanoseconds
    
    switch (type) {
        case EventType::MemcpyH2D:
            event.name = "cudaMemcpyHostToDevice";
            break;
        case EventType::MemcpyD2H:
            event.name = "cudaMemcpyDeviceToHost";
            break;
        case EventType::MemcpyD2D:
            event.name = "cudaMemcpyDeviceToDevice";
            break;
        default:
            event.name = "cudaMemcpy";
            break;
    }
    
    MemoryParams mp;
    mp.size_bytes = size;
    mp.src_address = 0x7f0000000000;
    mp.dst_address = 0x7f1000000000;
    event.memory_params = mp;
    
    if (buffer_) {
        buffer_->push(std::move(event));
        events_captured_.fetch_add(1);
    }
    
    if (callback_) {
        callback_(event);
    }
}

void SimulationProfiler::generatorLoop() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    auto interval = std::chrono::microseconds(
        static_cast<int64_t>(1000000.0 / event_rate_));
    
    while (running_.load()) {
        if (capturing_.load()) {
            TraceEvent event = createRandomEvent();
            
            if (buffer_) {
                buffer_->push(std::move(event));
                events_captured_.fetch_add(1);
            }
            
            if (callback_) {
                callback_(event);
            }
        }
        
        std::this_thread::sleep_for(interval);
    }
}

TraceEvent SimulationProfiler::createRandomEvent() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<int> type_dist(0, 5);
    
    static const char* kernel_names[] = {
        "matmul_kernel",
        "conv2d_forward",
        "relu_activation",
        "softmax_kernel",
        "attention_kernel",
        "layernorm_kernel",
        "elementwise_add",
        "reduce_sum",
        "transpose_kernel",
        "embedding_lookup"
    };
    
    int type_roll = type_dist(gen);
    TraceEvent event;
    
    std::uniform_int_distribution<uint32_t> stream_dist(0, 3);
    uint32_t stream_id = stream_dist(gen);
    
    switch (type_roll) {
        case 0:
        case 1:
        case 2: {
            // Kernel launch (most common)
            event.type = EventType::KernelLaunch;
            std::uniform_int_distribution<int> name_dist(0, 9);
            event.name = kernel_names[name_dist(gen)];
            event.stream_id = stream_id;
            event.device_id = 0;
            event.correlation_id = correlation_id_.fetch_add(1);
            
            std::uniform_int_distribution<uint64_t> dur_dist(10000, 5000000);
            event.duration = dur_dist(gen);
            
            KernelParams kp;
            std::uniform_int_distribution<uint32_t> grid_dist(1, 256);
            std::uniform_int_distribution<uint32_t> block_dist(32, 1024);
            kp.grid_x = grid_dist(gen);
            kp.grid_y = 1;
            kp.grid_z = 1;
            kp.block_x = block_dist(gen);
            kp.block_y = 1;
            kp.block_z = 1;
            event.kernel_params = kp;
            break;
        }
        case 3: {
            // Memcpy H2D
            event.type = EventType::MemcpyH2D;
            event.name = "cudaMemcpyHostToDevice";
            event.stream_id = stream_id;
            event.device_id = 0;
            event.correlation_id = correlation_id_.fetch_add(1);
            
            std::uniform_int_distribution<uint64_t> size_dist(1024, 100 * 1024 * 1024);
            uint64_t size = size_dist(gen);
            event.duration = size * 1000 / 500;
            
            MemoryParams mp;
            mp.size_bytes = size;
            event.memory_params = mp;
            break;
        }
        case 4: {
            // Memcpy D2H
            event.type = EventType::MemcpyD2H;
            event.name = "cudaMemcpyDeviceToHost";
            event.stream_id = stream_id;
            event.device_id = 0;
            event.correlation_id = correlation_id_.fetch_add(1);
            
            std::uniform_int_distribution<uint64_t> size_dist(1024, 100 * 1024 * 1024);
            uint64_t size = size_dist(gen);
            event.duration = size * 1000 / 500;
            
            MemoryParams mp;
            mp.size_bytes = size;
            event.memory_params = mp;
            break;
        }
        case 5: {
            // Stream sync
            event.type = EventType::StreamSync;
            event.name = "cudaStreamSynchronize";
            event.stream_id = stream_id;
            event.device_id = 0;
            event.correlation_id = correlation_id_.fetch_add(1);
            
            std::uniform_int_distribution<uint64_t> dur_dist(1000, 100000);
            event.duration = dur_dist(gen);
            break;
        }
    }
    
    event.timestamp = getCurrentTimestamp();
    
    return event;
}

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<IPlatformProfiler> createProfiler(PlatformType type) {
    if (type == PlatformType::Unknown) {
        type = detectPlatform();
    }
    
    switch (type) {
        case PlatformType::CUDA:
#ifdef TRACESMITH_ENABLE_CUDA
            {
                auto profiler = std::make_unique<CUPTIProfiler>();
                if (profiler->isAvailable()) {
                    return profiler;
                }
            }
#endif
            return nullptr;  // CUDA not available
        
        case PlatformType::ROCm:
            // TODO: Implement ROCmProfiler
            return nullptr;
        
        case PlatformType::Metal:
#ifdef TRACESMITH_ENABLE_METAL
            {
                auto profiler = std::make_unique<MetalProfiler>();
                if (profiler->isAvailable()) {
                    return profiler;
                }
            }
#endif
            return nullptr;  // Metal not available
        
        default:
            return nullptr;  // Unknown platform
    }
}

PlatformType detectPlatform() {
#ifdef TRACESMITH_ENABLE_CUDA
    // Check for CUDA
    if (isCUDAAvailable()) {
        return PlatformType::CUDA;
    }
#endif
    
#ifdef TRACESMITH_ENABLE_METAL
    // Check for Metal (macOS/iOS)
    if (isMetalAvailable()) {
        return PlatformType::Metal;
    }
#endif
    
    // TODO: Check for ROCm
    // if (isROCmAvailable()) return PlatformType::ROCm;
    
    return PlatformType::Unknown;
}

} // namespace tracesmith
