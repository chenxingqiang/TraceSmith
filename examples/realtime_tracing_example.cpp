/**
 * Real-time Tracing Example (v0.3.0)
 * 
 * Demonstrates TracingSession for thread-safe event collection:
 * - Lock-free event emission
 * - Counter tracks for metrics
 * - Automatic export to Perfetto format
 */

#include "tracesmith/types.hpp"
#include "tracesmith/perfetto_proto_exporter.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>

using namespace tracesmith;

// Simulate GPU workload
void simulateGPUWorkload(TracingSession& session, int workload_id) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> duration_dist(100, 1000);  // microseconds
    std::uniform_int_distribution<> stream_dist(0, 3);
    
    for (int i = 0; i < 50; ++i) {
        // Simulate kernel launch
        TraceEvent kernel(EventType::KernelLaunch);
        kernel.timestamp = getCurrentTimestamp();
        kernel.duration = duration_dist(gen) * 1000;  // Convert to nanoseconds
        kernel.name = "kernel_" + std::to_string(workload_id) + "_" + std::to_string(i);
        kernel.device_id = 0;
        kernel.stream_id = stream_dist(gen);
        kernel.thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        
        // Add Kineto-style metadata
        kernel.metadata["operator"] = "aten::matmul";
        kernel.metadata["input_shape"] = "[256, 256]";
        kernel.flow_info = FlowInfo(workload_id * 1000 + i, FlowType::FwdBwd, i % 2 == 0);
        
        session.emit(std::move(kernel));
        
        // Simulate memory operation
        if (i % 5 == 0) {
            TraceEvent memcpy(EventType::MemcpyH2D);
            memcpy.timestamp = getCurrentTimestamp();
            memcpy.duration = duration_dist(gen) * 500;
            memcpy.name = "memcpy_" + std::to_string(i);
            memcpy.device_id = 0;
            memcpy.stream_id = stream_dist(gen);
            
            MemoryParams mp;
            mp.size_bytes = 1024 * 1024;  // 1MB
            memcpy.memory_params = mp;
            
            session.emit(std::move(memcpy));
        }
        
        // Emit counter metrics
        session.emitCounter("GPU Bandwidth (GB/s)", 400.0 + (i % 100));
        session.emitCounter("SM Occupancy (%)", 70.0 + (i % 30));
        
        // Small delay to simulate real timing
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

int main() {
    std::cout << "Real-time Tracing Example (v0.3.0)\n";
    std::cout << "===================================\n\n";
    
    // Check SDK availability
    std::cout << "Perfetto SDK available: " 
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
    
    // Run simulated workloads (single producer for SPSC buffer)
    std::cout << "Running GPU workload simulation...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Simulate 3 workloads sequentially
    for (int w = 0; w < 3; ++w) {
        simulateGPUWorkload(session, w);
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

