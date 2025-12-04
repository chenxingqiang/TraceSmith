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
#include "tracesmith/state/gpu_state_machine.hpp"
#include "tracesmith/state/timeline_builder.hpp"
#include "tracesmith/state/perfetto_exporter.hpp"
#include "tracesmith/state/timeline_viewer.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <random>

using namespace tracesmith;

// Generate multi-stream GPU workload events
std::vector<TraceEvent> generateMultiStreamEvents() {
    std::vector<TraceEvent> events;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> duration_dist(50000, 350000);  // 50-350 µs
    
    Timestamp base_time = getCurrentTimestamp();
    const int num_streams = 3;
    const int kernels_per_stream = 8;
    
    // Track current time per stream
    std::vector<Timestamp> stream_times(num_streams, base_time);
    uint32_t correlation_id = 1;
    
    // Generate kernels with some overlap between streams
    for (int i = 0; i < kernels_per_stream; ++i) {
        for (int stream = 0; stream < num_streams; ++stream) {
            TraceEvent event;
            event.type = EventType::KernelLaunch;
            event.name = "kernel_s" + std::to_string(stream) + "_" + std::to_string(i);
            event.device_id = 0;
            event.stream_id = stream;
            event.correlation_id = correlation_id++;
            event.duration = duration_dist(gen);
            event.timestamp = stream_times[stream];
            
            // Add kernel parameters
            event.kernel_params = KernelParams{};
            event.kernel_params->grid_x = 256 + stream * 64;
            event.kernel_params->grid_y = 128;
            event.kernel_params->grid_z = 1;
            event.kernel_params->block_x = 32;
            event.kernel_params->block_y = 8;
            event.kernel_params->block_z = 1;
            event.kernel_params->shared_mem_bytes = stream * 1024;
            event.kernel_params->registers_per_thread = 32;
            
            events.push_back(event);
            stream_times[stream] += event.duration + 10000;  // Small gap
        }
    }
    
    // Add some memory operations
    for (int i = 0; i < 3; ++i) {
        TraceEvent memcpy;
        memcpy.type = EventType::MemcpyH2D;
        memcpy.name = "MemcpyH2D_" + std::to_string(i);
        memcpy.device_id = 0;
        memcpy.stream_id = i;
        memcpy.correlation_id = correlation_id++;
        memcpy.duration = 50000 + i * 10000;
        memcpy.timestamp = stream_times[i];
        
        memcpy.memory_params = MemoryParams{};
        memcpy.memory_params->size_bytes = (1 + i) * 1024 * 1024;  // 1-3 MB
        
        events.push_back(memcpy);
        stream_times[i] += memcpy.duration + 5000;
    }
    
    // Add sync events
    Timestamp max_time = *std::max_element(stream_times.begin(), stream_times.end());
    
    TraceEvent device_sync;
    device_sync.type = EventType::DeviceSync;
    device_sync.name = "cudaDeviceSynchronize";
    device_sync.device_id = 0;
    device_sync.stream_id = 0;
    device_sync.correlation_id = correlation_id++;
    device_sync.timestamp = max_time;
    device_sync.duration = 5000;
    events.push_back(device_sync);
    
    // Sort by timestamp
    std::sort(events.begin(), events.end(), 
              [](const TraceEvent& a, const TraceEvent& b) {
                  return a.timestamp < b.timestamp;
              });
    
    return events;
}

int main() {
    std::cout << "TraceSmith Phase 3 Example\n";
    std::cout << "==========================\n\n";
    
    // Step 1: Generate events
    std::cout << "1. Generating multi-stream GPU events...\n";
    std::vector<TraceEvent> events = generateMultiStreamEvents();
    std::cout << "   Generated " << events.size() << " events\n\n";
    
    // Step 2: GPU State Machine
    std::cout << "2. Building GPU State Machine...\n";
    GPUStateMachine state_machine;
    
    for (const auto& event : events) {
        state_machine.processEvent(event);
    }
    
    auto stats = state_machine.getStatistics();
    std::cout << "   Total events:       " << stats.total_events << "\n";
    std::cout << "   Total transitions:  " << stats.total_transitions << "\n";
    std::cout << "   GPU utilization:    " << std::fixed << std::setprecision(1) 
              << (stats.overall_utilization * 100.0) << "%\n\n";
    
    // Show state history for each stream
    std::cout << "   Stream state history:\n";
    auto all_streams = state_machine.getAllStreams();
    for (const auto& stream_key : all_streams) {
        auto* stream_state = state_machine.getStreamState(stream_key.first, stream_key.second);
        if (stream_state) {
            std::cout << "     Stream " << stream_key.second << ": " 
                      << stream_state->transitions().size() << " transitions, "
                      << std::fixed << std::setprecision(1) 
                      << (stream_state->utilization() * 100.0) << "% utilization\n";
        }
    }
    std::cout << "\n";
    
    // Step 3: Timeline Builder
    std::cout << "3. Building Timeline...\n";
    TimelineBuilder timeline_builder;
    timeline_builder.addEvents(events);
    Timeline timeline = timeline_builder.build();
    
    std::cout << "   Timeline spans:      " << timeline.spans.size() << "\n";
    std::cout << "   Total duration:      " << std::fixed << std::setprecision(2) 
              << (timeline.total_duration / 1000.0) << " µs\n";
    std::cout << "   GPU utilization:     " << std::fixed << std::setprecision(1) 
              << (timeline.gpu_utilization * 100.0) << "%\n";
    std::cout << "   Max concurrent ops:  " << timeline.max_concurrent_ops << "\n\n";
    
    // Step 4: Text Timeline Viewer
    std::cout << "4. ASCII Timeline Visualization:\n";
    std::cout << "   " << std::string(60, '-') << "\n";
    
    TimelineViewer::ViewConfig view_config;
    view_config.width = 60;
    view_config.max_rows = 12;
    TimelineViewer viewer(view_config);
    
    std::string ascii_timeline = viewer.render(timeline);
    // Indent each line
    std::istringstream iss(ascii_timeline);
    std::string line;
    while (std::getline(iss, line)) {
        std::cout << "   " << line << "\n";
    }
    std::cout << "   " << std::string(60, '-') << "\n\n";
    
    // Step 5: Stream details
    std::cout << "5. Stream 0 Details:\n";
    std::cout << viewer.renderStream(timeline, 0) << "\n";
    
    // Step 6: Statistics
    std::cout << "6. Timeline Statistics:\n";
    std::cout << viewer.renderStats(timeline) << "\n";
    
    // Step 7: Perfetto Export
    std::cout << "7. Exporting to Perfetto format...\n";
    PerfettoExporter exporter;
    exporter.setEnableGPUTracks(true);
    exporter.setEnableFlowEvents(true);
    
    std::string perfetto_file = "phase3_trace.json";
    if (exporter.exportToFile(events, perfetto_file)) {
        std::cout << "   Exported to: " << perfetto_file << "\n";
        std::cout << "   View at: chrome://tracing or https://ui.perfetto.dev\n\n";
    } else {
        std::cerr << "   Failed to export Perfetto trace\n\n";
    }
    
    // Step 8: Save to SBT format
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
    device.name = "TraceSmith GPU";
    device.vendor = "TraceSmith";
    device.multiprocessor_count = 80;
    device.clock_rate = 1700000;  // kHz
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
