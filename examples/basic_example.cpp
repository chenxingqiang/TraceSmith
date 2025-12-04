/**
 * TraceSmith Basic Example
 * 
 * This example demonstrates basic usage of TraceSmith:
 * - Platform detection
 * - Creating trace events manually
 * - Writing trace to SBT file
 * - Reading and analyzing trace
 * - Exporting to Perfetto format
 */

#include <tracesmith/tracesmith.hpp>
#include <tracesmith/state/perfetto_exporter.hpp>
#include <iostream>
#include <iomanip>
#include <random>

using namespace tracesmith;

// Helper to generate sample GPU events
std::vector<TraceEvent> generateSampleEvents(int count) {
    std::vector<TraceEvent> events;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> duration_dist(50000, 500000);  // 50-500 µs
    std::uniform_int_distribution<> stream_dist(0, 3);
    
    Timestamp base_time = getCurrentTimestamp();
    Timestamp current_time = base_time;
    
    for (int i = 0; i < count; ++i) {
        TraceEvent event;
        
        // Vary event types
        int type_selector = i % 5;
        switch (type_selector) {
            case 0:
            case 1:
            case 2:
                event.type = EventType::KernelLaunch;
                event.name = "compute_kernel_" + std::to_string(i);
                event.kernel_params = KernelParams{};
                event.kernel_params->grid_x = 256 + (i % 4) * 64;
                event.kernel_params->grid_y = 128;
                event.kernel_params->grid_z = 1;
                event.kernel_params->block_x = 32;
                event.kernel_params->block_y = 8;
                event.kernel_params->block_z = 1;
                event.kernel_params->shared_mem_bytes = (i % 3) * 1024;
                event.kernel_params->registers_per_thread = 32;
                break;
            case 3:
                event.type = EventType::MemcpyH2D;
                event.name = "upload_data_" + std::to_string(i);
                event.memory_params = MemoryParams{};
                event.memory_params->size_bytes = (1 + (i % 4)) * 1024 * 1024;
                event.memory_params->src_address = 0x7fff00000000 + i * 0x100000;
                event.memory_params->dst_address = 0xb0000000 + i * 0x100000;
                break;
            case 4:
                event.type = EventType::MemcpyD2H;
                event.name = "download_result_" + std::to_string(i);
                event.memory_params = MemoryParams{};
                event.memory_params->size_bytes = (1 + (i % 4)) * 1024 * 1024;
                event.memory_params->src_address = 0xb0000000 + i * 0x100000;
                event.memory_params->dst_address = 0x7fff00000000 + i * 0x100000;
                break;
        }
        
        event.timestamp = current_time;
        event.duration = duration_dist(gen);
        event.device_id = 0;
        event.stream_id = stream_dist(gen);
        event.correlation_id = i + 1;
        event.thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        
        events.push_back(event);
        current_time += event.duration + 10000;  // Gap between events
    }
    
    return events;
}

int main() {
    std::cout << "TraceSmith v" << getVersionString() << " Basic Example\n";
    std::cout << std::string(50, '=') << "\n\n";
    
    // ===========================================
    // Part 1: Platform Detection
    // ===========================================
    std::cout << "=== Part 1: Platform Detection ===\n\n";
    
    PlatformType detected = detectPlatform();
    std::cout << "Detected platform: ";
    switch (detected) {
        case PlatformType::CUDA:
            std::cout << "NVIDIA CUDA\n";
            std::cout << "  CUDA available: " << (isCUDAAvailable() ? "Yes" : "No") << "\n";
            if (isCUDAAvailable()) {
                std::cout << "  Device count: " << getCUDADeviceCount() << "\n";
                std::cout << "  Driver version: " << getCUDADriverVersion() << "\n";
            }
            break;
        case PlatformType::Metal:
            std::cout << "Apple Metal\n";
            std::cout << "  Metal available: " << (isMetalAvailable() ? "Yes" : "No") << "\n";
            if (isMetalAvailable()) {
                std::cout << "  Device count: " << getMetalDeviceCount() << "\n";
            }
            break;
        case PlatformType::ROCm:
            std::cout << "AMD ROCm\n";
            break;
        default:
            std::cout << "Unknown (no GPU detected)\n";
            break;
    }
    std::cout << "\n";
    
    // ===========================================
    // Part 2: Creating Events
    // ===========================================
    std::cout << "=== Part 2: Creating Events ===\n\n";
    
    std::vector<TraceEvent> events = generateSampleEvents(50);
    std::cout << "Generated " << events.size() << " sample events\n\n";
    
    // ===========================================
    // Part 3: Writing to SBT File
    // ===========================================
    std::cout << "=== Part 3: Writing to SBT File ===\n\n";
    
    const std::string filename = "basic_trace.sbt";
    
    SBTWriter writer(filename);
    
    // Write metadata
    TraceMetadata metadata;
    metadata.application_name = "basic_example";
    metadata.command_line = "./basic_example";
    metadata.start_time = events.empty() ? 0 : events.front().timestamp;
    metadata.end_time = events.empty() ? 0 : events.back().timestamp;
    metadata.hostname = "localhost";
    metadata.process_id = 12345;
    
    // Create device info
    std::vector<DeviceInfo> devices;
    DeviceInfo device;
    device.device_id = 0;
    device.name = "Example GPU";
    device.vendor = "TraceSmith";
    device.total_memory = 8ULL * 1024 * 1024 * 1024;  // 8GB
    device.multiprocessor_count = 80;
    device.clock_rate = 1700000;  // kHz
    device.compute_major = 8;
    device.compute_minor = 6;
    devices.push_back(device);
    
    metadata.devices = devices;
    
    writer.writeMetadata(metadata);
    writer.writeDeviceInfo(devices);
    writer.writeEvents(events);
    writer.finalize();
    
    std::cout << "Wrote " << writer.eventCount() << " events to " << filename << "\n\n";
    
    // ===========================================
    // Part 4: Reading and Analyzing
    // ===========================================
    std::cout << "=== Part 4: Reading and Analyzing ===\n\n";
    
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
    
    std::cout << "  KernelLaunch:  " << kernels.size() << "\n";
    std::cout << "  MemcpyH2D:     " << memcpy_h2d.size() << "\n";
    std::cout << "  MemcpyD2H:     " << memcpy_d2h.size() << "\n\n";
    
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
        
        if (event.memory_params) {
            const auto& mp = event.memory_params.value();
            std::cout << " [" << (mp.size_bytes / 1024 / 1024) << " MB]";
        }
        
        std::cout << "\n";
        count++;
    }
    
    // ===========================================
    // Part 5: Export to Perfetto
    // ===========================================
    std::cout << "\n=== Part 5: Export to Perfetto ===\n\n";
    
    PerfettoExporter exporter;
    exporter.setEnableGPUTracks(true);
    exporter.setEnableFlowEvents(true);
    
    const std::string perfetto_file = "basic_trace.json";
    if (exporter.exportToFile(events, perfetto_file)) {
        std::cout << "Exported to " << perfetto_file << "\n";
        std::cout << "View at: https://ui.perfetto.dev\n";
    }
    
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "Example complete!\n";
    
    return 0;
}
