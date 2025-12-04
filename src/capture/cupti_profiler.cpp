#include "tracesmith/capture/cupti_profiler.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace tracesmith {

// Singleton instance for static callbacks
CUPTIProfiler* CUPTIProfiler::instance_ = nullptr;

//==============================================================================
// CUPTI Error Handling Macros
//==============================================================================

#ifdef TRACESMITH_ENABLE_CUDA

#define CUPTI_CALL(call)                                                    \
    do {                                                                    \
        CUptiResult _status = call;                                         \
        if (_status != CUPTI_SUCCESS) {                                     \
            const char* errstr;                                             \
            cuptiGetResultString(_status, &errstr);                         \
            std::cerr << "CUPTI error: " << errstr << " at " << __FILE__   \
                      << ":" << __LINE__ << std::endl;                      \
            return false;                                                   \
        }                                                                   \
    } while (0)

#define CUPTI_CALL_VOID(call)                                               \
    do {                                                                    \
        CUptiResult _status = call;                                         \
        if (_status != CUPTI_SUCCESS) {                                     \
            const char* errstr;                                             \
            cuptiGetResultString(_status, &errstr);                         \
            std::cerr << "CUPTI error: " << errstr << " at " << __FILE__   \
                      << ":" << __LINE__ << std::endl;                      \
        }                                                                   \
    } while (0)

#define CUDA_CALL(call)                                                     \
    do {                                                                    \
        CUresult _status = call;                                            \
        if (_status != CUDA_SUCCESS) {                                      \
            const char* errstr;                                             \
            cuGetErrorString(_status, &errstr);                             \
            std::cerr << "CUDA error: " << errstr << " at " << __FILE__    \
                      << ":" << __LINE__ << std::endl;                      \
            return false;                                                   \
        }                                                                   \
    } while (0)

#endif // TRACESMITH_ENABLE_CUDA

//==============================================================================
// Constructor / Destructor
//==============================================================================

CUPTIProfiler::CUPTIProfiler()
    : initialized_(false)
    , capturing_(false)
    , events_captured_(0)
    , events_dropped_(0)
    , correlation_counter_(0)
#ifdef TRACESMITH_ENABLE_CUDA
    , subscriber_(nullptr)
    , buffer_size_(DEFAULT_BUFFER_SIZE)
#endif
{
    instance_ = this;
}

