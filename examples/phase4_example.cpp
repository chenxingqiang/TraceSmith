/**
 * Phase 4 Example: GPU Replay Engine
 * 
 * Demonstrates:
 * - Full trace replay
 * - Partial replay (time ranges)
 * - Stream-specific replay
 * - Dry-run mode for validation
 * - Determinism checking
 */

#include "tracesmith/tracesmith.hpp"
#include "tracesmith/replay_engine.hpp"
#include <iostream>

using namespace tracesmith;

// Helper to capture a trace
std::vector<TraceEvent> captureTrace() {
    auto profiler = createProfiler(PlatformType::Simulation);
    
    ProfilerConfig config;
    config.capture_callstacks = false;
    profiler->initialize(config);
    profiler->startCapture();
    
    // Generate multi-stream workload
    auto* sim = dynamic_cast<SimulationProfiler*>(profiler.get());
    if (sim) {
        for (int stream = 0; stream < 3; ++stream) {
            for (int i = 0; i < 10; ++i) {
                std::string name = "kernel_s" + std::to_string(stream) + "_" + std::to_string(i);
                sim->generateKernelEvent(name, stream);
            }
        }
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    profiler->stopCapture();
    
    std::vector<TraceEvent> events;
    profiler->getEvents(events, 0);
    
    return events;
}

int main() {
    std::cout << "TraceSmith Phase 4 Example\n";
    std::cout << "==========================\n\n";
    
    // Step 1: Capture a trace
    std::cout << "1. Capturing trace...\n";
    auto events = captureTrace();
    std::cout << "   Captured " << events.size() << " events\n\n";
    
    // Step 2: Full replay
    std::cout << "2. Full Replay\n";
    std::cout << "   ------------\n";
    
    ReplayEngine engine;
    engine.loadEvents(events);
    
    ReplayConfig config_full;
    config_full.mode = ReplayMode::Full;
    config_full.validate_order = true;
    config_full.validate_dependencies = true;
    
    auto result_full = engine.replay(config_full);
    std::cout << result_full.summary();
    std::cout << "   Replay duration: " << (result_full.replay_duration / 1000000.0) << " ms\n\n";
    
    // Step 3: Dry-run mode
    std::cout << "3. Dry-Run Mode (validation without execution)\n";
    std::cout << "   -------------------------------------------\n";
    
    ReplayConfig config_dryrun;
    config_dryrun.mode = ReplayMode::DryRun;
    config_dryrun.validate_order = true;
    config_dryrun.validate_dependencies = true;
    
    auto result_dryrun = engine.replay(config_dryrun);
    std::cout << result_dryrun.summary();
    std::cout << "   Dry-run overhead: " << (result_dryrun.replay_duration / 1000.0) << " µs\n\n";
    
    // Step 4: Partial replay (first 50% of operations)
    std::cout << "4. Partial Replay (first 50% operations)\n";
    std::cout << "   -------------------------------------\n";
    
    ReplayConfig config_partial;
    config_partial.mode = ReplayMode::Partial;
    config_partial.start_operation_id = 0;
    config_partial.end_operation_id = events.size() / 2;
    config_partial.validate_order = true;
    
    auto result_partial = engine.replay(config_partial);
    std::cout << result_partial.summary();
    std::cout << "\n";
    
    // Step 5: Stream-specific replay
    std::cout << "5. Stream-Specific Replay (stream 0 only)\n";
    std::cout << "   ---------------------------------------\n";
    
    ReplayConfig config_stream;
    config_stream.mode = ReplayMode::StreamSpecific;
    config_stream.stream_id = 0;
    config_stream.validate_order = true;
    
    auto result_stream = engine.replay(config_stream);
    std::cout << result_stream.summary();
    std::cout << "\n";
    
    // Step 6: Partial replay by time range
    if (!events.empty()) {
        std::cout << "6. Time-Range Replay\n";
        std::cout << "   -----------------\n";
        
        Timestamp start_time = events.front().timestamp;
        Timestamp end_time = events.back().timestamp;
        Timestamp mid_time = start_time + (end_time - start_time) / 2;
        
        ReplayConfig config_time;
        config_time.mode = ReplayMode::Partial;
        config_time.start_time = start_time;
        config_time.end_time = mid_time;
        
        auto result_time = engine.replay(config_time);
        std::cout << result_time.summary();
        std::cout << "\n";
    }
    
    // Step 7: Validation report
    std::cout << "7. Determinism Validation Report\n";
    std::cout << "   ------------------------------\n";
    std::cout << engine.getChecker().getReport() << "\n";
    
    // Step 8: Save trace for future replay
    std::cout << "8. Saving trace for future replay...\n";
    SBTWriter writer("phase4_trace.sbt");
    
    TraceMetadata metadata;
    metadata.application_name = "Phase4Example";
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
    std::cout << "   Saved to: phase4_trace.sbt\n\n";
    
    // Step 9: Load and replay from file
    std::cout << "9. Loading and replaying from file...\n";
    ReplayEngine file_engine;
    if (file_engine.loadTrace("phase4_trace.sbt")) {
        ReplayConfig config_file;
        config_file.mode = ReplayMode::Full;
        config_file.validate_order = true;
        
        auto result_file = file_engine.replay(config_file);
        std::cout << "   File replay: " << result_file.operations_executed 
                  << "/" << result_file.operations_total << " operations\n";
        std::cout << "   Validation: " << (result_file.deterministic ? "PASS" : "FAIL") << "\n\n";
    } else {
        std::cout << "   Failed to load trace file\n\n";
    }
    
    std::cout << "Phase 4 Example Complete!\n";
    std::cout << "========================\n\n";
    std::cout << "Key Features Demonstrated:\n";
    std::cout << "  ✓ Full trace replay with validation\n";
    std::cout << "  ✓ Dry-run mode (no execution)\n";
    std::cout << "  ✓ Partial replay (operation range)\n";
    std::cout << "  ✓ Stream-specific replay\n";
    std::cout << "  ✓ Time-range replay\n";
    std::cout << "  ✓ Determinism checking\n";
    std::cout << "  ✓ Save and load traces\n\n";
    std::cout << "Next steps:\n";
    std::cout << "  - Try: ./tracesmith-cli replay phase4_trace.sbt\n";
    std::cout << "  - Experiment with different replay configurations\n";
    std::cout << "  - Analyze replay performance metrics\n";
    
    return 0;
}
