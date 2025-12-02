/**
 * Metal Example - Apple GPU Profiling with TraceSmith
 * 
 * This example demonstrates:
 * - Initializing the Metal profiler
 * - Creating and executing compute shaders
 * - Capturing GPU events and timing
 * - Exporting to Perfetto trace format
 * 
 * Requirements:
 * - macOS 10.15+ with Metal support
 * - Apple Silicon or Intel Mac with Metal-capable GPU
 * - Build with -DTRACESMITH_ENABLE_METAL=ON
 * 
 * Compile:
 *   mkdir build && cd build
 *   cmake .. -DTRACESMITH_ENABLE_METAL=ON
 *   make metal_example
 * 
 * Run:
 *   ./bin/metal_example
 */

#include <iostream>
#include <iomanip>
#include <vector>

#include "tracesmith/profiler.hpp"
#include "tracesmith/sbt_format.hpp"
#include "tracesmith/perfetto_exporter.hpp"

#ifdef TRACESMITH_ENABLE_METAL
#include "tracesmith/metal_profiler.hpp"
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#endif

using namespace tracesmith;

//==============================================================================
// Metal Compute Shaders
//==============================================================================

#ifdef TRACESMITH_ENABLE_METAL

// Metal Shading Language (MSL) compute shader source
static const char* vectorAddKernel = R"(
#include <metal_stdlib>
using namespace metal;

kernel void vector_add(device const float* a [[buffer(0)]],
                       device const float* b [[buffer(1)]],
                       device float* result [[buffer(2)]],
                       uint id [[thread_position_in_grid]]) {
    result[id] = a[id] + b[id];
}
)";

static const char* matrixMulKernel = R"(
#include <metal_stdlib>
using namespace metal;

kernel void matrix_mul(device const float* A [[buffer(0)]],
                       device const float* B [[buffer(1)]],
                       device float* C [[buffer(2)]],
                       constant uint& M [[buffer(3)]],
                       constant uint& N [[buffer(4)]],
                       constant uint& K [[buffer(5)]],
                       uint2 gid [[thread_position_in_grid]]) {
    uint row = gid.y;
    uint col = gid.x;
    
    if (row < M && col < N) {
        float sum = 0.0f;
        for (uint k = 0; k < K; ++k) {
            sum += A[row * K + k] * B[k * N + col];
        }
        C[row * N + col] = sum;
    }
}
)";

static const char* reluKernel = R"(
#include <metal_stdlib>
using namespace metal;

kernel void relu(device float* data [[buffer(0)]],
                 uint id [[thread_position_in_grid]]) {
    data[id] = max(0.0f, data[id]);
}
)";

// Helper to create Metal buffer
id<MTLBuffer> createBuffer(id<MTLDevice> device, size_t size, const void* data = nullptr) {
    if (data) {
        return [device newBufferWithBytes:data 
                                   length:size 
                                  options:MTLResourceStorageModeShared];
    } else {
        return [device newBufferWithLength:size 
                                   options:MTLResourceStorageModeShared];
    }
}

