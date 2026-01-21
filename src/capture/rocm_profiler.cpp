#include "tracesmith/capture/rocm_profiler.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>

// Platform-specific thread ID includes
#ifdef __linux__
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

namespace tracesmith {

// Singleton instance for static callbacks
ROCmProfiler* ROCmProfiler::instance_ = nullptr;

//==============================================================================
// ROCm Error Handling Macros
//==============================================================================

#ifdef TRACESMITH_ENABLE_ROCM

#define ROCTRACER_CALL(call)                                                  \
    do {                                                                      \
        roctracer_status_t _status = call;                                    \
        if (_status != ROCTRACER_STATUS_SUCCESS) {                            \
            const char* errstr = roctracer_error_string();                    \
            std::cerr << "roctracer error: " << errstr << " at " << __FILE__ \
                      << ":" << __LINE__ << std::endl;                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

#define ROCTRACER_CALL_VOID(call)                                             \
    do {                                                                      \
        roctracer_status_t _status = call;                                    \
        if (_status != ROCTRACER_STATUS_SUCCESS) {                            \
            const char* errstr = roctracer_error_string();                    \
            std::cerr << "roctracer error: " << errstr << " at " << __FILE__ \
                      << ":" << __LINE__ << std::endl;                        \
        }                                                                     \
    } while (0)

#define HIP_CALL(call)                                                        \
    do {                                                                      \
        hipError_t _status = call;                                            \
        if (_status != hipSuccess) {                                          \
            const char* errstr = hipGetErrorString(_status);                  \
            std::cerr << "HIP error: " << errstr << " at " << __FILE__       \
                      << ":" << __LINE__ << std::endl;                        \
            return false;                                                     \
        }                                                                     \
    } while (0)

#endif // TRACESMITH_ENABLE_ROCM

//==============================================================================
// Constructor / Destructor
//==============================================================================

ROCmProfiler::ROCmProfiler()
    : initialized_(false)
    , capturing_(false)
    , events_captured_(0)
    , events_dropped_(0)
    , correlation_counter_(0)
#ifdef TRACESMITH_ENABLE_ROCM
    , activity_pool_(nullptr)
    , buffer_size_(DEFAULT_BUFFER_SIZE)
    , hip_api_tracing_enabled_(true)
    , hip_activity_tracing_enabled_(true)
    , hsa_api_tracing_enabled_(false)
#endif
{
    instance_ = this;
}

ROCmProfiler::~ROCmProfiler() {
    if (capturing_) {
        stopCapture();
    }
    if (initialized_) {
        finalize();
    }
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

//==============================================================================
// Platform Detection
//==============================================================================

bool ROCmProfiler::isAvailable() const {
#ifdef TRACESMITH_ENABLE_ROCM
    return isROCmAvailable();
#else
    return false;
#endif
}

bool isROCmAvailable() {
#ifdef TRACESMITH_ENABLE_ROCM
    hipError_t result = hipInit(0);
    if (result != hipSuccess) {
        return false;
    }
    
    int device_count = 0;
    result = hipGetDeviceCount(&device_count);
    return (result == hipSuccess && device_count > 0);
#else
    return false;
#endif
}

int getROCmDriverVersion() {
#ifdef TRACESMITH_ENABLE_ROCM
    int version = 0;
    if (hipDriverGetVersion(&version) == hipSuccess) {
        return version;
    }
#endif
    return 0;
}

int getROCmDeviceCount() {
#ifdef TRACESMITH_ENABLE_ROCM
    int count = 0;
    if (hipInit(0) == hipSuccess) {
        hipGetDeviceCount(&count);
    }
    return count;
#else
    return 0;
#endif
}

std::string getROCmGpuArch(int device_id) {
#ifdef TRACESMITH_ENABLE_ROCM
    hipDeviceProp_t props;
    if (hipGetDeviceProperties(&props, device_id) == hipSuccess) {
        return std::string(props.gcnArchName);
    }
#else
    (void)device_id;
#endif
    return "";
}

//==============================================================================
// Initialization
//==============================================================================

bool ROCmProfiler::initialize(const ProfilerConfig& config) {
#ifdef TRACESMITH_ENABLE_ROCM
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    // Initialize HIP runtime
    HIP_CALL(hipInit(0));
    
    // Set up roctracer properties for activity tracing
    roctracer_properties_t properties{};
    properties.buffer_size = buffer_size_;
    properties.buffer_callback_fun = hipActivityCallback;
    properties.buffer_callback_arg = this;
    
    ROCTRACER_CALL(roctracer_open_pool(&properties, &activity_pool_));
    
    initialized_ = true;
    return true;
#else
    (void)config;
    std::cerr << "TraceSmith was compiled without ROCm support" << std::endl;
    return false;
#endif
}

void ROCmProfiler::finalize() {
#ifdef TRACESMITH_ENABLE_ROCM
    if (!initialized_) {
        return;
    }
    
    if (capturing_) {
        stopCapture();
    }
    
    if (activity_pool_) {
        ROCTRACER_CALL_VOID(roctracer_close_pool(activity_pool_));
        activity_pool_ = nullptr;
    }
    
    initialized_ = false;
#endif
}

//==============================================================================
// Capture Control
//==============================================================================

bool ROCmProfiler::startCapture() {
#ifdef TRACESMITH_ENABLE_ROCM
    if (!initialized_) {
        std::cerr << "ROCmProfiler not initialized" << std::endl;
        return false;
    }
    
    if (capturing_) {
        return true; // Already capturing
    }
    
    // Clear previous events
    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        events_.clear();
    }
    events_captured_ = 0;
    events_dropped_ = 0;
    
    // Enable HIP API tracing (callback-based)
    if (hip_api_tracing_enabled_) {
        ROCTRACER_CALL(roctracer_enable_domain_callback(
            ACTIVITY_DOMAIN_HIP_API,
            hipApiCallback,
            this
        ));
    }
    
    // Enable HIP activity tracing (async operations)
    if (hip_activity_tracing_enabled_) {
        // Enable activity for various HIP operations
        ROCTRACER_CALL(roctracer_enable_op_activity(
            ACTIVITY_DOMAIN_HIP_OPS,
            HIP_OP_ID_DISPATCH  // Kernel dispatches
        ));
        
        ROCTRACER_CALL(roctracer_enable_op_activity(
            ACTIVITY_DOMAIN_HIP_OPS,
            HIP_OP_ID_COPY  // Memory copies
        ));
        
        ROCTRACER_CALL(roctracer_enable_op_activity(
            ACTIVITY_DOMAIN_HIP_OPS,
            HIP_OP_ID_BARRIER  // Synchronization barriers
        ));
    }
    
    // Enable HSA API tracing (optional, for low-level tracing)
    if (hsa_api_tracing_enabled_) {
        ROCTRACER_CALL(roctracer_enable_domain_callback(
            ACTIVITY_DOMAIN_HSA_API,
            hsaApiCallback,
            this
        ));
    }
    
    capturing_ = true;
    return true;
#else
    return false;
#endif
}

bool ROCmProfiler::stopCapture() {
#ifdef TRACESMITH_ENABLE_ROCM
    if (!capturing_) {
        return true;
    }
    
    capturing_ = false;
    
    // Flush all activity buffers
    ROCTRACER_CALL(roctracer_flush_activity(activity_pool_));
    
    // Disable HIP API tracing
    if (hip_api_tracing_enabled_) {
        ROCTRACER_CALL_VOID(roctracer_disable_domain_callback(ACTIVITY_DOMAIN_HIP_API));
    }
    
    // Disable HIP activity tracing
    if (hip_activity_tracing_enabled_) {
        ROCTRACER_CALL_VOID(roctracer_disable_op_activity(
            ACTIVITY_DOMAIN_HIP_OPS,
            HIP_OP_ID_DISPATCH
        ));
        ROCTRACER_CALL_VOID(roctracer_disable_op_activity(
            ACTIVITY_DOMAIN_HIP_OPS,
            HIP_OP_ID_COPY
        ));
        ROCTRACER_CALL_VOID(roctracer_disable_op_activity(
            ACTIVITY_DOMAIN_HIP_OPS,
            HIP_OP_ID_BARRIER
        ));
    }
    
    // Disable HSA API tracing
    if (hsa_api_tracing_enabled_) {
        ROCTRACER_CALL_VOID(roctracer_disable_domain_callback(ACTIVITY_DOMAIN_HSA_API));
    }
    
    return true;
#else
    return false;
#endif
}

//==============================================================================
// Event Retrieval
//==============================================================================

size_t ROCmProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
    std::lock_guard<std::mutex> lock(events_mutex_);
    
    size_t count = (max_count > 0 && max_count < events_.size()) 
                   ? max_count 
                   : events_.size();
    
    events.insert(events.end(), 
                  events_.begin(), 
                  events_.begin() + count);
    
    events_.erase(events_.begin(), events_.begin() + count);
    
    return count;
}

//==============================================================================
// Device Information
//==============================================================================

std::vector<DeviceInfo> ROCmProfiler::getDeviceInfo() const {
    std::vector<DeviceInfo> devices;
    
#ifdef TRACESMITH_ENABLE_ROCM
    int device_count = 0;
    if (hipGetDeviceCount(&device_count) != hipSuccess) {
        return devices;
    }
    
    for (int i = 0; i < device_count; ++i) {
        hipDeviceProp_t props;
        if (hipGetDeviceProperties(&props, i) != hipSuccess) {
            continue;
        }
        
        DeviceInfo info;
        info.device_id = i;
        info.vendor = "AMD";
        info.name = props.name;
        
        // Architecture (e.g., gfx908 for MI100, gfx90a for MI200)
        info.architecture = props.gcnArchName;
        
        // Compute capability (ROCm version-based)
        info.compute_major = props.major;
        info.compute_minor = props.minor;
        
        // Memory
        info.total_memory = props.totalGlobalMem;
        
        // Compute units
        info.multiprocessor_count = props.multiProcessorCount;
        
        // Clock speed (MHz for AMD, we store as kHz for consistency)
        info.clock_rate = props.clockRate; // Already in kHz
        
        // Additional metadata
        info.metadata["gcnArch"] = std::to_string(props.gcnArch);
        info.metadata["pciBusId"] = std::to_string(props.pciBusID);
        info.metadata["pciDeviceId"] = std::to_string(props.pciDeviceID);
        info.metadata["maxThreadsPerBlock"] = std::to_string(props.maxThreadsPerBlock);
        info.metadata["warpSize"] = std::to_string(props.warpSize);
        info.metadata["memoryBusWidth"] = std::to_string(props.memoryBusWidth);
        info.metadata["l2CacheSize"] = std::to_string(props.l2CacheSize);
        
        devices.push_back(info);
    }
#endif
    
    return devices;
}

//==============================================================================
// Callback Registration
//==============================================================================

void ROCmProfiler::setEventCallback(EventCallback callback) {
    callback_ = std::move(callback);
}

//==============================================================================
// ROCm Configuration
//==============================================================================

#ifdef TRACESMITH_ENABLE_ROCM

void ROCmProfiler::setBufferSize(size_t size_bytes) {
    buffer_size_ = size_bytes;
}

void ROCmProfiler::enableHipApiTracing(bool enable) {
    hip_api_tracing_enabled_ = enable;
}

void ROCmProfiler::enableHipActivityTracing(bool enable) {
    hip_activity_tracing_enabled_ = enable;
}

void ROCmProfiler::enableHsaApiTracing(bool enable) {
    hsa_api_tracing_enabled_ = enable;
}

uint32_t ROCmProfiler::getRoctracerVersion() const {
    // roctracer doesn't expose version directly, use a constant
    return 5;  // ROCm 5.x style
}

//==============================================================================
// ROCm Callbacks (Static)
//==============================================================================

void ROCmProfiler::hipApiCallback(uint32_t domain, uint32_t cid,
                                   const void* callback_data, void* arg) {
    ROCmProfiler* self = static_cast<ROCmProfiler*>(arg);
    if (!self || !self->capturing_) {
        return;
    }
    
    const hip_api_data_t* data = static_cast<const hip_api_data_t*>(callback_data);
    
    // Only process at API exit to get timing
    if (data->phase == ACTIVITY_API_PHASE_EXIT) {
        return;
    }
    
    // Track correlation ID -> thread ID mapping at API entry
    std::lock_guard<std::mutex> lock(self->correlation_mutex_);
    
    // Get current thread ID
    uint32_t thread_id = 0;
#ifdef __linux__
    thread_id = static_cast<uint32_t>(syscall(SYS_gettid));
#else
    thread_id = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
    
    self->correlation_thread_ids_[data->correlation_id] = thread_id;
    
    // Track kernel launches specifically for timing
    if (cid == HIP_API_ID_hipLaunchKernel || 
        cid == HIP_API_ID_hipModuleLaunchKernel) {
        auto now = std::chrono::high_resolution_clock::now();
        self->kernel_start_times_[data->correlation_id] = 
            static_cast<Timestamp>(now.time_since_epoch().count());
    }
}

void ROCmProfiler::hipActivityCallback(const char* begin, const char* end, void* arg) {
    ROCmProfiler* self = static_cast<ROCmProfiler*>(arg);
    if (!self) {
        return;
    }
    
    // Process all records in the buffer
    const roctracer_record_t* record = 
        reinterpret_cast<const roctracer_record_t*>(begin);
    const roctracer_record_t* end_record = 
        reinterpret_cast<const roctracer_record_t*>(end);
    
    while (record < end_record) {
        self->processHipActivity(record);
        roctracer_next_record(record, &record);
    }
}

void ROCmProfiler::hsaApiCallback(uint32_t domain, uint32_t cid,
                                   const void* callback_data, void* arg) {
    ROCmProfiler* self = static_cast<ROCmProfiler*>(arg);
    if (!self || !self->capturing_) {
        return;
    }
    
    // HSA tracing is for advanced low-level debugging
    // Basic implementation - extend as needed
    (void)domain;
    (void)cid;
    (void)callback_data;
}

//==============================================================================
// Activity Processing
//==============================================================================

void ROCmProfiler::processHipActivity(const roctracer_record_t* record) {
    if (!record) {
        return;
    }
    
    switch (record->op) {
        case HIP_OP_ID_DISPATCH:
            processKernelActivity(record);
            break;
            
        case HIP_OP_ID_COPY:
            processMemcpyActivity(record);
            break;
            
        case HIP_OP_ID_BARRIER:
            processSyncActivity(record);
            break;
            
        default:
            // Ignore other activity types
            break;
    }
}

void ROCmProfiler::processKernelActivity(const roctracer_record_t* record) {
    // Get thread ID from correlation map
    uint32_t thread_id = 0;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        auto it = correlation_thread_ids_.find(record->correlation_id);
        if (it != correlation_thread_ids_.end()) {
            thread_id = it->second;
        }
    }
    
    // Create kernel launch event
    TraceEvent launch_event;
    launch_event.type = EventType::KernelLaunch;
    launch_event.timestamp = static_cast<Timestamp>(record->begin_ns);
    launch_event.correlation_id = record->correlation_id;
    launch_event.device_id = record->device_id;
    launch_event.stream_id = record->queue_id;
    launch_event.thread_id = thread_id;
    launch_event.name = record->kernel_name ? record->kernel_name : "hip_kernel";
    
    // Kernel parameters (from roctracer record)
    if (record->kernel_name) {
        launch_event.metadata["kernelName"] = record->kernel_name;
    }
    
    addEvent(std::move(launch_event));
    
    // Create kernel completion event
    TraceEvent complete_event;
    complete_event.type = EventType::KernelComplete;
    complete_event.timestamp = static_cast<Timestamp>(record->end_ns);
    complete_event.correlation_id = record->correlation_id;
    complete_event.device_id = record->device_id;
    complete_event.stream_id = record->queue_id;
    complete_event.thread_id = thread_id;
    complete_event.name = record->kernel_name ? record->kernel_name : "hip_kernel";
    
    // Duration in nanoseconds
    complete_event.metadata["duration_ns"] = std::to_string(record->end_ns - record->begin_ns);
    
    addEvent(std::move(complete_event));
}

void ROCmProfiler::processMemcpyActivity(const roctracer_record_t* record) {
    // Get thread ID from correlation map
    uint32_t thread_id = 0;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        auto it = correlation_thread_ids_.find(record->correlation_id);
        if (it != correlation_thread_ids_.end()) {
            thread_id = it->second;
        }
    }
    