CUPTIProfiler::~CUPTIProfiler() {
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

bool CUPTIProfiler::isAvailable() const {
#ifdef TRACESMITH_ENABLE_CUDA
    return isCUDAAvailable();
#else
    return false;
#endif
}

bool isCUDAAvailable() {
#ifdef TRACESMITH_ENABLE_CUDA
    CUresult result = cuInit(0);
    if (result != CUDA_SUCCESS) {
        return false;
    }
    
    int device_count = 0;
    result = cuDeviceGetCount(&device_count);
    return (result == CUDA_SUCCESS && device_count > 0);
#else
    return false;
#endif
}

int getCUDADriverVersion() {
#ifdef TRACESMITH_ENABLE_CUDA
    int version = 0;
    if (cuDriverGetVersion(&version) == CUDA_SUCCESS) {
        return version;
    }
#endif
    return 0;
}

int getCUDADeviceCount() {
#ifdef TRACESMITH_ENABLE_CUDA
    int count = 0;
    if (cuInit(0) == CUDA_SUCCESS) {
        cuDeviceGetCount(&count);
    }
    return count;
#else
    return 0;
#endif
}

//==============================================================================
// Initialization
//==============================================================================

bool CUPTIProfiler::initialize(const ProfilerConfig& config) {
#ifdef TRACESMITH_ENABLE_CUDA
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    // Initialize CUDA driver API
    CUDA_CALL(cuInit(0));
    
    // Subscribe to CUPTI
    CUPTI_CALL(cuptiSubscribe(&subscriber_, 
                              (CUpti_CallbackFunc)callbackHandler, 
                              this));
    
    // Set default enabled activities
    enabled_activities_ = {
        CUPTI_ACTIVITY_KIND_KERNEL,
        CUPTI_ACTIVITY_KIND_MEMCPY,
        CUPTI_ACTIVITY_KIND_MEMSET,
        CUPTI_ACTIVITY_KIND_SYNCHRONIZATION
    };
    
    // Register buffer callbacks
    CUPTI_CALL(cuptiActivityRegisterCallbacks(bufferRequested, bufferCompleted));
    
    initialized_ = true;
    return true;
#else
    std::cerr << "TraceSmith was compiled without CUDA support" << std::endl;
    return false;
#endif
}

void CUPTIProfiler::finalize() {
#ifdef TRACESMITH_ENABLE_CUDA
    if (!initialized_) {
        return;
    }
    
    if (capturing_) {
        stopCapture();
    }
    
    if (subscriber_) {
        CUPTI_CALL_VOID(cuptiUnsubscribe(subscriber_));
        subscriber_ = nullptr;
    }
    
    initialized_ = false;
#endif
}

//==============================================================================
// Capture Control
//==============================================================================

bool CUPTIProfiler::startCapture() {
#ifdef TRACESMITH_ENABLE_CUDA
    if (!initialized_) {
        std::cerr << "CUPTIProfiler not initialized" << std::endl;
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
    
    // Enable activity kinds
    for (auto kind : enabled_activities_) {
        CUPTI_CALL(cuptiActivityEnable(kind));
    }
    
    // Enable callback domain for runtime API (optional, for launch tracking)
    CUPTI_CALL(cuptiEnableDomain(1, subscriber_, CUPTI_CB_DOMAIN_RUNTIME_API));
    
    capturing_ = true;
    return true;
#else
    return false;
#endif
}

bool CUPTIProfiler::stopCapture() {
#ifdef TRACESMITH_ENABLE_CUDA
    if (!capturing_) {
        return true;
    }
    
    capturing_ = false;
    
    // Flush all activity buffers
    CUPTI_CALL(cuptiActivityFlushAll(CUPTI_ACTIVITY_FLAG_FLUSH_FORCED));
    
    // Disable activity kinds
    for (auto kind : enabled_activities_) {
        CUPTI_CALL_VOID(cuptiActivityDisable(kind));
    }
    
    // Disable callback domain
    CUPTI_CALL_VOID(cuptiEnableDomain(0, subscriber_, CUPTI_CB_DOMAIN_RUNTIME_API));
    
    return true;
#else
    return false;
#endif
}

//==============================================================================
// Event Retrieval
//==============================================================================

size_t CUPTIProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
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

std::vector<DeviceInfo> CUPTIProfiler::getDeviceInfo() const {
    std::vector<DeviceInfo> devices;
    
#ifdef TRACESMITH_ENABLE_CUDA
    int device_count = 0;
    if (cuDeviceGetCount(&device_count) != CUDA_SUCCESS) {
        return devices;
    }
    
    for (int i = 0; i < device_count; ++i) {
        CUdevice device;
        if (cuDeviceGet(&device, i) != CUDA_SUCCESS) {
            continue;
        }
        
        DeviceInfo info;
        info.device_id = i;
        info.vendor = "NVIDIA";
        
        // Device name
        char name[256];
        if (cuDeviceGetName(name, sizeof(name), device) == CUDA_SUCCESS) {
            info.name = name;
        }
        
        // Compute capability
        int major = 0, minor = 0;
        cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device);
        cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device);
        info.compute_major = major;
        info.compute_minor = minor;
        
        // Memory
        size_t total_mem = 0;
        cuDeviceTotalMem(&total_mem, device);
        info.total_memory = total_mem;
        
        // Compute units (SMs)
        int sm_count = 0;
        cuDeviceGetAttribute(&sm_count, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device);
        info.multiprocessor_count = sm_count;
        
        // Clock speed
        int clock_rate_khz = 0;
        cuDeviceGetAttribute(&clock_rate_khz, CU_DEVICE_ATTRIBUTE_CLOCK_RATE, device);
        info.clock_rate = clock_rate_khz; // kHz
        
        devices.push_back(info);
    }
#endif
    
    return devices;
}

//==============================================================================
// Callback Registration
//==============================================================================

void CUPTIProfiler::setEventCallback(EventCallback callback) {
    callback_ = std::move(callback);
}

//==============================================================================
// CUPTI Configuration
//==============================================================================

#ifdef TRACESMITH_ENABLE_CUDA

void CUPTIProfiler::setBufferSize(size_t size_bytes) {
    buffer_size_ = size_bytes;
}

void CUPTIProfiler::enableActivityKind(CUpti_ActivityKind kind, bool enable) {
    auto it = std::find(enabled_activities_.begin(), enabled_activities_.end(), kind);
    
    if (enable && it == enabled_activities_.end()) {
        enabled_activities_.push_back(kind);
    } else if (!enable && it != enabled_activities_.end()) {
        enabled_activities_.erase(it);
    }
}

uint32_t CUPTIProfiler::getCuptiVersion() const {
    uint32_t version = 0;
    cuptiGetVersion(&version);
    return version;
}

//==============================================================================
// CUPTI Buffer Callbacks (Static)
//==============================================================================

void CUPTIAPI CUPTIProfiler::bufferRequested(uint8_t** buffer, size_t* size, 
                                              size_t* maxNumRecords) {
    if (!instance_) {
        *buffer = nullptr;
        *size = 0;
        *maxNumRecords = 0;
        return;
    }
    
    // Allocate aligned buffer
    *size = instance_->buffer_size_;
    *buffer = (uint8_t*)aligned_alloc(ALIGN_SIZE, *size);
    
    if (*buffer == nullptr) {
        std::cerr << "CUPTI: Failed to allocate activity buffer" << std::endl;
        *size = 0;
        *maxNumRecords = 0;
        return;
    }
    
    *maxNumRecords = 0; // No limit on records per buffer
}

