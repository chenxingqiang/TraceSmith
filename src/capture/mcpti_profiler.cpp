#include "tracesmith/capture/mcpti_profiler.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace tracesmith {

// Singleton instance for static callbacks
MCPTIProfiler* MCPTIProfiler::instance_ = nullptr;

//==============================================================================
// MCPTI Error Handling Macros
//==============================================================================

#ifdef TRACESMITH_ENABLE_MACA

#define MCPTI_CALL(call)                                                    \
    do {                                                                    \
        MCptiResult _status = call;                                         \
        if (_status != MCPTI_SUCCESS) {                                     \
            const char* errstr;                                             \
            mcptiGetResultString(_status, &errstr);                         \
            std::cerr << "MCPTI error: " << errstr << " at " << __FILE__   \
                      << ":" << __LINE__ << std::endl;                      \
            return false;                                                   \
        }                                                                   \
    } while (0)

#define MCPTI_CALL_VOID(call)                                               \
    do {                                                                    \
        MCptiResult _status = call;                                         \
        if (_status != MCPTI_SUCCESS) {                                     \
            const char* errstr;                                             \
            mcptiGetResultString(_status, &errstr);                         \
            std::cerr << "MCPTI error: " << errstr << " at " << __FILE__   \
                      << ":" << __LINE__ << std::endl;                      \
        }                                                                   \
    } while (0)

#define MCC_CALL(call)                                                      \
    do {                                                                    \
        mcError_t _status = call;                                           \
        if (_status != mcSuccess) {                                         \
            const char* errstr = mcGetErrorString(_status);                 \
            std::cerr << "MACA error: " << (errstr ? errstr : "unknown")   \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl;\
            return false;                                                   \
        }                                                                   \
    } while (0)

#endif // TRACESMITH_ENABLE_MACA

//==============================================================================
// Constructor / Destructor
//==============================================================================

MCPTIProfiler::MCPTIProfiler()
    : initialized_(false)
    , capturing_(false)
    , events_captured_(0)
    , events_dropped_(0)
    , correlation_counter_(0)
#ifdef TRACESMITH_ENABLE_MACA
    , subscriber_(nullptr)
    , buffer_size_(DEFAULT_BUFFER_SIZE)
#endif
{
    instance_ = this;
}

