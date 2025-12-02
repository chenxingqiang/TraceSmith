/**
 * Phase 3 Example: GPU State Machine & Timeline Builder
 * 
 * Demonstrates:
 * - GPU state machine with state transitions
 * - Timeline building from trace events
 * - Perfetto export for chrome://tracing
 * - Text-based timeline visualization
 */

#include "tracesmith/tracesmith.hpp"
#include "tracesmith/gpu_state_machine.hpp"
#include "tracesmith/timeline_builder.hpp"
#include "tracesmith/perfetto_exporter.hpp"
#include "tracesmith/timeline_viewer.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace tracesmith;

int main() {
    std::cout << "TraceSmith Phase 3 Example\n";
    std::cout << "==========================\n\n";
    
    // Initialize profiler
    auto profiler = createProfiler(PlatformType::Simulation);
    if (!profiler) {
        std::cerr << "Failed to create profiler\n";
        return 1;
    }
    
    ProfilerConfig config;
    config.capture_callstacks = false;
    if (!profiler->initialize(config)) {
        std::cerr << "Failed to initialize profiler\n";
        return 1;
    }
    
    // Start capturing
    if (!profiler->startCapture()) {
        std::cerr << "Failed to start capture\n";
        return 1;
    }
    
    std::cout << "1. Capturing GPU events...\n";
    
    // Simulate GPU operations on multiple streams
    const int num_streams = 3;
    const int kernels_per_stream = 5;
    
    for (int stream = 0; stream < num_streams; ++stream) {
        for (int i = 0; i < kernels_per_stream; ++i) {
            TraceEvent event(EventType::KernelLaunch);
            event.name = "kernel_stream" + std::to_string(stream) + "_" + std::to_string(i);
            event.device_id = 0;
            event.stream_id = stream;
            event.correlation_id = stream * 1000 + i;
            event.duration = 100000 + (i * 50000); // Varying durations (100-350us)
            
            // Store event for simulation profiler
            auto* sim_profiler = dynamic_cast<SimulationProfiler*>(profiler.get());
            if (sim_profiler) {
                sim_profiler->generateKernelEvent(event.name, stream);
            }
            
            // Small delay between events
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    
    // Add some memory operations
    TraceEvent memcpy(EventType::MemcpyH2D);
    memcpy.name = "MemcpyH2D";
    memcpy.device_id = 0;
    memcpy.stream_id = 0;
    memcpy.correlation_id = 10000;
    memcpy.duration = 50000; // 50us
    MemoryParams mem_params;
    mem_params.size_bytes = 1024 * 1024; // 1MB
    memcpy.memory_params = mem_params;
    // Record memory operation
    auto* sim_profiler = dynamic_cast<SimulationProfiler*>(profiler.get());
    if (sim_profiler) {
        sim_profiler->generateMemcpyEvent(EventType::MemcpyH2D, 1024 * 1024, 0);
    }
    
    // Stop capturing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    profiler->stopCapture();
    
    // Get captured events
    std::vector<TraceEvent> events;
    profiler->getEvents(events, 0);
    std::cout << "   Captured " << events.size() << " events\n\n";
    
    // Phase 3: GPU State Machine
    std::cout << "2. Building GPU State Machine...\n";
    GPUStateMachine state_machine;
    
    for (const auto& event : events) {
        state_machine.processEvent(event);
    }
    
    auto stats = state_machine.getStatistics();
    std::cout << "   Total events: " << stats.total_events << "\n";
    std::cout << "   Total transitions: " << stats.total_transitions << "\n";
    std::cout << "   GPU utilization: " << (stats.overall_utilization * 100.0) << "%\n\n";
    
    // Phase 3: Timeline Builder
    std::cout << "3. Building Timeline...\n";
    TimelineBuilder timeline_builder;
    timeline_builder.addEvents(events);
    Timeline timeline = timeline_builder.build();
    
    std::cout << "   Timeline spans: " << timeline.spans.size() << "\n";
    std::cout << "   Total duration: " << (timeline.total_duration / 1000.0) << " µs\n";
    std::cout << "   GPU utilization: " << (timeline.gpu_utilization * 100.0) << "%\n";
    std::cout << "   Max concurrent ops: " << timeline.max_concurrent_ops << "\n\n";
    
    // Phase 3: Text Timeline Viewer
    std::cout << "4. ASCII Timeline Visualization:\n";
    TimelineViewer::ViewConfig view_config;
    view_config.width = 60;
    view_config.max_rows = 10;
    TimelineViewer viewer(view_config);
    std::cout << viewer.render(timeline) << "\n";
    
    // Show stream details
    std::cout << "5. Stream 0 Details:\n";
    std::cout << viewer.renderStream(timeline, 0) << "\n";
    
    // Statistics
    std::cout << "6. Timeline Statistics:\n";
    std::cout << viewer.renderStats(timeline) << "\n";
    
    // Phase 3: Perfetto Export
    std::cout << "7. Exporting to Perfetto format...\n";
    PerfettoExporter exporter;
    
    std::string perfetto_file = "phase3_trace.json";
    if (exporter.exportToFile(events, perfetto_file)) {
        std::cout << "   Exported to: " << perfetto_file << "\n";
        std::cout << "   View at: chrome://tracing or https://ui.perfetto.dev\n\n";
    } else {
        std::cerr << "   Failed to export Perfetto trace\n\n";
    }
    
    // Save to SBT format as well
    std::cout << "8. Saving to SBT format...\n";
    SBTWriter writer("phase3_trace.sbt");
    
    TraceMetadata metadata;
    metadata.application_name = "Phase3Example";
    metadata.start_time = events.front().timestamp;
    metadata.end_time = events.back().timestamp;
    writer.writeMetadata(metadata);
    
    std::vector<DeviceInfo> devices;
    DeviceInfo device;
    device.device_id = 0;
    device.name = "Simulation GPU";
    device.vendor = "TraceSmith";
    devices.push_back(device);
    writer.writeDeviceInfo(devices);
    
    for (const auto& event : events) {
        writer.writeEvent(event);
    }
    
    writer.finalize();
    std::cout << "   Saved to: phase3_trace.sbt\n\n";
    
    std::cout << "Phase 3 Example Complete!\n";
    std::cout << "========================\n";
    std::cout << "\nNext steps:\n";
    std::cout << "  - View phase3_trace.json in chrome://tracing\n";
    std::cout << "  - Run: ./tracesmith-cli info phase3_trace.sbt\n";
    std::cout << "  - Run: ./tracesmith-cli view phase3_trace.sbt\n";
    
    return 0;
}