void CUPTIAPI CUPTIProfiler::bufferCompleted(CUcontext ctx, uint32_t streamId,
                                              uint8_t* buffer, size_t size, 
                                              size_t validSize) {
    if (!instance_ || !buffer) {
        free(buffer);
        return;
    }
    
    // Process all activities in the buffer
    CUpti_Activity* record = nullptr;
    CUptiResult status;
    
    while ((status = cuptiActivityGetNextRecord(buffer, validSize, &record)) 
           == CUPTI_SUCCESS) {
        instance_->processActivity(record);
    }
    
    if (status != CUPTI_ERROR_MAX_LIMIT_REACHED) {
        const char* errstr;
        cuptiGetResultString(status, &errstr);
        std::cerr << "CUPTI: Error processing activity buffer: " << errstr << std::endl;
    }
    
    // Free the buffer
    free(buffer);
}

void CUPTIAPI CUPTIProfiler::callbackHandler(void* userdata, 
                                              CUpti_CallbackDomain domain,
                                              CUpti_CallbackId cbid, 
                                              const void* cbdata) {
    CUPTIProfiler* self = static_cast<CUPTIProfiler*>(userdata);
    if (!self || !self->capturing_) {
        return;
    }
    
    // Handle runtime API callbacks for launch tracking
    if (domain == CUPTI_CB_DOMAIN_RUNTIME_API) {
        const CUpti_CallbackData* cbInfo = static_cast<const CUpti_CallbackData*>(cbdata);
        
        // Track kernel launches
        if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {
            if (cbInfo->callbackSite == CUPTI_API_ENTER) {
                // Record correlation ID -> timestamp mapping
                std::lock_guard<std::mutex> lock(self->correlation_mutex_);
                auto now = std::chrono::high_resolution_clock::now();
                self->kernel_start_times_[cbInfo->correlationId] = 
                    static_cast<Timestamp>(now.time_since_epoch().count());
            }
        }
    }
}

//==============================================================================
// Activity Processing
//==============================================================================

void CUPTIProfiler::processActivity(CUpti_Activity* record) {
    switch (record->kind) {
        case CUPTI_ACTIVITY_KIND_KERNEL:
        case CUPTI_ACTIVITY_KIND_CONCURRENT_KERNEL:
            processKernelActivity((CUpti_ActivityKernel4*)record);
            break;
            
        case CUPTI_ACTIVITY_KIND_MEMCPY:
            processMemcpyActivity((CUpti_ActivityMemcpy*)record);
            break;
            
        case CUPTI_ACTIVITY_KIND_MEMSET:
            processMemsetActivity((CUpti_ActivityMemset*)record);
            break;
            
        case CUPTI_ACTIVITY_KIND_SYNCHRONIZATION:
            processSyncActivity((CUpti_ActivitySynchronization*)record);
            break;
            
        default:
            // Ignore other activity types
            break;
    }
}

void CUPTIProfiler::processKernelActivity(const CUpti_ActivityKernel4* kernel) {
    // Create kernel launch event
    TraceEvent launch_event;
    launch_event.type = EventType::KernelLaunch;
    launch_event.timestamp = static_cast<Timestamp>(kernel->start);
    launch_event.correlation_id = kernel->correlationId;
    launch_event.device_id = kernel->deviceId;
    launch_event.stream_id = kernel->streamId;
    launch_event.name = kernel->name ? kernel->name : "unknown_kernel";
    
    // Kernel parameters
    launch_event.metadata["gridDimX"] = std::to_string(kernel->gridX);
    launch_event.metadata["gridDimY"] = std::to_string(kernel->gridY);
    launch_event.metadata["gridDimZ"] = std::to_string(kernel->gridZ);
    launch_event.metadata["blockDimX"] = std::to_string(kernel->blockX);
    launch_event.metadata["blockDimY"] = std::to_string(kernel->blockY);
    launch_event.metadata["blockDimZ"] = std::to_string(kernel->blockZ);
    launch_event.metadata["dynamicSharedMemory"] = std::to_string(kernel->dynamicSharedMemory);
    launch_event.metadata["staticSharedMemory"] = std::to_string(kernel->staticSharedMemory);
    launch_event.metadata["localMemoryPerThread"] = std::to_string(kernel->localMemoryPerThread);
    launch_event.metadata["localMemoryTotal"] = std::to_string(kernel->localMemoryTotal);
    launch_event.metadata["registersPerThread"] = std::to_string(kernel->registersPerThread);
    
    addEvent(std::move(launch_event));
    
    // Create kernel completion event
    TraceEvent complete_event;
    complete_event.type = EventType::KernelComplete;
    complete_event.timestamp = static_cast<Timestamp>(kernel->end);
    complete_event.correlation_id = kernel->correlationId;
    complete_event.device_id = kernel->deviceId;
    complete_event.stream_id = kernel->streamId;
    complete_event.name = kernel->name ? kernel->name : "unknown_kernel";
    
    // Duration in nanoseconds
    complete_event.metadata["duration_ns"] = std::to_string(kernel->end - kernel->start);
    
    addEvent(std::move(complete_event));
}

