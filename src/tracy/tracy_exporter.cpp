/**
 * Tracy Exporter Implementation
 * 
 * This file implements the TracyExporter class that bridges TraceSmith
 * events to Tracy profiler for real-time visualization.
 */

#include "tracesmith/tracy/tracy_exporter.hpp"

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include <algorithm>
#include <sstream>

namespace tracesmith {
namespace tracy {

// =============================================================================
// TracyExporter Implementation
// =============================================================================

TracyExporter::TracyExporter() 
    : config_()
{
}

TracyExporter::TracyExporter(const TracyExporterConfig& config)
    : config_(config)
{
}

TracyExporter::~TracyExporter() {
    shutdown();
}

TracyExporter::TracyExporter(TracyExporter&& other) noexcept
    : config_(std::move(other.config_))
    , initialized_(other.initialized_)
    , events_emitted_(other.events_emitted_.load())
    , gpu_zones_emitted_(other.gpu_zones_emitted_.load())
    , query_id_counter_(other.query_id_counter_.load())
    , gpu_contexts_(std::move(other.gpu_contexts_))
    , configured_plots_(std::move(other.configured_plots_))
{
    other.initialized_ = false;
}

TracyExporter& TracyExporter::operator=(TracyExporter&& other) noexcept {
    if (this != &other) {
        shutdown();
        config_ = std::move(other.config_);
        initialized_ = other.initialized_;
        events_emitted_.store(other.events_emitted_.load());
        gpu_zones_emitted_.store(other.gpu_zones_emitted_.load());
        query_id_counter_.store(other.query_id_counter_.load());
        gpu_contexts_ = std::move(other.gpu_contexts_);
        configured_plots_ = std::move(other.configured_plots_);
        other.initialized_ = false;
    }
    return *this;
}

bool TracyExporter::initialize() {
    if (initialized_) {
        return true;
    }
    
#ifdef TRACY_ENABLE
    // Set up default plots if configured
    if (config_.auto_configure_plots) {
        setupDefaultPlots();
    }
    
    // Set application info
    std::string app_info = "TraceSmith GPU Profiler";
    setAppInfo(app_info);
    
    initialized_ = true;
    return true;
#else
    // Tracy not enabled at compile time
    return false;
#endif
}

void TracyExporter::shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Clean up GPU contexts
    {
        std::lock_guard<std::mutex> lock(gpu_context_mutex_);
        gpu_contexts_.clear();
    }
    