    TraceEvent event;
    
    // Determine memcpy direction based on record type
    // ROCm uses different approach - check memory kind flags
    if (record->op == HIP_OP_ID_COPY) {
        // Default to H2D, actual direction would be in record flags
        event.type = EventType::MemcpyH2D;
        event.name = "hipMemcpy";
        
        // Try to determine direction from additional data
        // This varies by roctracer version
    }
    
    event.timestamp = static_cast<Timestamp>(record->begin_ns);
    event.correlation_id = record->correlation_id;
    event.device_id = record->device_id;
    event.stream_id = record->queue_id;
    event.thread_id = thread_id;
    
    // Memory transfer details
    event.metadata["bytes"] = std::to_string(record->bytes);
    event.metadata["duration_ns"] = std::to_string(record->end_ns - record->begin_ns);
    
    // Bandwidth calculation (bytes per second)
    uint64_t duration_ns = record->end_ns - record->begin_ns;
    if (duration_ns > 0) {
        double bandwidth_gbps = (double)record->bytes / duration_ns; // GB/s
        event.metadata["bandwidth_gbps"] = std::to_string(bandwidth_gbps);
    }
    
    addEvent(std::move(event));
}

void ROCmProfiler::processMemsetActivity(const roctracer_record_t* record) {
    // Get thread ID from correlation map
    uint32_t thread_id = 0;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        auto it = correlation_thread_ids_.find(record->correlation_id);
        if (it != correlation_thread_ids_.end()) {
            thread_id = it->second;
        }
    }
    
    TraceEvent event;
    event.type = EventType::MemsetDevice;
    event.name = "hipMemset";
    event.timestamp = static_cast<Timestamp>(record->begin_ns);
    event.correlation_id = record->correlation_id;
    event.device_id = record->device_id;
    event.stream_id = record->queue_id;
    event.thread_id = thread_id;
    
    event.metadata["bytes"] = std::to_string(record->bytes);
    event.metadata["duration_ns"] = std::to_string(record->end_ns - record->begin_ns);
    
    addEvent(std::move(event));
}