void CUPTIProfiler::processMemcpyActivity(const CUpti_ActivityMemcpy* memcpy) {
    TraceEvent event;
    
    // Determine memcpy direction
    switch (memcpy->copyKind) {
        case CUPTI_ACTIVITY_MEMCPY_KIND_HTOD:
            event.type = EventType::MemcpyH2D;
            event.name = "cudaMemcpyHostToDevice";
            break;
        case CUPTI_ACTIVITY_MEMCPY_KIND_DTOH:
            event.type = EventType::MemcpyD2H;
            event.name = "cudaMemcpyDeviceToHost";
            break;
        case CUPTI_ACTIVITY_MEMCPY_KIND_DTOD:
            event.type = EventType::MemcpyD2D;
            event.name = "cudaMemcpyDeviceToDevice";
            break;
        case CUPTI_ACTIVITY_MEMCPY_KIND_HTOH:
            event.type = EventType::MemcpyH2D; // Treat as H2D for simplicity
            event.name = "cudaMemcpyHostToHost";
            break;
        default:
            event.type = EventType::MemcpyH2D;
            event.name = "cudaMemcpy";
            break;
    }
    
    event.timestamp = static_cast<Timestamp>(memcpy->start);
    event.correlation_id = memcpy->correlationId;
    event.device_id = memcpy->deviceId;
    event.stream_id = memcpy->streamId;
    
    // Memory transfer details
    event.metadata["bytes"] = std::to_string(memcpy->bytes);
    event.metadata["duration_ns"] = std::to_string(memcpy->end - memcpy->start);
    event.metadata["srcKind"] = std::to_string(static_cast<uint32_t>(memcpy->srcKind));
    event.metadata["dstKind"] = std::to_string(static_cast<uint32_t>(memcpy->dstKind));
    
    // Bandwidth calculation (bytes per second)
    uint64_t duration_ns = memcpy->end - memcpy->start;
    if (duration_ns > 0) {
        double bandwidth_gbps = (double)memcpy->bytes / duration_ns; // GB/s
        event.metadata["bandwidth_gbps"] = std::to_string(bandwidth_gbps);
    }
    
    addEvent(std::move(event));
}

void CUPTIProfiler::processMemsetActivity(const CUpti_ActivityMemset* memset) {
    TraceEvent event;
    event.type = EventType::MemsetDevice;
    event.name = "cudaMemset";
    event.timestamp = static_cast<Timestamp>(memset->start);
    event.correlation_id = memset->correlationId;
    event.device_id = memset->deviceId;
    event.stream_id = memset->streamId;
    
    event.metadata["bytes"] = std::to_string(memset->bytes);
    event.metadata["value"] = std::to_string(memset->value);
    event.metadata["duration_ns"] = std::to_string(memset->end - memset->start);
    
    addEvent(std::move(event));
}

void CUPTIProfiler::processSyncActivity(const CUpti_ActivitySynchronization* sync) {
    TraceEvent event;
    
    switch (sync->type) {
        case CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_STREAM_SYNCHRONIZE:
            event.type = EventType::StreamSync;
            event.name = "cudaStreamSynchronize";
            break;
        case CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_CONTEXT_SYNCHRONIZE:
            event.type = EventType::DeviceSync;
            event.name = "cudaDeviceSynchronize";
            break;
        case CUPTI_ACTIVITY_SYNCHRONIZATION_TYPE_EVENT_SYNCHRONIZE:
            event.type = EventType::StreamSync;
            event.name = "cudaEventSynchronize";
            break;
        default:
            event.type = EventType::StreamSync;
            event.name = "cudaSync";
            break;
    }
    
    event.timestamp = static_cast<Timestamp>(sync->start);
    event.correlation_id = sync->correlationId;
    event.stream_id = sync->streamId;
    
    event.metadata["duration_ns"] = std::to_string(sync->end - sync->start);
    
    addEvent(std::move(event));
}

#endif // TRACESMITH_ENABLE_CUDA

//==============================================================================
// Thread-Safe Event Storage
//==============================================================================

void CUPTIProfiler::addEvent(TraceEvent&& event) {
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
