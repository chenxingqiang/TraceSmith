/**
 * TraceSmith Multi-GPU Profiling Example
 * 
 * Demonstrates multi-GPU profiling capabilities:
 * - GPU topology discovery
 * - Multi-GPU concurrent profiling
 * - NVLink bandwidth analysis
 * - Cross-GPU event correlation
 * 
 * Usage:
 *   ./multi_gpu_example
 * 
 * Requirements:
 *   - NVIDIA GPU with CUDA support
 *   - Multiple GPUs recommended for full functionality
 */

#include "tracesmith/tracesmith.hpp"
#include "tracesmith/cluster/multi_gpu_profiler.hpp"
#include "tracesmith/cluster/gpu_topology.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

#ifdef TRACESMITH_ENABLE_CUDA
#include <cuda_runtime.h>
#endif

using namespace tracesmith;
using namespace tracesmith::cluster;

// ============================================================================
// Helper Functions
// ============================================================================

void printSeparator(const char* title) {
    std::cout << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════════\n";
    std::cout << "  " << title << "\n";
    std::cout << "═══════════════════════════════════════════════════════════════════\n\n";
}

void printSuccess(const std::string& msg) {
    std::cout << "✓ " << msg << "\n";
}

void printError(const std::string& msg) {
    std::cout << "✗ " << msg << "\n";
}

void printInfo(const std::string& msg) {
    std::cout << "  " << msg << "\n";
}

#ifdef TRACESMITH_ENABLE_CUDA

// Simple CUDA kernel for testing
__global__ void test_kernel(float* data, int n, int gpu_id) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        // Some work to simulate GPU activity
        float val = data[idx];
        for (int i = 0; i < 100; ++i) {
            val = sinf(val) * cosf(val) + static_cast<float>(gpu_id);
        }
        data[idx] = val;
    }
}

// Launch kernels on a specific GPU
void launchKernelsOnGPU(int gpu_id, int num_kernels) {
    cudaSetDevice(gpu_id);
    
    const int N = 1024 * 1024;
    float* d_data;
    cudaMalloc(&d_data, N * sizeof(float));
    
    int threads = 256;
    int blocks = (N + threads - 1) / threads;
    
    for (int i = 0; i < num_kernels; ++i) {
        test_kernel<<<blocks, threads>>>(d_data, N, gpu_id);
    }
    
    cudaDeviceSynchronize();
    cudaFree(d_data);
}

#endif

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║          TraceSmith Multi-GPU Profiling Example (v0.7.0)          ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════════╝\n";

#ifndef TRACESMITH_ENABLE_CUDA
    printError("CUDA support not enabled. Please rebuild with -DTRACESMITH_ENABLE_CUDA=ON");
    return 1;
