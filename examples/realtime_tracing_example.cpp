/**
 * Real-time Tracing Example (v0.3.0)
 * 
 * Demonstrates TracingSession for thread-safe event collection:
 * - Lock-free event emission from real GPU profiler
 * - Counter tracks for metrics
 * - Automatic export to Perfetto format
 * 
 * Requirements:
 * - NVIDIA GPU with CUDA for full functionality
 * - Build with -DTRACESMITH_ENABLE_CUDA=ON
 */

#include "tracesmith/common/types.hpp"
#include "tracesmith/state/perfetto_proto_exporter.hpp"
#include "tracesmith/capture/profiler.hpp"
#include <iostream>
#include <thread>
#include <chrono>

#ifdef TRACESMITH_ENABLE_CUDA
#include "tracesmith/capture/cupti_profiler.hpp"
#include <cuda_runtime.h>

// Real CUDA kernel for tracing
__global__ void trace_kernel(float* data, int n, int iter) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        data[idx] = data[idx] * 1.5f + static_cast<float>(iter);
    }
}
#endif

using namespace tracesmith;

// Run real GPU workload and capture events
void runRealGPUWorkload(TracingSession& session, int workload_id) {
#ifdef TRACESMITH_ENABLE_CUDA
    const int N = 256 * 1024;  // 256K elements
    float* d_data;
    cudaMalloc(&d_data, N * sizeof(float));
    
    std::vector<float> h_data(N, 1.0f);
    cudaMemcpy(d_data, h_data.data(), N * sizeof(float), cudaMemcpyHostToDevice);
    
    int threads = 256;
    int blocks = (N + threads - 1) / threads;
    
    for (int i = 0; i < 50; ++i) {
        // Create event for kernel launch
        TraceEvent kernel(EventType::KernelLaunch);
        kernel.timestamp = getCurrentTimestamp();
        kernel.name = "trace_kernel_" + std::to_string(workload_id) + "_" + std::to_string(i);
        kernel.device_id = 0;
        kernel.stream_id = 0;
        kernel.thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        
        // Add Kineto-style metadata
        kernel.metadata["operator"] = "custom::trace_kernel";
        kernel.metadata["grid"] = std::to_string(blocks);
        kernel.metadata["block"] = std::to_string(threads);
        kernel.flow_info = FlowInfo(workload_id * 1000 + i, FlowType::FwdBwd, i % 2 == 0);
        
        // Launch real CUDA kernel
        auto start = std::chrono::high_resolution_clock::now();
        trace_kernel<<<blocks, threads>>>(d_data, N, i);
        cudaDeviceSynchronize();
        auto end = std::chrono::high_resolution_clock::now();
        
        kernel.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        session.emit(std::move(kernel));
        
        // Memory operation every 5 iterations
        if (i % 5 == 0) {
            TraceEvent memcpy_event(EventType::MemcpyD2H);
            memcpy_event.timestamp = getCurrentTimestamp();
            memcpy_event.name = "cudaMemcpy_D2H_" + std::to_string(i);
            memcpy_event.device_id = 0;
            memcpy_event.stream_id = 0;
            
            MemoryParams mp;
            mp.size_bytes = N * sizeof(float);
            memcpy_event.memory_params = mp;
            
            start = std::chrono::high_resolution_clock::now();
            cudaMemcpy(h_data.data(), d_data, N * sizeof(float), cudaMemcpyDeviceToHost);
            end = std::chrono::high_resolution_clock::now();
            
            memcpy_event.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            session.emit(std::move(memcpy_event));
        }
        
        // Query real GPU metrics if available
        size_t free_mem, total_mem;
        cudaMemGetInfo(&free_mem, &total_mem);
        double used_mb = (total_mem - free_mem) / (1024.0 * 1024.0);
        session.emitCounter("GPU Memory Used (MB)", used_mb);
        session.emitCounter("Iteration", static_cast<double>(i));
    }
    
    cudaFree(d_data);
#else
    // Without CUDA, create representative events
    std::cout << "  (CUDA not available, creating sample events)\n";
    
    for (int i = 0; i < 50; ++i) {
        TraceEvent kernel(EventType::KernelLaunch);
        kernel.timestamp = getCurrentTimestamp();
        kernel.duration = 50000 + (i * 1000);  // 50-100 us
        kernel.name = "kernel_" + std::to_string(workload_id) + "_" + std::to_string(i);
        kernel.device_id = 0;
        kernel.stream_id = i % 4;
        kernel.thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        kernel.metadata["operator"] = "sample::kernel";
        kernel.flow_info = FlowInfo(workload_id * 1000 + i, FlowType::FwdBwd, i % 2 == 0);
        
        session.emit(std::move(kernel));
        session.emitCounter("Iteration", static_cast<double>(i));
        
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
#endif
}

int main() {
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Real-time Tracing Example - Real GPU Profiling           ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";
    
    // Check GPU and SDK availability
#ifdef TRACESMITH_ENABLE_CUDA
    if (isCUDAAvailable()) {
        std::cout << "✓ CUDA available: " << getCUDADeviceCount() << " device(s)\n";
        std::cout << "  Driver version: " << getCUDADriverVersion() << "\n";
    } else {
        std::cout << "⚠ CUDA not available on this system\n";
    }
#else
    std::cout << "⚠ Built without CUDA support\n";
#endif
    
    std::cout << "Perfetto SDK: " 
              << (PerfettoProtoExporter::isSDKAvailable() ? "YES" : "NO") << "\n\n";
    
    // Create tracing session with custom buffer size
    TracingSession session(16384, 4096);  // 16K events, 4K counters
    
    std::cout << "Event buffer capacity: " << session.eventBufferCapacity() << "\n";
    
    // Configure and start session
    TracingConfig config;
    config.buffer_size_kb = 4096;
    config.enable_gpu_tracks = true;
    config.enable_counter_tracks = true;
    config.enable_flow_events = true;
    
    std::cout << "\nStarting tracing session...\n";
    if (!session.start(config)) {
        std::cerr << "Failed to start tracing session\n";
        return 1;
    }
    
    // Run real GPU workloads
    std::cout << "Running GPU workloads...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Run 3 workloads sequentially
    for (int w = 0; w < 3; ++w) {
        std::cout << "  Workload " << (w + 1) << "/3...\n";
        runRealGPUWorkload(session, w);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Stop session
    session.stop();
    
    // Print statistics
    const auto& stats = session.getStatistics();
    std::cout << "\n--- Session Statistics ---\n";
    std::cout << "Events emitted:   " << stats.events_emitted << "\n";
    std::cout << "Events dropped:   " << stats.events_dropped << "\n";
    std::cout << "Counters emitted: " << stats.counters_emitted << "\n";
    std::cout << "Duration:         " << stats.duration_ms() << " ms\n";
    std::cout << "Wall time:        " << duration.count() << " ms\n";
    std::cout << "Events rate:      " << (stats.events_emitted * 1000.0 / duration.count()) 
              << " events/sec\n";
    
    // Get captured data
    const auto& events = session.getEvents();
    const auto& counters = session.getCounters();
    
    std::cout << "\nCaptured " << events.size() << " events and " 
              << counters.size() << " counter samples\n";
    
    // Export to files
    std::cout << "\n--- Exporting Traces ---\n";
    
    // Export to Perfetto protobuf (if SDK available)
    std::string proto_file = "realtime_trace.perfetto-trace";
    if (session.exportToFile(proto_file, true)) {
        std::cout << "✓ Exported to: " << proto_file << " (protobuf)\n";
    }
    
    // Export to JSON
    std::string json_file = "realtime_trace.json";
    if (session.exportToFile(json_file, false)) {
        std::cout << "✓ Exported to: " << json_file << " (JSON)\n";
    }
    
    // Show sample events
    std::cout << "\n--- Sample Events ---\n";
    for (size_t i = 0; i < std::min(events.size(), size_t(5)); ++i) {
        const auto& e = events[i];
        std::cout << "  " << e.name << " (stream " << e.stream_id 
                  << ", duration " << (e.duration / 1000) << " µs)\n";
    }
    
    // Show sample counters
    std::cout << "\n--- Sample Counters ---\n";
    for (size_t i = 0; i < std::min(counters.size(), size_t(5)); ++i) {
        const auto& c = counters[i];
        std::cout << "  " << c.counter_name << " = " << c.value << "\n";
    }
    
    std::cout << "\n✅ Real-time tracing example complete!\n";
    std::cout << "\nView traces in:\n";
    std::cout << "  - Perfetto UI: https://ui.perfetto.dev\n";
    std::cout << "  - Load " << proto_file << " or " << json_file << "\n";
    
    return 0;
}