void ROCmProfiler::processSyncActivity(const roctracer_record_t* record) {
    // Get thread ID from correlation map
    uint32_t thread_id = 0;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        auto it = correlation_thread_ids_.find(record->correlation_id);
        if (it != correlation_thread_ids_.end()) {
            thread_id = it->second;
        }
    }
    
    TraceEvent event;
    event.type = EventType::StreamSync;
    event.name = "hipStreamSynchronize";
    event.timestamp = static_cast<Timestamp>(record->begin_ns);
    event.correlation_id = record->correlation_id;
    event.stream_id = record->queue_id;
    event.thread_id = thread_id;
    
    event.metadata["duration_ns"] = std::to_string(record->end_ns - record->begin_ns);
    
    addEvent(std::move(event));
}

#endif // TRACESMITH_ENABLE_ROCM

//==============================================================================
// Thread-Safe Event Storage
//==============================================================================

void ROCmProfiler::addEvent(TraceEvent&& event) {
    ++events_captured_;
    
    // Fire callback if registered
    if (callback_) {
        callback_(event);
    }
    
    // Store event
    std::lock_guard<std::mutex> lock(events_mutex_);
    
    // Check buffer limits
    if (config_.buffer_size > 0 && events_.size() >= config_.buffer_size) {
        ++events_dropped_;
        return;
    }
    
    events_.push_back(std::move(event));
}

} // namespace tracesmith
