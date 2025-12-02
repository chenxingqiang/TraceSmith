/**
 * TraceSmith Basic Example
 * 
 * This example demonstrates basic usage of TraceSmith:
 * - Creating a profiler
 * - Capturing GPU events (simulated)
 * - Writing trace to file
 * - Reading and analyzing trace
 */

#include <tracesmith/tracesmith.hpp>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace tracesmith;

int main() {
    std::cout << "TraceSmith v" << getVersionString() << " Basic Example\n\n";
    
    // ===========================================
    // Part 1: Recording Events
    // ===========================================
    std::cout << "=== Part 1: Recording Events ===\n\n";
    
    // Create a simulation profiler (for testing without GPU)
    auto profiler = createProfiler(PlatformType::Simulation);
    
    // Configure the profiler
    ProfilerConfig config;
    config.buffer_size = 100000;  // 100K events max
    
    profiler->initialize(config);
    
    // Set event rate for simulation (events per second)
    if (auto* sim = dynamic_cast<SimulationProfiler*>(profiler.get())) {
        sim->setEventRate(5000);  // 5000 events per second
    }
    
    // Get device info
    auto devices = profiler->getDeviceInfo();
    std::cout << "Detected devices:\n";
    for (const auto& dev : devices) {
        std::cout << "  [" << dev.device_id << "] " << dev.name 
                  << " (" << dev.vendor << ")\n";
    }
    std::cout << "\n";
    
    // Start capturing
    std::cout << "Starting capture for 2 seconds...\n";
    profiler->startCapture();
    
    // Wait for some events
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Stop capturing
    profiler->stopCapture();
    
    // Get events
    std::vector<TraceEvent> events;
    profiler->getEvents(events);
    
    std::cout << "Captured " << events.size() << " events\n";
    std::cout << "Dropped " << profiler->eventsDropped() << " events\n\n";
    
    // ===========================================
    // Part 2: Writing to File
    // ===========================================
    std::cout << "=== Part 2: Writing to File ===\n\n";
    
    const std::string filename = "example_trace.sbt";
    
    SBTWriter writer(filename);
    
    // Write metadata
    TraceMetadata metadata;
    metadata.application_name = "basic_example";
    metadata.command_line = "./basic_example";
    metadata.start_time = events.empty() ? 0 : events.front().timestamp;
    metadata.end_time = events.empty() ? 0 : events.back().timestamp;
    metadata.hostname = "localhost";
    metadata.process_id = 12345;
    metadata.devices = devices;
    
    writer.writeMetadata(metadata);
    writer.writeDeviceInfo(devices);
    writer.writeEvents(events);
    writer.finalize();
    
    std::cout << "Wrote " << writer.eventCount() << " events to " << filename << "\n\n";
    
    // ===========================================
    // Part 3: Reading and Analyzing
    // ===========================================
    std::cout << "=== Part 3: Reading and Analyzing ===\n\n";
    
    SBTReader reader(filename);
    
    if (!reader.isValid()) {
        std::cerr << "Error: Invalid file\n";
        return 1;
    }
    
    TraceRecord record;
    reader.readAll(record);
    
    std::cout << "Read " << record.size() << " events from file\n\n";
    
    // Analyze by event type
    std::cout << "Events by type:\n";
    auto kernels = record.filterByType(EventType::KernelLaunch);
    auto memcpy_h2d = record.filterByType(EventType::MemcpyH2D);
    auto memcpy_d2h = record.filterByType(EventType::MemcpyD2H);
    auto syncs = record.filterByType(EventType::StreamSync);
    
    std::cout << "  KernelLaunch:  " << kernels.size() << "\n";
    std::cout << "  MemcpyH2D:     " << memcpy_h2d.size() << "\n";
    std::cout << "  MemcpyD2H:     " << memcpy_d2h.size() << "\n";
    std::cout << "  StreamSync:    " << syncs.size() << "\n\n";
    
    // Analyze by stream
    std::cout << "Events by stream:\n";
    for (uint32_t stream = 0; stream < 4; ++stream) {
        auto stream_events = record.filterByStream(stream);
        std::cout << "  Stream " << stream << ": " << stream_events.size() << "\n";
    }
    std::cout << "\n";
    
    // Show first few events
    std::cout << "First 10 events:\n";
    size_t count = 0;
    for (const auto& event : record.events()) {
        if (count >= 10) break;
        
        std::cout << "  [" << std::setw(3) << count << "] "
                  << std::setw(16) << std::left << eventTypeToString(event.type)
                  << " | Stream " << event.stream_id
                  << " | " << event.name;
        
        if (event.kernel_params) {
            const auto& kp = event.kernel_params.value();
            std::cout << " <<<" << kp.grid_x << "," << kp.grid_y << "," << kp.grid_z
                      << ">>>, <<<" << kp.block_x << "," << kp.block_y << "," << kp.block_z << ">>>";
        }
        
        std::cout << "\n";
        count++;
    }
    
    std::cout << "\nExample complete!\n";
    
    return 0;
}
