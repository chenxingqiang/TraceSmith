/**
 * Counter Track Visualization Example
 * 
 * Demonstrates how to use CounterEvent for time-series metrics visualization
 * in Perfetto UI. This example creates GPU performance counters that will
 * appear as separate tracks with line graphs.
 * 
 * Usage:
 *   ./counter_track_example
 *   # Then open counter_trace.json in https://ui.perfetto.dev
 */

#include <tracesmith/common/types.hpp>
#include <tracesmith/state/perfetto_exporter.hpp>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <random>

using namespace tracesmith;

int main() {
    std::cout << "TraceSmith Counter Track Visualization Example\n";
    std::cout << "==============================================\n\n";
    
    std::vector<TraceEvent> events;
    std::vector<CounterEvent> counters;
    
    // Random generator for realistic-looking metrics
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> noise(-5.0, 5.0);
    
    // Simulate 2 seconds of GPU activity with metrics
    Timestamp base_time = 1000000000; // 1 second in nanoseconds
    const int num_kernels = 20;
    const Timestamp interval = 100000000; // 100ms between kernels
    
    for (int i = 0; i < num_kernels; ++i) {
        Timestamp t = base_time + i * interval;
        
        // Create a kernel launch event
        TraceEvent kernel;
        kernel.name = "compute_kernel_" + std::to_string(i % 4);
        kernel.type = EventType::KernelLaunch;
        kernel.timestamp = t;
        kernel.duration = 50000000 + (i % 3) * 10000000; // 50-70ms duration
        kernel.device_id = 0;
        kernel.stream_id = i % 2;
        kernel.correlation_id = i + 1;
        
        // Add kernel parameters
        kernel.kernel_params = KernelParams{};
        kernel.kernel_params->grid_x = 256 + (i % 4) * 64;
        kernel.kernel_params->grid_y = 256;
        kernel.kernel_params->grid_z = 1;
        kernel.kernel_params->block_x = 32;
        kernel.kernel_params->block_y = 8;
        kernel.kernel_params->block_z = 1;
        
        events.push_back(kernel);
        
        // ============================================
        // Create counter events for this time point
        // ============================================
        
        // 1. SM Occupancy (percentage)
        double occupancy = 70.0 + 15.0 * std::sin(i * 0.5) + noise(gen);
        counters.push_back(CounterEvent("SM Occupancy", occupancy, t, "%"));
        
        // 2. GPU Memory Usage (GB)
        double mem_usage = 4.0 + 2.0 * std::sin(i * 0.3) + 0.5 * noise(gen) / 5.0;
        counters.push_back(CounterEvent("GPU Memory (GB)", mem_usage, t, "GB"));
        
        // 3. Memory Bandwidth (GB/s)
        double bandwidth = 400.0 + 100.0 * std::cos(i * 0.4) + noise(gen) * 2;
        counters.push_back(CounterEvent("Memory Bandwidth", bandwidth, t, "GB/s"));
        
        // 4. Power Consumption (W)
        double power = 180.0 + 40.0 * std::sin(i * 0.6) + noise(gen);
        counters.push_back(CounterEvent("Power (W)", power, t, "W"));
        
        // 5. Temperature (°C)
        double temp = 65.0 + 10.0 * (i / (double)num_kernels) + noise(gen) * 0.5;
        counters.push_back(CounterEvent("Temperature", temp, t, "°C"));
        
        // 6. FLOPs (TFLOPs)
        double flops = 5.0 + 3.0 * std::sin(i * 0.7) + noise(gen) * 0.3;
        counters.push_back(CounterEvent("TFLOPs", flops, t, "TFLOPs"));
    }
    
    std::cout << "Generated " << events.size() << " kernel events\n";
    std::cout << "Generated " << counters.size() << " counter samples\n";
    std::cout << "  - SM Occupancy (%)\n";
    std::cout << "  - GPU Memory (GB)\n";
    std::cout << "  - Memory Bandwidth (GB/s)\n";
    std::cout << "  - Power (W)\n";
    std::cout << "  - Temperature (°C)\n";
    std::cout << "  - TFLOPs\n\n";
    
    // Export to Perfetto JSON with counter tracks
    PerfettoExporter exporter;
    exporter.setEnableGPUTracks(true);
    exporter.setEnableFlowEvents(true);
    exporter.setEnableCounterTracks(true);
    
    const std::string output_file = "counter_trace.json";
    if (exporter.exportToFile(events, counters, output_file)) {
        std::cout << "✅ Exported to: " << output_file << "\n";
        std::cout << "\nVisualization Tips:\n";
        std::cout << "  1. Open https://ui.perfetto.dev\n";
        std::cout << "  2. Click 'Open trace file' and select " << output_file << "\n";
        std::cout << "  3. Look for 'Performance Counters' process\n";
        std::cout << "  4. Each counter appears as a separate track with line graphs\n";
        std::cout << "  5. Hover over points to see exact values\n";
        std::cout << "  6. Use Ctrl+scroll to zoom in/out\n";
    } else {
        std::cerr << "❌ Failed to export trace\n";
        return 1;
    }
    
    // Print sample counter values
    std::cout << "\nSample Counter Values:\n";
    std::cout << "────────────────────────────────────────\n";
    for (int i = 0; i < std::min(6, (int)counters.size()); ++i) {
        const auto& c = counters[i];
        std::cout << "  " << c.counter_name << ": " << std::fixed << std::setprecision(2) 
                  << c.value << " " << c.unit << "\n";
    }
    
    return 0;
}

