/**
 * Tracy GPU Context Implementation
 * 
 * Provides full GPU timeline support in Tracy for Ascend, MetaX, and other
 * GPU platforms that Tracy doesn't natively support.
 */

#include "tracesmith/tracy/tracy_gpu_context.hpp"
#include "tracesmith/capture/profiler.hpp"

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include <cstring>
#include <memory>

namespace tracesmith {
namespace tracy {

// =============================================================================
// Source Location Cache
// =============================================================================

TracyGpuContext::SourceLocationCache& TracyGpuContext::getSourceLocationCache() {
    static SourceLocationCache cache;
    return cache;
}

::tracy::SourceLocationData* TracyGpuContext::SourceLocationCache::getOrCreate(
    const std::string& name, uint32_t color) {
#ifdef TRACY_ENABLE
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = locations.find(name);
    if (it != locations.end()) {
        return it->second;
    }
    
    // Allocate persistent storage for the source location
    // Tracy requires these to remain valid for the lifetime of the profiler
    char* name_copy = new char[name.size() + 1];
    std::strcpy(name_copy, name.c_str());
    
    auto* loc = new ::tracy::SourceLocationData{
        name_copy,      // name
        __FUNCTION__,   // function
        __FILE__,       // file
        __LINE__,       // line
        color           // color
    };
    
    locations[name] = loc;
    return loc;
#else
    (void)name;
    (void)color;
    return nullptr;
#endif
}

TracyGpuContext::SourceLocationCache::~SourceLocationCache() {
#ifdef TRACY_ENABLE
    for (auto& [name, loc] : locations) {
        delete[] loc->name;
        delete loc;
    }
#endif
}

// =============================================================================
// TracyGpuContext Implementation
// =============================================================================

TracyGpuContext::TracyGpuContext(const std::string& name,
                                   GpuContextType type,
                                   uint32_t device_id)
    : name_(name)
    , type_(type)
    , device_id_(device_id)
{
#ifdef TRACY_ENABLE
    // Allocate a new GPU context ID
    static std::atomic<uint8_t> next_context_id{0};
    context_id_ = next_context_id.fetch_add(1);
    
    if (context_id_ >= 255) {
        // Tracy supports max 255 GPU contexts
        context_id_ = 255;
        return;
    }
    
    // Get current timestamps for initial calibration
    int64_t cpu_time = ::tracy::Profiler::GetTime();
    int64_t gpu_time = cpu_time; // Initial assumption: GPU time = CPU time
    
    // Announce the new GPU context to Tracy
    auto* item = ::tracy::Profiler::QueueSerial();
    ::tracy::MemWrite(&item->hdr.type, ::tracy::QueueType::GpuNewContext);
    ::tracy::MemWrite(&item->gpuNewContext.cpuTime, cpu_time);
    ::tracy::MemWrite(&item->gpuNewContext.gpuTime, gpu_time);
    ::tracy::MemWrite(&item->gpuNewContext.thread, uint32_t(0));
    ::tracy::MemWrite(&item->gpuNewContext.period, clock_period_);
    
    // Map our context type to Tracy's GPU context type
    // Tracy uses GpuContextType enum internally
    ::tracy::GpuContextType tracy_type;
    switch (type) {
        case GpuContextType::CUDA:
            tracy_type = ::tracy::GpuContextType::CUDA;
            break;
        case GpuContextType::Vulkan:
            tracy_type = ::tracy::GpuContextType::Vulkan;
            break;
        case GpuContextType::OpenGL:
            tracy_type = ::tracy::GpuContextType::OpenGl;
            break;
        case GpuContextType::Direct3D11:
            tracy_type = ::tracy::GpuContextType::Direct3D11;
            break;
        case GpuContextType::Direct3D12:
            tracy_type = ::tracy::GpuContextType::Direct3D12;
            break;
        case GpuContextType::OpenCL:
            tracy_type = ::tracy::GpuContextType::OpenCL;
            break;
        case GpuContextType::Metal:
            tracy_type = ::tracy::GpuContextType::Metal;
            break;
        // For Ascend, MetaX, ROCm - use OpenCL type as it's most generic
        case GpuContextType::Ascend:
        case GpuContextType::MACA:
        case GpuContextType::ROCm:
        case GpuContextType::Generic:
        default:
            tracy_type = ::tracy::GpuContextType::OpenCL;
            break;
    }
    
    ::tracy::MemWrite(&item->gpuNewContext.type, tracy_type);
    ::tracy::MemWrite(&item->gpuNewContext.context, context_id_);
    ::tracy::MemWrite(&item->gpuNewContext.flags, ::tracy::GpuContextCalibration);
    ::tracy::Profiler::QueueSerialFinish();
    
    // Set context name
    if (!name_.empty()) {
        auto* name_ptr = reinterpret_cast<char*>(::tracy::tracy_malloc(name_.size()));
        std::memcpy(name_ptr, name_.c_str(), name_.size());
        
        auto* name_item = ::tracy::Profiler::QueueSerial();
        ::tracy::MemWrite(&name_item->hdr.type, ::tracy::QueueType::GpuContextName);
        ::tracy::MemWrite(&name_item->gpuContextNameFat.context, context_id_);
        ::tracy::MemWrite(&name_item->gpuContextNameFat.ptr, reinterpret_cast<uint64_t>(name_ptr));
        ::tracy::MemWrite(&name_item->gpuContextNameFat.size, static_cast<uint16_t>(name_.size()));
        ::tracy::Profiler::QueueSerialFinish();
    }
    
    // Initial calibration
    last_calibration_cpu_ = cpu_time;
    last_calibration_gpu_ = gpu_time;
#endif
}

TracyGpuContext::~TracyGpuContext() {
    // GPU contexts in Tracy are persistent, no cleanup needed
}

uint16_t TracyGpuContext::allocateQueryId() {
    return query_counter_.fetch_add(2);
}

void TracyGpuContext::calibrate(int64_t cpu_time, int64_t gpu_time) {
#ifdef TRACY_ENABLE
    if (!isValid()) return;
    
    int64_t delta = gpu_time - last_calibration_gpu_;
    if (delta <= 0) return;
    
    last_calibration_cpu_ = cpu_time;
    last_calibration_gpu_ = gpu_time;
    
    auto* item = ::tracy::Profiler::QueueSerial();
    ::tracy::MemWrite(&item->hdr.type, ::tracy::QueueType::GpuCalibration);
    ::tracy::MemWrite(&item->gpuCalibration.gpuTime, gpu_time);
    ::tracy::MemWrite(&item->gpuCalibration.cpuTime, cpu_time);
    ::tracy::MemWrite(&item->gpuCalibration.cpuDelta, delta);
    ::tracy::MemWrite(&item->gpuCalibration.context, context_id_);
    ::tracy::Profiler::QueueSerialFinish();
#else
    (void)cpu_time;
    (void)gpu_time;
#endif
}

void TracyGpuContext::emitZoneInternal(const char* zone_name, size_t name_len,
                                        int64_t cpu_start, int64_t cpu_end,
                                        int64_t gpu_start, int64_t gpu_end,
                                        uint32_t thread_id, uint32_t color) {
#ifdef TRACY_ENABLE
    if (!isValid()) return;
    
    // Get or create source location
    std::string name_str(zone_name, name_len);
    auto* src_loc = getSourceLocationCache().getOrCreate(name_str, color);
    
    // Allocate query IDs
    uint16_t query_id = allocateQueryId();
    
    // Get thread ID if not provided
    if (thread_id == 0) {
        thread_id = ::tracy::GetThreadHandle();
    }
    
    // Emit GPU zone begin
    {
        auto* item = ::tracy::Profiler::QueueSerial();
        ::tracy::MemWrite(&item->hdr.type, ::tracy::QueueType::GpuZoneBeginSerial);
        ::tracy::MemWrite(&item->gpuZoneBegin.cpuTime, cpu_start);
        ::tracy::MemWrite(&item->gpuZoneBegin.srcloc, reinterpret_cast<uint64_t>(src_loc));
        ::tracy::MemWrite(&item->gpuZoneBegin.thread, thread_id);
        ::tracy::MemWrite(&item->gpuZoneBegin.queryId, static_cast<uint16_t>(query_id));
        ::tracy::MemWrite(&item->gpuZoneBegin.context, context_id_);
        ::tracy::Profiler::QueueSerialFinish();
    }
    
    // Emit GPU zone end
    {
        auto* item = ::tracy::Profiler::QueueSerial();
        ::tracy::MemWrite(&item->hdr.type, ::tracy::QueueType::GpuZoneEndSerial);
        ::tracy::MemWrite(&item->gpuZoneEnd.cpuTime, cpu_end);
        ::tracy::MemWrite(&item->gpuZoneEnd.thread, thread_id);
        ::tracy::MemWrite(&item->gpuZoneEnd.queryId, static_cast<uint16_t>(query_id + 1));
        ::tracy::MemWrite(&item->gpuZoneEnd.context, context_id_);
        ::tracy::Profiler::QueueSerialFinish();
    }
    
    // Submit GPU timestamps
    {
        auto* item = ::tracy::Profiler::QueueSerial();
        ::tracy::MemWrite(&item->hdr.type, ::tracy::QueueType::GpuTime);
        ::tracy::MemWrite(&item->gpuTime.gpuTime, gpu_start);
        ::tracy::MemWrite(&item->gpuTime.queryId, static_cast<uint16_t>(query_id));
        ::tracy::MemWrite(&item->gpuTime.context, context_id_);
        ::tracy::Profiler::QueueSerialFinish();
    }
    
    {
        auto* item = ::tracy::Profiler::QueueSerial();
        ::tracy::MemWrite(&item->hdr.type, ::tracy::QueueType::GpuTime);
        ::tracy::MemWrite(&item->gpuTime.gpuTime, gpu_end);
        ::tracy::MemWrite(&item->gpuTime.queryId, static_cast<uint16_t>(query_id + 1));
        ::tracy::MemWrite(&item->gpuTime.context, context_id_);
        ::tracy::Profiler::QueueSerialFinish();
    }
    
    zones_emitted_.fetch_add(1);
#else
    (void)zone_name;
    (void)name_len;
    (void)cpu_start;
    (void)cpu_end;
    (void)gpu_start;
    (void)gpu_end;
    (void)thread_id;
    (void)color;
#endif
}

void TracyGpuContext::emitGpuZone(const char* zone_name,
                                   int64_t cpu_start,
                                   int64_t cpu_end,
                                   int64_t gpu_start,
                                   int64_t gpu_end,
                                   uint32_t thread_id,
                                   uint32_t color) {
    emitZoneInternal(zone_name, std::strlen(zone_name),
                     cpu_start, cpu_end, gpu_start, gpu_end,
                     thread_id, color);
}

void TracyGpuContext::emitGpuZone(const TraceEvent& event) {
    // Determine color based on event type
    uint32_t color = 0;
    switch (event.type) {
        case EventType::KernelLaunch:
        case EventType::KernelComplete:
            color = 0xFF4444; // Red
            break;
        case EventType::MemcpyH2D:
            color = 0x4444FF; // Blue
            break;
        case EventType::MemcpyD2H:
            color = 0xFF44FF; // Magenta
            break;
        case EventType::MemcpyD2D:
            color = 0x44FFFF; // Cyan
            break;
        case EventType::StreamSync:
        case EventType::DeviceSync:
            color = 0x8844FF; // Purple
            break;
        default:
            color = 0x888888; // Gray
            break;
    }
    
    // Calculate timestamps
    int64_t cpu_start = static_cast<int64_t>(event.timestamp);
    int64_t cpu_end = cpu_start + static_cast<int64_t>(event.duration);
    
    // Use CPU timestamps as GPU timestamps if not available
    // In practice, TraceSmith should provide accurate GPU timestamps
    int64_t gpu_start = cpu_start;
    int64_t gpu_end = cpu_end;
    
    emitZoneInternal(event.name.c_str(), event.name.size(),
                     cpu_start, cpu_end, gpu_start, gpu_end,
                     event.thread_id, color);
}

void TracyGpuContext::emitGpuZones(const std::vector<TraceEvent>& events) {
    for (const auto& event : events) {
        // Only emit GPU-related events
        switch (event.type) {
            case EventType::KernelLaunch:
            case EventType::KernelComplete:
            case EventType::MemcpyH2D:
            case EventType::MemcpyD2H:
            case EventType::MemcpyD2D:
            case EventType::MemsetDevice:
            case EventType::StreamSync:
            case EventType::DeviceSync:
                emitGpuZone(event);
                break;
            default:
                // Skip non-GPU events
                break;
        }
    }
}

// =============================================================================
// TracyGpuZoneEmitter Implementation
// =============================================================================

TracyGpuZoneEmitter::TracyGpuZoneEmitter(TracyGpuContext& context,
                                          const char* name,
                                          uint32_t color)
    : context_(context)
    , name_(name)
    , color_(color)
{
#ifdef TRACY_ENABLE
    cpu_start_ = ::tracy::Profiler::GetTime();
#else
    cpu_start_ = static_cast<int64_t>(getCurrentTimestamp());
#endif
}

TracyGpuZoneEmitter::~TracyGpuZoneEmitter() {
#ifdef TRACY_ENABLE
    int64_t cpu_end = ::tracy::Profiler::GetTime();
#else
    int64_t cpu_end = static_cast<int64_t>(getCurrentTimestamp());
#endif
    
    int64_t gpu_start = gpu_timestamps_set_ ? gpu_start_ : cpu_start_;
    int64_t gpu_end = gpu_timestamps_set_ ? gpu_end_ : cpu_end;
    
    context_.emitGpuZone(name_, cpu_start_, cpu_end, gpu_start, gpu_end, 0, color_);
}

void TracyGpuZoneEmitter::setGpuTimestamps(int64_t gpu_start, int64_t gpu_end) {
    gpu_start_ = gpu_start;
    gpu_end_ = gpu_end;
    gpu_timestamps_set_ = true;
}

// =============================================================================
// Global GPU Context Management
// =============================================================================

namespace {
    std::mutex g_context_mutex;
    std::unordered_map<uint64_t, std::unique_ptr<TracyGpuContext>> g_contexts;
    
