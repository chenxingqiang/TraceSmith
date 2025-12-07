/**
 * TraceSmith Profiler - Real GPU Profiling Only
 * 
 * Platform detection and factory functions for GPU profilers:
 * - CUDA (NVIDIA via CUPTI)
 * - ROCm (AMD)
 * - Metal (Apple)
 * - MACA (MetaX via MCPTI)
 */

#include "tracesmith/capture/profiler.hpp"
#ifdef TRACESMITH_ENABLE_CUDA
#include "tracesmith/capture/cupti_profiler.hpp"
#endif
#ifdef TRACESMITH_ENABLE_METAL
#include "tracesmith/capture/metal_profiler.hpp"
#endif
#ifdef TRACESMITH_ENABLE_MACA
#include "tracesmith/capture/mcpti_profiler.hpp"
#endif

namespace tracesmith {

// ============================================================================
// Platform Detection Functions (always available for Python bindings)
// ============================================================================

#ifndef TRACESMITH_ENABLE_CUDA
// Stub implementations when CUDA is not enabled
bool isCUDAAvailable() { return false; }
int getCUDADriverVersion() { return 0; }
int getCUDADeviceCount() { return 0; }
#endif

#ifndef TRACESMITH_ENABLE_METAL
// Stub implementations when Metal is not enabled
bool isMetalAvailable() { return false; }
int getMetalDeviceCount() { return 0; }
#endif

#ifndef TRACESMITH_ENABLE_MACA
// Stub implementations when MACA is not enabled
bool isMACAAvailable() { return false; }
int getMACADriverVersion() { return 0; }
int getMACADeviceCount() { return 0; }
#endif

// ============================================================================
// Factory Functions - Real GPU Profilers Only
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
        
        case PlatformType::MACA:
#ifdef TRACESMITH_ENABLE_MACA
            {
                auto profiler = std::make_unique<MCPTIProfiler>();
                if (profiler->isAvailable()) {
                    return profiler;
                }
            }
#endif
            return nullptr;  // MACA not available
        
        default:
            return nullptr;  // No supported GPU platform
    }
}

PlatformType detectPlatform() {
#ifdef TRACESMITH_ENABLE_CUDA
    if (isCUDAAvailable()) {
        return PlatformType::CUDA;
    }
#endif
    
#ifdef TRACESMITH_ENABLE_MACA
    if (isMACAAvailable()) {
        return PlatformType::MACA;
    }
#endif
    
#ifdef TRACESMITH_ENABLE_METAL
    if (isMetalAvailable()) {
        return PlatformType::Metal;
    }
#endif
    
    // TODO: Check for ROCm
    // if (isROCmAvailable()) return PlatformType::ROCm;
    
    return PlatformType::Unknown;
}

} // namespace tracesmith