// Run Metal compute workload
void runMetalWorkload(MetalProfiler& profiler) {
    @autoreleasepool {
        std::cout << "Running Metal compute workload...\n";
        
        id<MTLDevice> device = (__bridge id<MTLDevice>)profiler.getDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        
        const uint32_t N = 1024 * 1024;  // 1M elements
        const uint32_t M = 512, K = 512;
        
        // Compile compute shaders
        NSError* error = nil;
        
        // Vector addition
        NSString* vectorAddSource = [NSString stringWithUTF8String:vectorAddKernel];
        id<MTLLibrary> vectorAddLib = [device newLibraryWithSource:vectorAddSource
                                                            options:nil
                                                              error:&error];
        if (!vectorAddLib) {
            std::cerr << "Failed to compile vector add shader\n";
            return;
        }
        id<MTLFunction> vectorAddFunc = [vectorAddLib newFunctionWithName:@"vector_add"];
        id<MTLComputePipelineState> vectorAddPipeline = 
            [device newComputePipelineStateWithFunction:vectorAddFunc error:&error];
        
        // Matrix multiplication
        NSString* matrixMulSource = [NSString stringWithUTF8String:matrixMulKernel];
        id<MTLLibrary> matrixMulLib = [device newLibraryWithSource:matrixMulSource
                                                            options:nil
                                                              error:&error];
        if (!matrixMulLib) {
            std::cerr << "Failed to compile matrix mul shader\n";
            return;
        }
        id<MTLFunction> matrixMulFunc = [matrixMulLib newFunctionWithName:@"matrix_mul"];
        id<MTLComputePipelineState> matrixMulPipeline = 
            [device newComputePipelineStateWithFunction:matrixMulFunc error:&error];
        
        // ReLU
        NSString* reluSource = [NSString stringWithUTF8String:reluKernel];
        id<MTLLibrary> reluLib = [device newLibraryWithSource:reluSource
                                                       options:nil
                                                         error:&error];
        if (!reluLib) {
            std::cerr << "Failed to compile relu shader\n";
            return;
        }
        id<MTLFunction> reluFunc = [reluLib newFunctionWithName:@"relu"];
        id<MTLComputePipelineState> reluPipeline = 
            [device newComputePipelineStateWithFunction:reluFunc error:&error];
        
        // Prepare data
        std::vector<float> h_a(N), h_b(N);
        for (uint32_t i = 0; i < N; ++i) {
            h_a[i] = static_cast<float>(i);
            h_b[i] = static_cast<float>(i * 2);
        }
        
        // Create buffers
        id<MTLBuffer> bufferA = createBuffer(device, N * sizeof(float), h_a.data());
        id<MTLBuffer> bufferB = createBuffer(device, N * sizeof(float), h_b.data());
        id<MTLBuffer> bufferC = createBuffer(device, N * sizeof(float));
        
        // Matrix buffers
        id<MTLBuffer> matrixA = createBuffer(device, M * K * sizeof(float));
        id<MTLBuffer> matrixB = createBuffer(device, K * K * sizeof(float));
        id<MTLBuffer> matrixC = createBuffer(device, M * K * sizeof(float));
        
        // Matrix dimensions buffer
        uint32_t dims[3] = {M, K, K};
        id<MTLBuffer> dimsBuffer = createBuffer(device, sizeof(dims), dims);
        
        // --- Profiled operations ---
        
        // 1. Vector addition
        std::cout << "  [1/3] Vector addition\n";
        id<MTLCommandBuffer> cmdBuffer1 = [queue commandBuffer];
        cmdBuffer1.label = @"VectorAdd";
        
        id<MTLComputeCommandEncoder> encoder1 = [cmdBuffer1 computeCommandEncoder];
        [encoder1 setComputePipelineState:vectorAddPipeline];
        [encoder1 setBuffer:bufferA offset:0 atIndex:0];
        [encoder1 setBuffer:bufferB offset:0 atIndex:1];
        [encoder1 setBuffer:bufferC offset:0 atIndex:2];
        
        MTLSize gridSize1 = MTLSizeMake(N, 1, 1);
        NSUInteger threadGroupSize = vectorAddPipeline.maxTotalThreadsPerThreadgroup;
        if (threadGroupSize > N) threadGroupSize = N;
        MTLSize threadgroupSize1 = MTLSizeMake(threadGroupSize, 1, 1);
        
        [encoder1 dispatchThreads:gridSize1 threadsPerThreadgroup:threadgroupSize1];
        [encoder1 endEncoding];
        
        profiler.trackCommandBuffer((__bridge void*)cmdBuffer1);
        [cmdBuffer1 commit];
        
        // 2. Matrix multiplication
        std::cout << "  [2/3] Matrix multiplication\n";
        id<MTLCommandBuffer> cmdBuffer2 = [queue commandBuffer];
        cmdBuffer2.label = @"MatrixMul";
        
        id<MTLComputeCommandEncoder> encoder2 = [cmdBuffer2 computeCommandEncoder];
        [encoder2 setComputePipelineState:matrixMulPipeline];
        [encoder2 setBuffer:matrixA offset:0 atIndex:0];
        [encoder2 setBuffer:matrixB offset:0 atIndex:1];
        [encoder2 setBuffer:matrixC offset:0 atIndex:2];
        [encoder2 setBytes:&M length:sizeof(M) atIndex:3];
        [encoder2 setBytes:&K length:sizeof(K) atIndex:4];
        [encoder2 setBytes:&K length:sizeof(K) atIndex:5];
        
        MTLSize gridSize2 = MTLSizeMake(K, M, 1);
        MTLSize threadgroupSize2 = MTLSizeMake(16, 16, 1);
        
        [encoder2 dispatchThreads:gridSize2 threadsPerThreadgroup:threadgroupSize2];
        [encoder2 endEncoding];
        
        profiler.trackCommandBuffer((__bridge void*)cmdBuffer2);
        [cmdBuffer2 commit];
        
        // 3. ReLU activation
        std::cout << "  [3/3] ReLU activation\n";
        id<MTLCommandBuffer> cmdBuffer3 = [queue commandBuffer];
        cmdBuffer3.label = @"ReLU";
        
        id<MTLComputeCommandEncoder> encoder3 = [cmdBuffer3 computeCommandEncoder];
        [encoder3 setComputePipelineState:reluPipeline];
        [encoder3 setBuffer:bufferC offset:0 atIndex:0];
        
        MTLSize gridSize3 = MTLSizeMake(N, 1, 1);
        MTLSize threadgroupSize3 = MTLSizeMake(threadGroupSize, 1, 1);
        
        [encoder3 dispatchThreads:gridSize3 threadsPerThreadgroup:threadgroupSize3];
        [encoder3 endEncoding];
        
        profiler.trackCommandBuffer((__bridge void*)cmdBuffer3);
        [cmdBuffer3 commit];
        
        // Wait for completion
        [cmdBuffer1 waitUntilCompleted];
        [cmdBuffer2 waitUntilCompleted];
        [cmdBuffer3 waitUntilCompleted];
        
        std::cout << "Metal workload completed.\n\n";
    }
}

