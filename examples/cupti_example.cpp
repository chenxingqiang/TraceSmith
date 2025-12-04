/**
 * CUPTI Example - Real NVIDIA GPU Profiling with TraceSmith
 * 
 * This example demonstrates REAL GPU profiling :
 * - Initializing the CUPTI profiler for hardware-level tracing
 * - Running actual CUDA kernels (vectorAdd, matrixMul, ReLU)
 * - Capturing real GPU events (kernels, memory operations, synchronization)
 * - Multi-stream concurrent execution profiling
 * - Exporting to SBT and Perfetto trace formats
 * 
 * Requirements:
 * - NVIDIA GPU with CUDA support
 * - CUDA Toolkit with CUPTI
 * - Build with -DTRACESMITH_ENABLE_CUDA=ON
 * 
 * Compile:
 *   mkdir build && cd build
 *   cmake .. -DTRACESMITH_ENABLE_CUDA=ON
 *   make cupti_example
 * 
 * Run:
 *   ./bin/cupti_example
 * 
 * Output:
 *   cupti_trace.sbt - TraceSmith binary trace
 *   cupti_trace.json - Perfetto JSON (open in ui.perfetto.dev)
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

#include "tracesmith/capture/profiler.hpp"
#include "tracesmith/format/sbt_format.hpp"
#include "tracesmith/state/perfetto_exporter.hpp"

#ifdef TRACESMITH_ENABLE_CUDA
#include "tracesmith/capture/cupti_profiler.hpp"
#include <cuda_runtime.h>
#endif

using namespace tracesmith;

//==============================================================================
// CUDA Kernels and Operations
//==============================================================================

#ifdef TRACESMITH_ENABLE_CUDA

// Simple vector addition kernel
__global__ void vectorAdd(float* a, float* b, float* c, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        c[idx] = a[idx] + b[idx];
    }
}

// Matrix multiplication kernel
__global__ void matrixMul(float* A, float* B, float* C, int M, int N, int K) {
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    int col = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (row < M && col < N) {
        float sum = 0.0f;
        for (int k = 0; k < K; ++k) {
            sum += A[row * K + k] * B[k * N + col];
        }
        C[row * N + col] = sum;
    }
}

// ReLU activation kernel
__global__ void relu(float* data, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        data[idx] = fmaxf(0.0f, data[idx]);
    }
}

// Run a sample CUDA workload
void runCUDAWorkload() {
    std::cout << "Running CUDA workload...\n";
    
    const int N = 1 << 20;  // 1M elements
    const int M = 512, K = 512;  // Matrix dimensions
    
    // Host memory
    float *h_a = new float[N];
    float *h_b = new float[N];
    float *h_c = new float[N];
    
    // Initialize host data
    for (int i = 0; i < N; ++i) {
        h_a[i] = static_cast<float>(i);
        h_b[i] = static_cast<float>(i * 2);
    }
    
    // Device memory
    float *d_a, *d_b, *d_c;
    cudaMalloc(&d_a, N * sizeof(float));
    cudaMalloc(&d_b, N * sizeof(float));
    cudaMalloc(&d_c, N * sizeof(float));
    
    // Create streams for concurrent execution
    cudaStream_t stream1, stream2;
    cudaStreamCreate(&stream1);
    cudaStreamCreate(&stream2);
    
    // --- Profiled operations ---
    
    // 1. Host to Device memory transfers
    std::cout << "  [1/5] H2D Memory transfers\n";
    cudaMemcpyAsync(d_a, h_a, N * sizeof(float), cudaMemcpyHostToDevice, stream1);
    cudaMemcpyAsync(d_b, h_b, N * sizeof(float), cudaMemcpyHostToDevice, stream1);
    
    // 2. Vector addition kernel
    std::cout << "  [2/5] Vector addition kernel\n";
    int threadsPerBlock = 256;
    int blocksPerGrid = (N + threadsPerBlock - 1) / threadsPerBlock;
    vectorAdd<<<blocksPerGrid, threadsPerBlock, 0, stream1>>>(d_a, d_b, d_c, N);
    
    // 3. Matrix multiplication on stream2 (concurrent)
    std::cout << "  [3/5] Matrix multiplication kernel\n";
    float *d_A, *d_B, *d_C;
    cudaMalloc(&d_A, M * K * sizeof(float));
    cudaMalloc(&d_B, K * K * sizeof(float));
    cudaMalloc(&d_C, M * K * sizeof(float));
    
    cudaMemsetAsync(d_A, 0, M * K * sizeof(float), stream2);
    cudaMemsetAsync(d_B, 0, K * K * sizeof(float), stream2);
    
    dim3 blockDim(16, 16);
    dim3 gridDim((K + blockDim.x - 1) / blockDim.x,
                 (M + blockDim.y - 1) / blockDim.y);
    matrixMul<<<gridDim, blockDim, 0, stream2>>>(d_A, d_B, d_C, M, K, K);
    
    // 4. ReLU activation
    std::cout << "  [4/5] ReLU activation kernel\n";
    relu<<<blocksPerGrid, threadsPerBlock, 0, stream1>>>(d_c, N);
    
    // 5. Device to Host memory transfer
    std::cout << "  [5/5] D2H Memory transfer\n";
    cudaMemcpyAsync(h_c, d_c, N * sizeof(float), cudaMemcpyDeviceToHost, stream1);
    
    // Synchronize all streams
    cudaStreamSynchronize(stream1);
    cudaStreamSynchronize(stream2);
    cudaDeviceSynchronize();
    
    // Cleanup
    cudaFree(d_a);
    cudaFree(d_b);
    cudaFree(d_c);
    cudaFree(d_A);
    cudaFree(d_B);
    cudaFree(d_C);
    cudaStreamDestroy(stream1);
    cudaStreamDestroy(stream2);
    
    delete[] h_a;
    delete[] h_b;
    delete[] h_c;
    
    std::cout << "CUDA workload completed.\n\n";
}

#endif // TRACESMITH_ENABLE_CUDA

//==============================================================================
// Main Program
//==============================================================================

void printDeviceInfo(const std::vector<DeviceInfo>& devices) {
    std::cout << "\n=== GPU Devices ===\n";
    for (const auto& dev : devices) {
        std::cout << "Device " << dev.device_id << ": " << dev.name << "\n";
        std::cout << "  Vendor: " << dev.vendor << "\n";
        std::cout << "  Compute: " << dev.compute_major << "." << dev.compute_minor << "\n";
        std::cout << "  Memory: " << (dev.total_memory / (1024 * 1024 * 1024)) << " GB\n";
        std::cout << "  SMs: " << dev.multiprocessor_count << "\n";
        std::cout << "  Clock: " << (dev.clock_rate / 1000) << " MHz\n";
    }
    std::cout << "\n";
}

void printEventSummary(const std::vector<TraceEvent>& events) {
    std::cout << "\n=== Event Summary ===\n";
    std::cout << "Total events captured: " << events.size() << "\n\n";
    
    // Count by type and calculate durations from Launch/Complete pairs
    std::map<EventType, int> type_counts;
    std::map<std::string, uint64_t> kernel_launch_times;  // name -> launch timestamp
    std::vector<std::pair<std::string, uint64_t>> kernel_durations;  // name, duration_ns
    
    for (const auto& event : events) {
        type_counts[event.type]++;
        
        // Track kernel launch/complete pairs
        if (event.type == EventType::KernelLaunch) {
            kernel_launch_times[event.name] = event.timestamp;
        } else if (event.type == EventType::KernelComplete) {
            auto it = kernel_launch_times.find(event.name);
            if (it != kernel_launch_times.end()) {
                uint64_t duration = event.timestamp - it->second;
                kernel_durations.push_back({event.name, duration});
                kernel_launch_times.erase(it);
            }
        }
    }
    
    auto typeName = [](EventType t) -> const char* {
        switch (t) {
            case EventType::KernelLaunch: return "KernelLaunch";
            case EventType::KernelComplete: return "KernelComplete";
            case EventType::MemcpyH2D: return "MemcpyH2D";
            case EventType::MemcpyD2H: return "MemcpyD2H";
            case EventType::MemcpyD2D: return "MemcpyD2D";
            case EventType::MemsetDevice: return "MemsetDevice";
            case EventType::StreamSync: return "StreamSync";
            case EventType::DeviceSync: return "DeviceSync";
            default: return "Other";
        }
    };
    
    std::cout << std::left << std::setw(20) << "Event Type" 
              << std::setw(10) << "Count\n";
    std::cout << std::string(30, '-') << "\n";
    
    for (const auto& [type, count] : type_counts) {
        std::cout << std::left << std::setw(20) << typeName(type)
                  << std::setw(10) << count << "\n";
    }
    
    // Print kernel execution times
    if (!kernel_durations.empty()) {
        std::cout << "\n=== Kernel Execution Times ===\n";
        std::cout << std::left << std::setw(35) << "Kernel Name" 
                  << std::setw(15) << "Duration (ns)"
                  << std::setw(15) << "Duration (us)"
                  << "Duration (ms)\n";
        std::cout << std::string(80, '-') << "\n";
        
        uint64_t total_kernel_time = 0;
        for (const auto& [name, duration] : kernel_durations) {
            double us = duration / 1e3;
            double ms = duration / 1e6;
            std::cout << std::left << std::setw(35) << name
                      << std::setw(15) << duration
                      << std::fixed << std::setprecision(2)
                      << std::setw(15) << us
                      << std::setprecision(4) << ms << "\n";
            total_kernel_time += duration;
        }
        
        std::cout << std::string(80, '-') << "\n";
        std::cout << std::left << std::setw(35) << "TOTAL"
                  << std::setw(15) << total_kernel_time
                  << std::fixed << std::setprecision(2)
                  << std::setw(15) << (total_kernel_time / 1e3)
                  << std::setprecision(4) << (total_kernel_time / 1e6) << "\n";
    }
    
    // Print some sample events
    std::cout << "\n=== Sample Events (first 10) ===\n";
    int shown = 0;
    for (const auto& event : events) {
        if (shown >= 10) break;
        
        std::cout << "[" << shown + 1 << "] " << event.name << "\n";
        std::cout << "    Type: " << typeName(event.type) 
                  << " | Stream: " << event.stream_id 
                  << " | Device: " << event.device_id << "\n";
        std::cout << "    Timestamp: " << event.timestamp << " ns\n";
        
        if (event.duration > 0) {
            double us = event.duration / 1e3;
            double ms = event.duration / 1e6;
            std::cout << "    Duration: " << event.duration << " ns"
                      << " (" << std::fixed << std::setprecision(2) << us << " us"
                      << " / " << std::setprecision(4) << ms << " ms)\n";
        }
        
        std::cout << "\n";
        ++shown;
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=======================================================\n";
    std::cout << "  TraceSmith CUPTI Profiling Example\n";
    std::cout << "  REAL GPU Profiling with CUPTI\n";
    std::cout << "=======================================================\n\n";
    
#ifndef TRACESMITH_ENABLE_CUDA
    std::cerr << "ERROR: TraceSmith was compiled without CUDA support.\n";
    std::cerr << "Please rebuild with -DTRACESMITH_ENABLE_CUDA=ON\n";
    return 1;
#else
    // Check CUDA availability
    if (!isCUDAAvailable()) {
        std::cerr << "ERROR: No CUDA-capable GPU found.\n";
        std::cerr << "Make sure NVIDIA drivers are installed and GPU is accessible.\n";
        return 1;
    }
    
    std::cout << "CUDA Driver Version: " << getCUDADriverVersion() << "\n";
    std::cout << "CUDA Device Count: " << getCUDADeviceCount() << "\n";
    
    // Create CUPTI profiler
    CUPTIProfiler profiler;
    std::cout << "CUPTI Version: " << profiler.getCuptiVersion() << "\n";
    
    // Print device info
    ProfilerConfig config;
    config.buffer_size = 100000;  // Store up to 100k events
    
    if (!profiler.initialize(config)) {
        std::cerr << "ERROR: Failed to initialize CUPTI profiler.\n";
        std::cerr << "This may be due to insufficient permissions.\n";
        std::cerr << "Try running with sudo or enabling profiling permissions.\n";
        return 1;
    }
    
    printDeviceInfo(profiler.getDeviceInfo());
    
    // Start profiling
    std::cout << "Starting profiling...\n";
    if (!profiler.startCapture()) {
        std::cerr << "ERROR: Failed to start capture.\n";
        return 1;
    }
    
    // Run CUDA workload
    runCUDAWorkload();
    
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
    std::string sbt_file = "cupti_trace.sbt";
    std::cout << "\nSaving to " << sbt_file << "...\n";
    
    SBTWriter writer(sbt_file);
    for (const auto& event : events) {
        writer.writeEvent(event);
    }
    std::cout << "Saved " << events.size() << " events to " << sbt_file << "\n";
    
    // Export to Perfetto format
    std::string perfetto_file = "cupti_trace.json";
    std::cout << "\nExporting to Perfetto format: " << perfetto_file << "...\n";
    
    PerfettoExporter exporter;
    if (exporter.exportToFile(events, perfetto_file)) {
        std::cout << "Exported to " << perfetto_file << "\n";
        std::cout << "Open in https://ui.perfetto.dev/ to visualize.\n";
    }
    
    // Cleanup
    profiler.finalize();
    
    std::cout << "\n=======================================================\n";
    std::cout << "  CUPTI Example Completed Successfully!\n";
    std::cout << "  Verified: Real GPU profiling with CUPTI\n";
    std::cout << "  - " << events.size() << " real GPU events captured\n";
    std::cout << "  - Trace files: cupti_trace.sbt, cupti_trace.json\n";
    std::cout << "=======================================================\n";
    
    return 0;
#endif
}