MCPTIProfiler::~MCPTIProfiler() {
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

bool MCPTIProfiler::isAvailable() const {
#ifdef TRACESMITH_ENABLE_MACA
    return isMACAAvailable();
#else
    return false;
#endif
}

bool isMACAAvailable() {
#ifdef TRACESMITH_ENABLE_MACA
    mcError_t result = mcInit(0);
    if (result != mcSuccess) {
        return false;
    }
    
    int device_count = 0;
    result = mcGetDeviceCount(&device_count);
    return (result == mcSuccess && device_count > 0);
#else
    return false;
#endif
}

int getMACADriverVersion() {
#ifdef TRACESMITH_ENABLE_MACA
    int version = 0;
    if (mcDriverGetVersion(&version) == mcSuccess) {
        return version;
    }
#endif
    return 0;
}

int getMACADeviceCount() {
#ifdef TRACESMITH_ENABLE_MACA
    int count = 0;
    if (mcInit(0) == mcSuccess) {
        mcGetDeviceCount(&count);
    }
    return count;
#else
    return 0;
#endif
}

//==============================================================================
// Initialization
//==============================================================================

bool MCPTIProfiler::initialize(const ProfilerConfig& config) {
#ifdef TRACESMITH_ENABLE_MACA
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    // Initialize MACA driver API
    MCC_CALL(mcInit(0));
    
    // Subscribe to MCPTI
    MCPTI_CALL(mcptiSubscribe(&subscriber_, 
                              (MCpti_CallbackFunc)callbackHandler, 
                              this));
    
    // Set default enabled activities
    enabled_activities_ = {
        MCPTI_ACTIVITY_KIND_KERNEL,
        MCPTI_ACTIVITY_KIND_MEMCPY,
        MCPTI_ACTIVITY_KIND_MEMSET,
        MCPTI_ACTIVITY_KIND_SYNCHRONIZATION
    };
    
    // Register buffer callbacks
    MCPTI_CALL(mcptiActivityRegisterCallbacks(bufferRequested, bufferCompleted));
    
    initialized_ = true;
    return true;
#else
    std::cerr << "TraceSmith was compiled without MACA/MetaX support" << std::endl;
    return false;
#endif
}

void MCPTIProfiler::finalize() {
#ifdef TRACESMITH_ENABLE_MACA
    if (!initialized_) {
        return;
    }
    
    if (capturing_) {
        stopCapture();
    }
    
    if (subscriber_) {
        MCPTI_CALL_VOID(mcptiUnsubscribe(subscriber_));
        subscriber_ = nullptr;
    }
    
    initialized_ = false;
#endif
}

//==============================================================================
// Capture Control
//==============================================================================

bool MCPTIProfiler::startCapture() {
#ifdef TRACESMITH_ENABLE_MACA
    if (!initialized_) {
        std::cerr << "MCPTIProfiler not initialized" << std::endl;
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
        MCPTI_CALL(mcptiActivityEnable(kind));
    }
    
    // Enable callback domain for runtime API (optional, for launch tracking)
    MCPTI_CALL(mcptiEnableDomain(1, subscriber_, MCPTI_CB_DOMAIN_RUNTIME_API));
    
    capturing_ = true;
    return true;
#else
    return false;
#endif
}

bool MCPTIProfiler::stopCapture() {
#ifdef TRACESMITH_ENABLE_MACA
    if (!capturing_) {
        return true;
    }
    
    capturing_ = false;
    
    // Flush all activity buffers
    MCPTI_CALL(mcptiActivityFlushAll(MCPTI_ACTIVITY_FLAG_FLUSH_FORCED));
    
    // Disable activity kinds
    for (auto kind : enabled_activities_) {
        MCPTI_CALL_VOID(mcptiActivityDisable(kind));
    }
    
    // Disable callback domain
    MCPTI_CALL_VOID(mcptiEnableDomain(0, subscriber_, MCPTI_CB_DOMAIN_RUNTIME_API));
    
    return true;
#else
    return false;
#endif
}

//==============================================================================
// Event Retrieval
//==============================================================================

size_t MCPTIProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
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

std::vector<DeviceInfo> MCPTIProfiler::getDeviceInfo() const {
    std::vector<DeviceInfo> devices;
    
#ifdef TRACESMITH_ENABLE_MACA
    int device_count = 0;
    if (mcGetDeviceCount(&device_count) != mcSuccess) {
        return devices;
    }
    
    for (int i = 0; i < device_count; ++i) {
        DeviceInfo info;
        info.device_id = i;
        info.vendor = "MetaX";
        
        // Get device properties
        mcDeviceProp_t prop;
        if (mcGetDeviceProperties(&prop, i) == mcSuccess) {
            info.name = prop.name;
            info.compute_major = prop.major;
            info.compute_minor = prop.minor;
            info.total_memory = prop.totalGlobalMem;
            info.multiprocessor_count = prop.multiProcessorCount;
            info.clock_rate = prop.clockRate; // kHz
        } else {
            info.name = "MetaX GPU";
        }
        
        devices.push_back(info);
    }
#endif
    
    return devices;
}

//==============================================================================
// Callback Registration
//==============================================================================

void MCPTIProfiler::setEventCallback(EventCallback callback) {
    callback_ = std::move(callback);
}

//==============================================================================
// MCPTI Configuration
//==============================================================================

#ifdef TRACESMITH_ENABLE_MACA

void MCPTIProfiler::setBufferSize(size_t size_bytes) {
    buffer_size_ = size_bytes;
}

void MCPTIProfiler::enableActivityKind(MCpti_ActivityKind kind, bool enable) {
    auto it = std::find(enabled_activities_.begin(), enabled_activities_.end(), kind);
    
    if (enable && it == enabled_activities_.end()) {
        enabled_activities_.push_back(kind);
    } else if (!enable && it != enabled_activities_.end()) {
        enabled_activities_.erase(it);
    }
}

uint32_t MCPTIProfiler::getMcptiVersion() const {
    uint32_t version = 0;
    mcptiGetVersion(&version);
    return version;
}

//==============================================================================
// MCPTI Buffer Callbacks (Static)
//==============================================================================

void MCPTIAPI MCPTIProfiler::bufferRequested(uint8_t** buffer, size_t* size, 
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
        std::cerr << "MCPTI: Failed to allocate activity buffer" << std::endl;
        *size = 0;
        *maxNumRecords = 0;
        return;
    }
    
    *maxNumRecords = 0; // No limit on records per buffer
}

