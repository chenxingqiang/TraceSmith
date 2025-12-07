/**
 * MetaX Cluster Profiling Example
 * 
 * Demonstrates multi-GPU and cluster profiling features on MetaX GPUs.
 * 
 * Features demonstrated:
 * - GPU topology discovery
 * - Multi-GPU profiling
 * - Time synchronization
 * - Event aggregation across GPUs
 * 
 * Build with:
 *   cmake -DTRACESMITH_ENABLE_MACA=ON ..
 *   make metax_cluster_example
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <tracesmith/tracesmith.hpp>

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/mc_runtime_api.h>
#endif

using namespace tracesmith;
using namespace tracesmith::cluster;

void printSeparator(const char* title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void testTopologyDiscovery() {
    printSeparator("GPU Topology Discovery");
    
    GPUTopology topology;
    
    if (!topology.discover()) {
        std::cout << "Failed to discover GPU topology.\n";
        std::cout << "This may be due to single GPU or limited driver support.\n";
        return;
    }
    
    auto info = topology.getTopology();
    
    std::cout << "Discovered " << info.gpu_count << " GPU(s)\n\n";
    
    // Print device info
    for (const auto& dev : info.devices) {
        std::cout << "GPU " << dev.gpu_id << ": " << dev.name << "\n";
        std::cout << "  PCI Bus: " << dev.pci_bus_id << "\n";
        std::cout << "  Memory: " << (dev.total_memory / (1024.0*1024*1024)) << " GB\n";
        std::cout << "  Compute: " << dev.compute_major << "." << dev.compute_minor << "\n";
        if (dev.has_nvlink) {
            std::cout << "  NVLink: " << dev.nvlink_count << " links\n";
        }
        if (dev.has_mxlink) {
            std::cout << "  MXLink: " << dev.mxlink_count << " links\n";
        }
        std::cout << "\n";
    }
    
    // Print links
    if (!info.links.empty()) {
        std::cout << "GPU Interconnects:\n";
        for (const auto& link : info.links) {
            std::cout << "  GPU" << link.gpu_a << " <-> GPU" << link.gpu_b 
                      << ": " << linkTypeToString(link.type);
            if (link.link_count > 1) {
                std::cout << " x" << link.link_count;
            }
            std::cout << " (" << link.bandwidth_gbps << " GB/s)\n";
        }
    }
    
    // Print ASCII representation
    std::cout << "\nTopology Matrix:\n";
    std::cout << topology.toASCII();
}

void testTimeSync() {
    printSeparator("Time Synchronization");
    
#ifdef TRACESMITH_ENABLE_MACA
    // Configure for MACA
    TimeSyncConfig config;
    config.method = TimeSyncMethod::MACA;
    
    TimeSync sync(config);
    
    if (!sync.initialize()) {
        std::cout << "Failed to initialize time synchronization.\n";
        std::cout << "Falling back to system clock...\n";
        
        config.method = TimeSyncMethod::SystemClock;
        TimeSync fallback_sync(config);
        fallback_sync.initialize();
        
        auto result = fallback_sync.synchronize();
        std::cout << "System clock sync: " << (result.success ? "OK" : "Failed") << "\n";
        return;
    }
    
    // Perform synchronization
    auto result = sync.synchronize();
    
    std::cout << "Sync method: " << timeSyncMethodToString(config.method) << "\n";
    std::cout << "Sync result: " << (result.success ? "Success" : "Failed") << "\n";
    
    if (result.success) {
        std::cout << "  Offset: " << result.offset_ns << " ns\n";
        std::cout << "  Uncertainty: " << result.uncertainty_ns << " ns\n";
    }
    
    // Test GPU correlation
    int deviceCount = 0;
    mcGetDeviceCount(&deviceCount);
    
    std::cout << "\nGPU Clock Correlation:\n";
    for (int i = 0; i < deviceCount; ++i) {
        if (sync.correlateGPUTimestamps(i)) {
            int64_t offset = sync.getGPUOffset(i);
            std::cout << "  GPU " << i << ": offset = " << offset << " ns\n";
        } else {
            std::cout << "  GPU " << i << ": correlation failed\n";
        }
    }
#else
    std::cout << "MACA not enabled. Skipping MACA-specific time sync test.\n";
    
    TimeSyncConfig config;
    config.method = TimeSyncMethod::SystemClock;
    
    TimeSync sync(config);
    sync.initialize();
    
    auto result = sync.synchronize();
    std::cout << "System clock sync: " << (result.success ? "OK" : "Failed") << "\n";
#endif
}

void testMultiGPUProfiler() {
    printSeparator("Multi-GPU Profiler");
    
#ifdef TRACESMITH_ENABLE_MACA
    int deviceCount = 0;
    mcError_t err = mcGetDeviceCount(&deviceCount);
    
    if (err != mcSuccess || deviceCount == 0) {
        std::cout << "No MetaX GPUs available.\n";
        return;
    }
    
    std::cout << "Found " << deviceCount << " MetaX GPU(s)\n\n";
    
    // Configure multi-GPU profiler
    MultiGPUConfig config;
    config.capture_topology = true;
    config.enable_nvlink_tracking = false;  // MetaX may use MXLink
    config.enable_peer_access_tracking = true;
    config.unified_timestamps = true;
    config.per_gpu_buffer_size = 10000;
    
    MultiGPUProfiler profiler(config);
    
    if (!profiler.initialize()) {
        std::cout << "Failed to initialize multi-GPU profiler.\n";
        return;
    }
    
    std::cout << "Profiler initialized for " << profiler.getActiveGPUs().size() << " GPU(s)\n";
    
    // Print device info
    auto devices = profiler.getAllDeviceInfo();
    for (const auto& dev : devices) {
        std::cout << "  GPU " << dev.device_id << ": " << dev.name << "\n";
    }
    
    // Start capture
    std::cout << "\nStarting capture...\n";
    profiler.startCapture();
    
    // Generate some GPU activity on all devices
    std::cout << "Generating GPU activity...\n";
    for (int i = 0; i < deviceCount; ++i) {
        mcSetDevice(i);
        
        // Allocate and transfer memory
        void* d_data = nullptr;
        size_t bytes = 16 * 1024 * 1024;  // 16 MB
        mcMalloc(&d_data, bytes);
        
        std::vector<float> h_data(bytes / sizeof(float), 1.0f);
        mcMemcpy(d_data, h_data.data(), bytes, mcMemcpyHostToDevice);
        mcMemset(d_data, 0, bytes);
        mcMemcpy(h_data.data(), d_data, bytes, mcMemcpyDeviceToHost);
        
        mcFree(d_data);
        mcDeviceSynchronize();
        
        std::cout << "  GPU " << i << ": completed\n";
    }
    
    // Stop capture
    profiler.stopCapture();
    std::cout << "Capture stopped.\n";
    
    // Get statistics
    auto stats = profiler.getStatistics();
    
    std::cout << "\nCapture Statistics:\n";
    std::cout << "  Total events: " << stats.total_events << "\n";
    std::cout << "  Events dropped: " << stats.total_dropped << "\n";
    std::cout << "  Capture duration: " << stats.capture_duration_ms << " ms\n";
    
    std::cout << "\nEvents per GPU:\n";
    for (const auto& [gpu_id, count] : stats.events_per_gpu) {
        std::cout << "  GPU " << gpu_id << ": " << count << " events\n";
    }
    
    // Get all events
    std::vector<TraceEvent> events;
    profiler.getEvents(events);
    
    if (!events.empty()) {
        std::cout << "\nFirst 10 events:\n";
        for (size_t i = 0; i < std::min(events.size(), size_t(10)); ++i) {
            const auto& e = events[i];
            std::cout << "  [" << i << "] " << eventTypeToString(e.type) 
                      << " | GPU " << e.device_id 
                      << " | " << e.name << "\n";
        }
        
        // Save to files
        SBTWriter writer("metax_cluster_trace.sbt");
        writer.writeEvents(events);
        writer.finalize();
        
        PerfettoExporter exporter;
        exporter.exportToFile(events, "metax_cluster_trace.json");
        
        std::cout << "\nTraces saved:\n";
        std::cout << "  - metax_cluster_trace.sbt\n";
        std::cout << "  - metax_cluster_trace.json\n";
    }
    
    profiler.finalize();
#else
    std::cout << "MACA not enabled. Multi-GPU profiler requires GPU support.\n";
#endif
}

void runFullTest() {
    printSeparator("Full Feature Test Summary");
    
    std::cout << "Testing all TraceSmith cluster features on MetaX...\n\n";
    
    // Test 1: Platform detection
    std::cout << "1. Platform Detection:\n";
    std::cout << "   MACA Available: " << (isMACAAvailable() ? "Yes" : "No") << "\n";
    if (isMACAAvailable()) {
        std::cout << "   Device Count: " << getMACADeviceCount() << "\n";
        std::cout << "   Driver Version: " << getMACADriverVersion() << "\n";
    }
    
    // Test 2: MCPTI availability
    std::cout << "\n2. Profiling Backend:\n";
#ifdef TRACESMITH_ENABLE_MACA
    auto profiler = createProfiler(PlatformType::MACA);
    if (profiler) {
        std::cout << "   MCPTI Profiler: Available\n";
        auto devices = profiler->getDeviceInfo();
        std::cout << "   Detected " << devices.size() << " device(s)\n";
    } else {
        std::cout << "   MCPTI Profiler: Not available\n";
    }
#else
    std::cout << "   MACA not enabled at compile time\n";
#endif
    
    // Test 3: Cluster features
    std::cout << "\n3. Cluster Features:\n";
    std::cout << "   MACA Mgmt Available: " << (isMACAMgmtAvailable() ? "Yes" : "No") << "\n";
    std::cout << "   MACA Version: " << getMACAVersion() << "\n";
    
    std::cout << "\n4. Running detailed tests...\n";
}

int main() {
    std::cout << "TraceSmith MetaX Cluster Profiling Example\n";
    std::cout << "Version: " << getVersionString() << "\n";
    std::cout << std::string(60, '=') << "\n";
    
    // Run full feature test
    runFullTest();
    
    // Run individual tests
    testTopologyDiscovery();
    testTimeSync();
    testMultiGPUProfiler();
    
    printSeparator("Example Complete");
    return 0;
}