    uint64_t makeContextKey(GpuContextType type, uint32_t device_id) {
        return (static_cast<uint64_t>(type) << 32) | device_id;
    }
}

TracyGpuContext& getOrCreateGpuContext(const std::string& name,
                                        GpuContextType type,
                                        uint32_t device_id) {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    
    uint64_t key = makeContextKey(type, device_id);
    auto it = g_contexts.find(key);
    
    if (it != g_contexts.end()) {
        return *it->second;
    }
    
    auto ctx = std::make_unique<TracyGpuContext>(name, type, device_id);
    auto& ref = *ctx;
    g_contexts[key] = std::move(ctx);
    return ref;
}

TracyGpuContext& getOrCreateGpuContext(PlatformType platform, uint32_t device_id) {
    GpuContextType type = platformToGpuContextType(platform);
    
    std::string name;
    switch (platform) {
        case PlatformType::CUDA:
            name = "CUDA GPU " + std::to_string(device_id);
            break;
        case PlatformType::Metal:
            name = "Metal GPU " + std::to_string(device_id);
            break;
        case PlatformType::MACA:
            name = "MetaX GPU " + std::to_string(device_id);
            break;
        case PlatformType::Ascend:
            name = "Ascend NPU " + std::to_string(device_id);
            break;
        case PlatformType::ROCm:
            name = "AMD GPU " + std::to_string(device_id);
            break;
        default:
            name = "GPU " + std::to_string(device_id);
            break;
    }
    
    return getOrCreateGpuContext(name, type, device_id);
}

void destroyAllGpuContexts() {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    g_contexts.clear();
}

} // namespace tracy
} // namespace tracesmith