void MCPTIAPI MCPTIProfiler::bufferCompleted(MCcontext ctx, uint32_t streamId,
                                              uint8_t* buffer, size_t size, 
                                              size_t validSize) {
    if (!instance_ || !buffer) {
        free(buffer);
        return;
    }
    
    // Process all activities in the buffer
    MCpti_Activity* record = nullptr;
    MCptiResult status;
    
    while ((status = mcptiActivityGetNextRecord(buffer, validSize, &record)) 
           == MCPTI_SUCCESS) {
        instance_->processActivity(record);
    }
    
    if (status != MCPTI_ERROR_MAX_LIMIT_REACHED) {
        const char* errstr;
        mcptiGetResultString(status, &errstr);
        std::cerr << "MCPTI: Error processing activity buffer: " << errstr << std::endl;
    }
    
    // Free the buffer
    free(buffer);
}

void MCPTIAPI MCPTIProfiler::callbackHandler(void* userdata, 
                                              MCpti_CallbackDomain domain,
                                              MCpti_CallbackId cbid, 
                                              const void* cbdata) {
    MCPTIProfiler* self = static_cast<MCPTIProfiler*>(userdata);
    if (!self || !self->capturing_) {
        return;
    }
    
    // Handle runtime API callbacks for launch tracking
    if (domain == MCPTI_CB_DOMAIN_RUNTIME_API) {
        const MCpti_CallbackData* cbInfo = static_cast<const MCpti_CallbackData*>(cbdata);
        
        // Track kernel launches (MACA uses similar API)
        if (cbInfo->callbackSite == MCPTI_API_ENTER) {
            // Record correlation ID -> timestamp mapping
            std::lock_guard<std::mutex> lock(self->correlation_mutex_);
            auto now = std::chrono::high_resolution_clock::now();
            self->kernel_start_times_[cbInfo->correlationId] = 
                static_cast<Timestamp>(now.time_since_epoch().count());
        }
    }
}

//==============================================================================
// Activity Processing
//==============================================================================

void MCPTIProfiler::processActivity(MCpti_Activity* record) {
    switch (record->kind) {
        case MCPTI_ACTIVITY_KIND_KERNEL:
        case MCPTI_ACTIVITY_KIND_CONCURRENT_KERNEL:
            processKernelActivity((MCpti_ActivityKernel4*)record);
            break;
            
        case MCPTI_ACTIVITY_KIND_MEMCPY:
            processMemcpyActivity((MCpti_ActivityMemcpy*)record);
            break;
            
        case MCPTI_ACTIVITY_KIND_MEMSET:
            processMemsetActivity((MCpti_ActivityMemset*)record);
            break;
            
        case MCPTI_ACTIVITY_KIND_SYNCHRONIZATION:
            processSyncActivity((MCpti_ActivitySynchronization*)record);
            break;
            
        default:
            // Ignore other activity types
            break;
    }
}

void MCPTIProfiler::processKernelActivity(const MCpti_ActivityKernel4* kernel) {
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
    launch_event.metadata["platform"] = "MetaX";
    
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
    complete_event.duration = kernel->end - kernel->start;
    complete_event.metadata["duration_ns"] = std::to_string(kernel->end - kernel->start);
    
    addEvent(std::move(complete_event));
}