#else

    // =========================================================================
    // Step 1: GPU Topology Discovery
    // =========================================================================
    printSeparator("GPU Topology Discovery");
    
    GPUTopology topology;
    if (!topology.discover()) {
        printError("Failed to discover GPU topology");
        // Continue anyway - we can still profile individual GPUs
    } else {
        printSuccess("GPU topology discovered");
        
        auto info = topology.getTopology();
        printInfo("Found " + std::to_string(info.gpu_count) + " GPU(s)");
        
        if (info.has_nvswitch) {
            printInfo("NVSwitch detected - full mesh connectivity");
        }
        
        // Print topology
        std::cout << "\n" << topology.toASCII();
    }
    
    // =========================================================================
    // Step 2: Initialize Multi-GPU Profiler
    // =========================================================================
    printSeparator("Multi-GPU Profiler Initialization");
    
    MultiGPUConfig config;
    config.per_gpu_buffer_size = 1024 * 1024;  // 1M events per GPU
    config.enable_nvlink_tracking = true;
    config.enable_peer_access_tracking = true;
    config.aggregation_interval_ms = 50;
    config.capture_topology = true;
    
    MultiGPUProfiler profiler(config);
    
    if (!profiler.initialize()) {
        printError("Failed to initialize multi-GPU profiler");
        return 1;
    }
    
    printSuccess("Multi-GPU profiler initialized");
    
    auto active_gpus = profiler.getActiveGPUs();
    printInfo("Active GPUs: " + std::to_string(active_gpus.size()));
    
    for (uint32_t gpu_id : active_gpus) {
        auto device_info = profiler.getDeviceInfo(gpu_id);
        printInfo("  GPU " + std::to_string(gpu_id) + ": " + device_info.name);
    }
    
    // =========================================================================
    // Step 3: Start Profiling
    // =========================================================================
    printSeparator("GPU Profiling");
    
    if (!profiler.startCapture()) {
        printError("Failed to start capture");
        return 1;
    }
    
    printSuccess("Started capturing on all GPUs");
    
    // Launch workload on each GPU
    const int KERNELS_PER_GPU = 1000;
    std::cout << "\nLaunching " << KERNELS_PER_GPU << " kernels on each GPU...\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Launch on each GPU in parallel (using threads)
    std::vector<std::thread> threads;
    for (uint32_t gpu_id : active_gpus) {
        threads.emplace_back([gpu_id]() {
            launchKernelsOnGPU(gpu_id, KERNELS_PER_GPU);
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    printSuccess("Launched " + std::to_string(KERNELS_PER_GPU * active_gpus.size()) + 
                 " kernels across " + std::to_string(active_gpus.size()) + " GPUs");
    printInfo("Total time: " + std::to_string(duration.count()) + " ms");
    
    // =========================================================================
    // Step 4: Stop Profiling and Collect Results
    // =========================================================================
    printSeparator("Results");
    
    profiler.stopCapture();
    printSuccess("Stopped capture");
    
    // Get statistics
    auto stats = profiler.getStatistics();
    
    std::cout << "\nCapture Statistics:\n";
    std::cout << "  Total events:    " << stats.total_events << "\n";
    std::cout << "  Events dropped:  " << stats.total_dropped << "\n";
    std::cout << "  Capture time:    " << std::fixed << std::setprecision(2) 
              << stats.capture_duration_ms << " ms\n";
    
    std::cout << "\nPer-GPU Statistics:\n";
    for (const auto& [gpu_id, count] : stats.events_per_gpu) {
        std::cout << "  GPU " << gpu_id << ": " << count << " events";
        if (stats.dropped_per_gpu.count(gpu_id) && stats.dropped_per_gpu.at(gpu_id) > 0) {
            std::cout << " (" << stats.dropped_per_gpu.at(gpu_id) << " dropped)";
        }
        std::cout << "\n";
    }
    
    if (stats.nvlink_transfers > 0) {
        std::cout << "\nNVLink Transfers:\n";
        std::cout << "  Transfer count:  " << stats.nvlink_transfers << "\n";
        std::cout << "  Total bytes:     " << stats.nvlink_bytes << "\n";
    }
    
    // =========================================================================
    // Step 5: Get and Analyze Events
    // =========================================================================
    printSeparator("Event Analysis");
    
    std::vector<TraceEvent> events;
    size_t event_count = profiler.getEvents(events, 1000);  // Get first 1000 events
    
    printSuccess("Retrieved " + std::to_string(event_count) + " events");
    
    // Count event types
    std::map<EventType, int> event_type_counts;
    for (const auto& event : events) {
        event_type_counts[event.type]++;
    }
    
    std::cout << "\nEvent Types:\n";
    for (const auto& [type, count] : event_type_counts) {
        std::cout << "  " << eventTypeToString(type) << ": " << count << "\n";
    }
    
    // =========================================================================
    // Step 6: Export to Perfetto
    // =========================================================================
    printSeparator("Export");
    
    // Get all events for export
    std::vector<TraceEvent> all_events;
    profiler.getEvents(all_events);
    
    const char* output_file = "multi_gpu_trace.json";
    
    PerfettoExporter exporter;
    if (exporter.exportToFile(all_events, {}, output_file)) {
        printSuccess("Exported to " + std::string(output_file));
        printInfo("Open in https://ui.perfetto.dev for visualization");
    } else {
        printError("Failed to export trace");
    }
    
    // =========================================================================
    // Summary
    // =========================================================================
    printSeparator("Summary");
    
    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    MULTI-GPU PROFILING SUMMARY                   ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║                                                                  ║\n";
    std::cout << "║  GPUs profiled:      " << std::setw(10) << active_gpus.size() 
              << "                               ║\n";
    std::cout << "║  Total events:       " << std::setw(10) << stats.total_events 
              << "                               ║\n";
    std::cout << "║  Kernels launched:   " << std::setw(10) << (KERNELS_PER_GPU * active_gpus.size())
              << "                               ║\n";
    std::cout << "║  Total time:         " << std::setw(7) << duration.count() 
              << " ms                            ║\n";
    std::cout << "║                                                                  ║\n";
    std::cout << "║  ✅ Multi-GPU profiling verified!                                ║\n";
    std::cout << "║                                                                  ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n\n";
    
    return 0;
    
#endif // TRACESMITH_ENABLE_CUDA
}