    initialized_ = false;
}

bool TracyExporter::isConnected() const {
#ifdef TRACY_ENABLE
    return ::tracy::GetProfiler().IsConnected();
#else
    return false;
#endif
}

void TracyExporter::emitEvent(const TraceEvent& event) {
    if (!initialized_) {
        return;
    }
    
    // Filter events based on configuration
    bool should_emit = false;
    switch (event.type) {
        case EventType::KernelLaunch:
        case EventType::KernelComplete:
            should_emit = config_.emit_kernel_events;
            break;
        case EventType::MemcpyH2D:
        case EventType::MemcpyD2H:
        case EventType::MemcpyD2D:
            should_emit = config_.emit_memcpy_events;
            break;
        case EventType::StreamSync:
        case EventType::DeviceSync:
            should_emit = config_.emit_sync_events;
            break;
        case EventType::MemAlloc:
        case EventType::MemFree:
            should_emit = config_.emit_alloc_events;
            break;
        default:
            should_emit = true;
            break;
    }
    
    if (should_emit) {
        emitTraceEventInternal(event);
        events_emitted_.fetch_add(1);
    }
}

void TracyExporter::emitMemoryEvent(const MemoryEvent& event) {
    if (!initialized_ || !config_.enable_memory_tracking) {
        return;
    }
    
    emitMemoryToTracy(event);
    events_emitted_.fetch_add(1);
}

void TracyExporter::emitCounterEvent(const CounterEvent& event) {
    if (!initialized_ || !config_.enable_counters) {
        return;
    }
    
    emitCounterToTracy(event);
    events_emitted_.fetch_add(1);
}

void TracyExporter::exportEvents(const std::vector<TraceEvent>& events) {
    if (!initialized_) {
        return;
    }
    
    for (const auto& event : events) {
        emitEvent(event);
    }
}

void TracyExporter::exportTraceRecord(const TraceRecord& record) {
    if (!initialized_) {
        return;
    }
    
    // Export metadata as app info
    const auto& meta = record.metadata();
    if (!meta.application_name.empty()) {
        setAppInfo(meta.application_name);
    }
    
    // Create GPU contexts for devices
    for (const auto& device : meta.devices) {
        createGpuContext(device.device_id, device.name);
    }
    
    // Export all events
    exportEvents(record.events());
}

uint8_t TracyExporter::createGpuContext(uint32_t device_id, const std::string& name) {
    std::lock_guard<std::mutex> lock(gpu_context_mutex_);
    
    // Check if context already exists
    auto it = gpu_contexts_.find(device_id);
    if (it != gpu_contexts_.end()) {
        return it->second;
    }
    
#ifdef TRACY_ENABLE
    // Allocate new context ID
    static std::atomic<uint8_t> next_context_id{0};
    uint8_t context_id = next_context_id.fetch_add(1);
    
    if (context_id >= 255) {
        // Tracy supports max 255 GPU contexts
        return 255;
    }
    
    gpu_contexts_[device_id] = context_id;
    
    // Log context creation
    std::string ctx_name = name.empty() 
        ? config_.gpu_context_name + " " + std::to_string(device_id)
        : name;
    logMessage("Created GPU context: " + ctx_name, colors::Default);
    
    return context_id;
#else
    return 0;
#endif
}

void TracyExporter::destroyGpuContext(uint8_t context_id) {
    std::lock_guard<std::mutex> lock(gpu_context_mutex_);
    
    for (auto it = gpu_contexts_.begin(); it != gpu_contexts_.end(); ) {
        if (it->second == context_id) {
            it = gpu_contexts_.erase(it);
        } else {
            ++it;
        }
    }
}

void TracyExporter::emitGpuZone(uint8_t context_id,
                                 const std::string& name,
                                 Timestamp cpu_start,
                                 Timestamp cpu_end,
                                 Timestamp gpu_start,
                                 Timestamp gpu_end,
                                 uint32_t color) {
    if (!initialized_ || !config_.enable_gpu_zones) {
        return;
    }
    
#ifdef TRACY_ENABLE
    // For now, emit as a message since we don't have direct GPU zone API access
    // without the full CUPTI integration
    std::ostringstream oss;
    oss << "[GPU:" << static_cast<int>(context_id) << "] " << name;
    double duration_ms = static_cast<double>(gpu_end - gpu_start) / 1000000.0;
    oss << " (" << duration_ms << " ms)";
    
    ::tracy::Profiler::LogString(::tracy::MessageSourceType::User,
                                  ::tracy::MessageSeverity::Info,
                                  color != 0 ? color : colors::KernelLaunch,
                                  TRACY_CALLSTACK, oss.str().size(), oss.str().c_str());
    
    // Also emit to kernel duration plot
    ::tracy::Profiler::PlotData("GPU Zone Duration (ms)", duration_ms);
#endif
    
    gpu_zones_emitted_.fetch_add(1);
    (void)cpu_start;
    (void)cpu_end;
}

void TracyExporter::markFrame(const char* name) {
    tracesmith::tracy::markFrame(name);
}

void TracyExporter::markFrameStart(const char* name) {
    tracesmith::tracy::markFrameStart(name);
}

void TracyExporter::markFrameEnd(const char* name) {
    tracesmith::tracy::markFrameEnd(name);
}

void TracyExporter::configurePlot(const std::string& name,
                                   PlotType type,
                                   bool step,
                                   bool fill,
                                   uint32_t color) {
    std::lock_guard<std::mutex> lock(plot_mutex_);
    
    if (configured_plots_.find(name) != configured_plots_.end()) {
        return; // Already configured
    }
    
    tracesmith::tracy::configurePlot(name.c_str(), type, step, fill, color);
    configured_plots_[name] = true;
}

void TracyExporter::emitPlotValue(const std::string& name, double value) {
#ifdef TRACY_ENABLE
    ::tracy::Profiler::PlotData(name.c_str(), value);
#endif
    (void)name;
    (void)value;
}

void TracyExporter::emitPlotValue(const std::string& name, int64_t value) {
#ifdef TRACY_ENABLE
    ::tracy::Profiler::PlotData(name.c_str(), value);
#endif
    (void)name;
    (void)value;
}

void TracyExporter::resetStats() {
    events_emitted_.store(0);
    gpu_zones_emitted_.store(0);
}

void TracyExporter::emitTraceEventInternal(const TraceEvent& event) {
#ifdef TRACY_ENABLE
    uint32_t color = getColorForEventType(event.type);
    
    // Build event description
    std::ostringstream oss;
    oss << "[" << eventTypeToString(event.type) << "] ";
    oss << event.name;
    
    if (event.duration > 0) {
        double duration_ms = static_cast<double>(event.duration) / 1000000.0;
        oss << " (" << duration_ms << " ms)";
        
        // Emit to kernel/operation duration plot
        if (event.type == EventType::KernelLaunch || 
            event.type == EventType::KernelComplete) {
            ::tracy::Profiler::PlotData("Kernel Duration (ms)", duration_ms);
        }
    }
    
    // Add metadata
    if (!event.metadata.empty()) {
        oss << " {";
        bool first = true;
        for (const auto& [key, value] : event.metadata) {
            if (!first) oss << ", ";
            oss << key << "=" << value;
            first = false;
        }
        oss << "}";
    }
    
    std::string message = oss.str();
    ::tracy::Profiler::LogString(::tracy::MessageSourceType::User, 
                                  ::tracy::MessageSeverity::Info, 
                                  color, TRACY_CALLSTACK, 
                                  message.size(), message.c_str());
    
    // Handle memory events separately
    if (event.type == EventType::MemAlloc && event.memory_params.has_value()) {
        const auto& params = event.memory_params.value();
        ::tracy::Profiler::MemAllocCallstackNamed(
            reinterpret_cast<void*>(params.dst_address),
            params.size_bytes, TRACY_CALLSTACK, false, "GPU");
    } else if (event.type == EventType::MemFree && event.memory_params.has_value()) {
        const auto& params = event.memory_params.value();
        ::tracy::Profiler::MemFreeCallstackNamed(
            reinterpret_cast<void*>(params.src_address), 
            TRACY_CALLSTACK, false, "GPU");
    }
#else
    (void)event;
#endif
}

void TracyExporter::setupDefaultPlots() {
#ifdef TRACY_ENABLE
    // Configure standard plots for GPU profiling
    configurePlot("Kernel Duration (ms)", PlotType::Number, false, true, colors::KernelLaunch);
    configurePlot("GPU Zone Duration (ms)", PlotType::Number, false, true, colors::Default);
    configurePlot("GPU Memory (MB)", PlotType::Memory, true, true, colors::MemAlloc);
    configurePlot("Memory Bandwidth (GB/s)", PlotType::Number, false, true, colors::MemcpyH2D);
    configurePlot("Active Streams", PlotType::Number, true, false, colors::StreamSync);
#endif
}

uint32_t TracyExporter::allocateQueryId() {
    return query_id_counter_.fetch_add(2);
}

// =============================================================================
// TracyGpuZoneScope Implementation
// =============================================================================

TracyGpuZoneScope::TracyGpuZoneScope(TracyExporter& exporter,
                                      uint8_t context_id,
                                      const std::string& name,
                                      uint32_t color)
    : exporter_(exporter)
    , context_id_(context_id)
    , name_(name)
    , color_(color)
    , cpu_start_(getCurrentTimestamp())
{
}

TracyGpuZoneScope::~TracyGpuZoneScope() {
    Timestamp cpu_end = getCurrentTimestamp();
    
    if (gpu_timestamps_set_) {
        exporter_.emitGpuZone(context_id_, name_, cpu_start_, cpu_end,
                               gpu_start_, gpu_end_, color_);
    } else {
        // Use CPU timestamps as approximation for GPU timestamps
        exporter_.emitGpuZone(context_id_, name_, cpu_start_, cpu_end,
                               cpu_start_, cpu_end, color_);
    }
}

void TracyGpuZoneScope::setGpuTimestamps(Timestamp gpu_start, Timestamp gpu_end) {
    gpu_start_ = gpu_start;
    gpu_end_ = gpu_end;
    gpu_timestamps_set_ = true;
}

// =============================================================================
// Global Tracy Exporter
// =============================================================================

namespace {
    std::unique_ptr<TracyExporter> g_tracy_exporter;
    TracyExporterConfig g_tracy_exporter_config;
    std::once_flag g_tracy_exporter_init;
}

TracyExporter& getGlobalTracyExporter() {
    std::call_once(g_tracy_exporter_init, []() {
        g_tracy_exporter = std::make_unique<TracyExporter>(g_tracy_exporter_config);
        g_tracy_exporter->initialize();
    });
    return *g_tracy_exporter;
}

void setGlobalTracyExporterConfig(const TracyExporterConfig& config) {
    g_tracy_exporter_config = config;
}

} // namespace tracy
} // namespace tracesmith