void MCPTIProfiler::processMemcpyActivity(const MCpti_ActivityMemcpy* memcpy) {
    TraceEvent event;
    
    // Determine memcpy direction
    switch (memcpy->copyKind) {
        case MCPTI_ACTIVITY_MEMCPY_KIND_HTOD:
            event.type = EventType::MemcpyH2D;
            event.name = "mcMemcpyHostToDevice";
            break;
        case MCPTI_ACTIVITY_MEMCPY_KIND_DTOH:
            event.type = EventType::MemcpyD2H;
            event.name = "mcMemcpyDeviceToHost";
            break;
        case MCPTI_ACTIVITY_MEMCPY_KIND_DTOD:
            event.type = EventType::MemcpyD2D;
            event.name = "mcMemcpyDeviceToDevice";
            break;
        case MCPTI_ACTIVITY_MEMCPY_KIND_HTOH:
            event.type = EventType::MemcpyH2D; // Treat as H2D for simplicity
            event.name = "mcMemcpyHostToHost";
            break;
        default:
            event.type = EventType::MemcpyH2D;
            event.name = "mcMemcpy";
            break;
    }
    
    event.timestamp = static_cast<Timestamp>(memcpy->start);
    event.duration = memcpy->end - memcpy->start;
    event.correlation_id = memcpy->correlationId;
    event.device_id = memcpy->deviceId;
    event.stream_id = memcpy->streamId;
    
    // Memory transfer details
    event.metadata["bytes"] = std::to_string(memcpy->bytes);
    event.metadata["duration_ns"] = std::to_string(memcpy->end - memcpy->start);
    event.metadata["srcKind"] = std::to_string(static_cast<uint32_t>(memcpy->srcKind));
    event.metadata["dstKind"] = std::to_string(static_cast<uint32_t>(memcpy->dstKind));
    event.metadata["platform"] = "MetaX";
    
    // Bandwidth calculation (bytes per second)
    uint64_t duration_ns = memcpy->end - memcpy->start;
    if (duration_ns > 0) {
        double bandwidth_gbps = (double)memcpy->bytes / duration_ns; // GB/s
        event.metadata["bandwidth_gbps"] = std::to_string(bandwidth_gbps);
    }
    
    addEvent(std::move(event));
}

void MCPTIProfiler::processMemsetActivity(const MCpti_ActivityMemset* memset) {
    TraceEvent event;
    event.type = EventType::MemsetDevice;
    event.name = "mcMemset";
    event.timestamp = static_cast<Timestamp>(memset->start);
    event.duration = memset->end - memset->start;
    event.correlation_id = memset->correlationId;
    event.device_id = memset->deviceId;
    event.stream_id = memset->streamId;
    
    event.metadata["bytes"] = std::to_string(memset->bytes);
    event.metadata["value"] = std::to_string(memset->value);
    event.metadata["duration_ns"] = std::to_string(memset->end - memset->start);
    event.metadata["platform"] = "MetaX";
    
    addEvent(std::move(event));
}

void MCPTIProfiler::processSyncActivity(const MCpti_ActivitySynchronization* sync) {
    TraceEvent event;
    
    switch (sync->type) {
        case MCPTI_ACTIVITY_SYNCHRONIZATION_TYPE_STREAM_SYNCHRONIZE:
            event.type = EventType::StreamSync;
            event.name = "mcStreamSynchronize";
            break;
        case MCPTI_ACTIVITY_SYNCHRONIZATION_TYPE_CONTEXT_SYNCHRONIZE:
            event.type = EventType::DeviceSync;
            event.name = "mcDeviceSynchronize";
            break;
        case MCPTI_ACTIVITY_SYNCHRONIZATION_TYPE_EVENT_SYNCHRONIZE:
            event.type = EventType::StreamSync;
            event.name = "mcEventSynchronize";
            break;
        default:
            event.type = EventType::StreamSync;
            event.name = "mcSync";
            break;
    }
    
    event.timestamp = static_cast<Timestamp>(sync->start);
    event.duration = sync->end - sync->start;
    event.correlation_id = sync->correlationId;
    event.stream_id = sync->streamId;
    
    event.metadata["duration_ns"] = std::to_string(sync->end - sync->start);
    event.metadata["platform"] = "MetaX";
    
    addEvent(std::move(event));
}

#endif // TRACESMITH_ENABLE_MACA

//==============================================================================
// Thread-Safe Event Storage
//==============================================================================

void MCPTIProfiler::addEvent(TraceEvent&& event) {
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