#endif // TRACESMITH_ENABLE_METAL

//==============================================================================
// Main Program
//==============================================================================

void printDeviceInfo(const std::vector<DeviceInfo>& devices) {
    std::cout << "\n=== Metal Devices ===\n";
    for (const auto& dev : devices) {
        std::cout << "Device " << dev.device_id << ": " << dev.name << "\n";
        std::cout << "  Vendor: " << dev.vendor << "\n";
        std::cout << "  Memory: " << (dev.total_memory / (1024 * 1024 * 1024)) << " GB\n";
    }
    std::cout << "\n";
}

void printEventSummary(const std::vector<TraceEvent>& events) {
    std::cout << "\n=== Event Summary ===\n";
    std::cout << "Total events captured: " << events.size() << "\n\n";
    
    for (const auto& event : events) {
        std::cout << "Event: " << event.name << "\n";
        std::cout << "  Correlation ID: " << event.correlation_id << "\n";
        
        if (event.duration > 0) {
            double ms = event.duration / 1e6;
            std::cout << "  Duration: " << std::fixed << std::setprecision(3) << ms << " ms\n";
        }
        
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=======================================================\n";
    std::cout << "  TraceSmith Metal Profiling Example\n";
    std::cout << "=======================================================\n\n";
    
#ifndef TRACESMITH_ENABLE_METAL
    std::cerr << "ERROR: TraceSmith was compiled without Metal support.\n";
    std::cerr << "Please rebuild with -DTRACESMITH_ENABLE_METAL=ON\n";
    return 1;
#else
    // Check Metal availability
    if (!isMetalAvailable()) {
        std::cerr << "ERROR: No Metal-capable GPU found.\n";
        std::cerr << "Metal requires macOS 10.15+ or iOS 13+.\n";
        return 1;
    }
    
    std::cout << "Metal Version: " << getMetalVersion() << "\n";
    std::cout << "Metal Device Count: " << getMetalDeviceCount() << "\n";
    std::cout << "Supports GPU Capture: " << (supportsMetalCapture() ? "Yes" : "No") << "\n";
    
    // Create Metal profiler
    MetalProfiler profiler;
    
    ProfilerConfig config;
    config.buffer_size = 10000;
    
    if (!profiler.initialize(config)) {
        std::cerr << "ERROR: Failed to initialize Metal profiler.\n";
        return 1;
    }
    
    std::cout << "Feature Set: " << profiler.getFeatureSet() << "\n";
    
    printDeviceInfo(profiler.getDeviceInfo());
    
    // Start profiling
    std::cout << "Starting profiling...\n";
    if (!profiler.startCapture()) {
        std::cerr << "ERROR: Failed to start capture.\n";
        return 1;
    }
    
    // Run Metal workload
    runMetalWorkload(profiler);
    
    // Give GPU time to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Stop profiling
    profiler.stopCapture();
    std::cout << "Profiling stopped.\n";
    
    // Get captured events
    std::vector<TraceEvent> events;
    profiler.getEvents(events);
    
    std::cout << "\nStatistics:\n";
    std::cout << "  Events captured: " << profiler.eventsCaptured() << "\n";
    std::cout << "  Events dropped: " << profiler.eventsDropped() << "\n";
    
    // Print event summary
    printEventSummary(events);
    
    // Export to SBT format
    std::string sbt_file = "metal_trace.sbt";
    std::cout << "\nSaving to " << sbt_file << "...\n";
    
    SBTWriter writer(sbt_file);
    for (const auto& event : events) {
        writer.writeEvent(event);
    }
    std::cout << "Saved " << events.size() << " events to " << sbt_file << "\n";
    
    // Export to Perfetto format
    std::string perfetto_file = "metal_trace.json";
    std::cout << "\nExporting to Perfetto format: " << perfetto_file << "...\n";
    
    PerfettoExporter exporter;
    if (exporter.exportToFile(events, perfetto_file)) {
        std::cout << "Exported to " << perfetto_file << "\n";
        std::cout << "Open in https://ui.perfetto.dev/ to visualize.\n";
    }
    
    // Cleanup
    profiler.finalize();
    
    std::cout << "\n=======================================================\n";
    std::cout << "  Example completed successfully!\n";
    std::cout << "=======================================================\n";
    
    return 0;
#endif
}
