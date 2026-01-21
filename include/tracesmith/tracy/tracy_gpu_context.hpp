#pragma once

/**
 * Tracy GPU Context for TraceSmith
 * 
 * Provides full GPU timeline support in Tracy for ALL GPU platforms,
 * including Ascend, MetaX, and ROCm that Tracy doesn't natively support.
 * 
 * This uses Tracy's internal GPU zone API to create proper GPU timelines
 * instead of message-based visualization.
 * 
 * Features:
 * - Full GPU timeline visualization (not message-based)
 * - GPU-CPU timestamp correlation
 * - Multiple GPU context support
 * - Kernel duration bars in Tracy
 * - Memory operation visualization
 */

#include "tracesmith/common/types.hpp"
#include "tracesmith/capture/profiler.hpp"  // For PlatformType

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/../client/TracyProfiler.hpp>
#include <tracy/../common/TracyQueue.hpp>
#endif

#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdint>

namespace tracesmith {
namespace tracy {

// Forward declaration to avoid namespace issues
using tracesmith::PlatformType;

/**
 * GPU Context Type for identification in Tracy
 */
enum class GpuContextType : uint8_t {
    Invalid = 0,
    CUDA = 1,      // Native Tracy CUDA
    Vulkan = 2,    // Native Tracy Vulkan  
    OpenGL = 3,    // Native Tracy OpenGL
    Direct3D11 = 4,
    Direct3D12 = 5,
    OpenCL = 6,
    Metal = 7,     // Native Tracy Metal
    // TraceSmith extended types
    Ascend = 100,  // Huawei Ascend NPU
    MACA = 101,    // MetaX MACA GPU
    ROCm = 102,    // AMD ROCm
    Generic = 255  // Generic GPU
};

/**
 * Convert PlatformType to GpuContextType
 */
inline GpuContextType platformToGpuContextType(PlatformType platform) {
    switch (platform) {
        case PlatformType::CUDA:   return GpuContextType::CUDA;
        case PlatformType::Metal:  return GpuContextType::Metal;
        case PlatformType::MACA:   return GpuContextType::MACA;
        case PlatformType::Ascend: return GpuContextType::Ascend;
        case PlatformType::ROCm:   return GpuContextType::ROCm;
        default:                   return GpuContextType::Generic;
    }
}

/**
 * TracyGpuContext - Full GPU timeline support for any GPU platform
 * 
 * This class creates a proper Tracy GPU context that shows up as a
 * dedicated GPU timeline in Tracy's profiler view, just like native
 * CUDA or Vulkan contexts.
 */
class TracyGpuContext {
public:
    /**
     * Create a new GPU context for Tracy
     * @param name Display name in Tracy (e.g., "Ascend 910B", "MetaX C500")
     * @param type GPU context type
     * @param device_id Device ID for multi-GPU systems
     */
    TracyGpuContext(const std::string& name, 
                    GpuContextType type = GpuContextType::Generic,
                    uint32_t device_id = 0);
    
    ~TracyGpuContext();
    
    // Disable copy
    TracyGpuContext(const TracyGpuContext&) = delete;
    TracyGpuContext& operator=(const TracyGpuContext&) = delete;
    
    /**
     * Get the Tracy context ID
     */
    uint8_t contextId() const { return context_id_; }
    
    /**
     * Get the context name
     */
    const std::string& name() const { return name_; }
    
    /**
     * Check if context is valid
     */
    bool isValid() const { return context_id_ != 255; }
    
    // =========================================================================
    // GPU Zone Emission - Creates proper timeline bars in Tracy
    // =========================================================================
    
    /**
     * Emit a GPU zone with full timeline support
     * This creates a visual bar in Tracy's GPU timeline
     * 
     * @param zone_name Name of the zone (kernel name, etc.)
     * @param cpu_start CPU timestamp when operation was submitted
     * @param cpu_end CPU timestamp when submission completed
     * @param gpu_start GPU timestamp when execution started
     * @param gpu_end GPU timestamp when execution completed
     * @param thread_id Thread that submitted the operation
     * @param color Optional color (0 for default)
     */
    void emitGpuZone(const char* zone_name,
                     int64_t cpu_start,
                     int64_t cpu_end,
                     int64_t gpu_start,
                     int64_t gpu_end,
                     uint32_t thread_id = 0,
                     uint32_t color = 0);
    
    /**
     * Emit a GPU zone from TraceSmith event
     */
    void emitGpuZone(const TraceEvent& event);
    
    /**
     * Emit multiple GPU zones from TraceSmith events
     */
    void emitGpuZones(const std::vector<TraceEvent>& events);
    
    // =========================================================================
    // Timestamp Calibration
    // =========================================================================
    
    /**
     * Calibrate GPU-CPU timestamps
     * Call this periodically for accurate time correlation
     * 
     * @param cpu_time Current CPU timestamp
     * @param gpu_time Corresponding GPU timestamp
     */
    void calibrate(int64_t cpu_time, int64_t gpu_time);
    
    /**
     * Set the GPU clock period (nanoseconds per tick)
     * Default is 1.0 (1 tick = 1 nanosecond)
     */
    void setClockPeriod(float period) { clock_period_ = period; }
    
    // =========================================================================
    // Statistics
    // =========================================================================
    
    uint64_t zonesEmitted() const { return zones_emitted_.load(); }
    
private:
    // Allocate a query ID pair for GPU zone
    uint16_t allocateQueryId();
    
    // Internal zone emission
    void emitZoneInternal(const char* zone_name, size_t name_len,
                          int64_t cpu_start, int64_t cpu_end,
                          int64_t gpu_start, int64_t gpu_end,
                          uint32_t thread_id, uint32_t color);
    
    std::string name_;
    GpuContextType type_;
    uint32_t device_id_;
    uint8_t context_id_ = 255;
    float clock_period_ = 1.0f;
    
    std::atomic<uint16_t> query_counter_{0};
    std::atomic<uint64_t> zones_emitted_{0};
    
    int64_t last_calibration_cpu_ = 0;
    int64_t last_calibration_gpu_ = 0;
    
    // Source location for zones (persistent memory)
    struct SourceLocationCache {
        std::unordered_map<std::string, ::tracy::SourceLocationData*> locations;
        std::mutex mutex;
        
        ::tracy::SourceLocationData* getOrCreate(const std::string& name, uint32_t color);
        ~SourceLocationCache();
    };
    static SourceLocationCache& getSourceLocationCache();
};

/**
 * TracyGpuZoneEmitter - RAII helper for GPU zone emission
 * 
 * Usage:
 *   TracyGpuContext ctx("My GPU", GpuContextType::Ascend);
 *   {
 *       TracyGpuZoneEmitter zone(ctx, "kernel_name");
 *       // ... kernel execution ...
 *       zone.setGpuTimestamps(gpu_start, gpu_end);
 *   } // Zone emitted on destruction
 */
class TracyGpuZoneEmitter {
public:
    TracyGpuZoneEmitter(TracyGpuContext& context, const char* name, uint32_t color = 0);
    ~TracyGpuZoneEmitter();
    
    // Disable copy
    TracyGpuZoneEmitter(const TracyGpuZoneEmitter&) = delete;
    TracyGpuZoneEmitter& operator=(const TracyGpuZoneEmitter&) = delete;
    
    /**
     * Set GPU timestamps for accurate timing
     * Must be called before destruction
     */
    void setGpuTimestamps(int64_t gpu_start, int64_t gpu_end);
    
private:
    TracyGpuContext& context_;
    const char* name_;
    uint32_t color_;
    int64_t cpu_start_;
    int64_t gpu_start_ = 0;
    int64_t gpu_end_ = 0;
    bool gpu_timestamps_set_ = false;
};

// =============================================================================
// Global GPU Context Management
// =============================================================================

/**
 * Create or get a GPU context for a device
 * Thread-safe, contexts are cached by device_id and type
 */
TracyGpuContext& getOrCreateGpuContext(const std::string& name,
                                        GpuContextType type,
                                        uint32_t device_id = 0);

/**
 * Create GPU context from TraceSmith platform type
 */
TracyGpuContext& getOrCreateGpuContext(PlatformType platform,
                                        uint32_t device_id = 0);

/**
 * Destroy all cached GPU contexts
 */
void destroyAllGpuContexts();

} // namespace tracy
} // namespace tracesmith

// =============================================================================
// Convenience Macros
// =============================================================================

#ifdef TRACY_ENABLE

/**
 * Create a GPU zone in the specified context
 * Usage: TracySmithGpuZone(ctx, "kernel_name")
 */
#define TracySmithGpuZone(ctx, name) \
    tracesmith::tracy::TracyGpuZoneEmitter TRACESMITH_CONCAT(__ts_gpu_zone_, __LINE__)(ctx, name)

/**
 * Create a GPU zone with color
 */
#define TracySmithGpuZoneC(ctx, name, color) \
    tracesmith::tracy::TracyGpuZoneEmitter TRACESMITH_CONCAT(__ts_gpu_zone_, __LINE__)(ctx, name, color)

#else

#define TracySmithGpuZone(ctx, name)
#define TracySmithGpuZoneC(ctx, name, color)

#endif
