/**
 * TraceSmith MetaX GPU Profiling Example
 * 
 * Demonstrates how to use TraceSmith with MetaX GPUs (C500, C550, etc.)
 * using the MCPTI (MACA Profiling Tools Interface) backend.
 * 
 * This example shows:
 * - MetaX GPU detection and device info
 * - MCPTI profiler initialization
 * - Capturing GPU events (kernels, memory operations)
 * - Exporting traces to SBT and Perfetto formats
 * 
 * Build requirements:
 * - MetaX MACA SDK (typically in /opt/maca or /opt/maca-3.0.0)
 * - cmake -DTRACESMITH_ENABLE_MACA=ON ..
 * 
 * Run on MetaX system:
 * ./metax_example
 */

#include <tracesmith/tracesmith.hpp>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/mc_runtime_api.h>
#endif

using namespace tracesmith;

// Helper function to print separator
void printSeparator(const std::string& title = "") {
    std::cout << "\n";
    if (!title.empty()) {
        std::cout << "=== " << title << " ===" << std::endl;
    }
    std::cout << std::string(50, '-') << std::endl;
}

// Print device information
void printDeviceInfo(const DeviceInfo& info) {
    std::cout << "  Device ID:     " << info.device_id << std::endl;
    std::cout << "  Name:          " << info.name << std::endl;
    std::cout << "  Vendor:        " << info.vendor << std::endl;
    std::cout << "  Compute:       " << info.compute_major << "." << info.compute_minor << std::endl;
    std::cout << "  Memory:        " << (info.total_memory / (1024*1024)) << " MB" << std::endl;
    std::cout << "  CUs:           " << info.multiprocessor_count << std::endl;
    std::cout << "  Clock:         " << (info.clock_rate / 1000) << " MHz" << std::endl;
}

// Simple GPU workload using MACA runtime
#ifdef TRACESMITH_ENABLE_MACA
void runGPUWorkload() {
    const size_t SIZE = 1024 * 1024;  // 1M elements
    const size_t BYTES = SIZE * sizeof(float);
    
    // Allocate host memory
    float* h_data = new float[SIZE];
    for (size_t i = 0; i < SIZE; i++) {
        h_data[i] = static_cast<float>(i);
    }
    
    // Allocate device memory
    float* d_data = nullptr;
    mcError_t err = mcMalloc((void**)&d_data, BYTES);
    if (err != mcSuccess) {
        std::cerr << "mcMalloc failed: " << mcGetErrorString(err) << std::endl;
        delete[] h_data;
        return;
    }
    
    std::cout << "  Allocated " << (BYTES / (1024*1024)) << " MB on GPU" << std::endl;
    
    // Copy data to device (H2D)
    err = mcMemcpy(d_data, h_data, BYTES, mcMemcpyHostToDevice);
    if (err != mcSuccess) {
        std::cerr << "mcMemcpy H2D failed: " << mcGetErrorString(err) << std::endl;
    }
    std::cout << "  Copied data Host -> Device" << std::endl;
    
    // Memset on device
    err = mcMemset(d_data, 0, BYTES);
    if (err != mcSuccess) {
        std::cerr << "mcMemset failed: " << mcGetErrorString(err) << std::endl;
    }
    std::cout << "  Memset on device" << std::endl;
    
    // Copy data back (D2H)
    err = mcMemcpy(h_data, d_data, BYTES, mcMemcpyDeviceToHost);
    if (err != mcSuccess) {
        std::cerr << "mcMemcpy D2H failed: " << mcGetErrorString(err) << std::endl;
    }
    std::cout << "  Copied data Device -> Host" << std::endl;
    
    // Synchronize
    mcDeviceSynchronize();
    std::cout << "  Device synchronized" << std::endl;
    
    // Cleanup
    mcFree(d_data);
    delete[] h_data;
    
    std::cout << "  GPU workload completed" << std::endl;
}
#endif

int main() {
    std::cout << "TraceSmith MetaX GPU Profiling Example" << std::endl;
    std::cout << "Version: " << getVersionString() << std::endl;
    
    // =========================================================================
    // Part 1: Platform Detection
    // =========================================================================
    printSeparator("Part 1: Platform Detection");
    
#ifdef TRACESMITH_ENABLE_MACA
    std::cout << "MACA support: ENABLED" << std::endl;
    
    if (isMACAAvailable()) {
        std::cout << "MetaX GPU: DETECTED" << std::endl;
        std::cout << "Driver version: " << getMACADriverVersion() << std::endl;
        std::cout << "Device count: " << getMACADeviceCount() << std::endl;
    } else {
        std::cout << "MetaX GPU: NOT DETECTED" << std::endl;
        std::cout << "(Make sure MetaX driver is loaded)" << std::endl;
        return 1;
    }
#else
    std::cout << "MACA support: DISABLED" << std::endl;
    std::cout << "(Rebuild with -DTRACESMITH_ENABLE_MACA=ON)" << std::endl;
    return 1;
#endif
    
    // =========================================================================
    // Part 2: Create MCPTI Profiler
    // =========================================================================
    printSeparator("Part 2: Initialize MCPTI Profiler");
    
#ifdef TRACESMITH_ENABLE_MACA
    auto profiler = createProfiler(PlatformType::MACA);
    if (!profiler) {
        std::cerr << "Failed to create MCPTI profiler" << std::endl;
        return 1;
    }
    
    std::cout << "Platform: " << platformTypeToString(profiler->platformType()) << std::endl;
    
    // Get device info
    auto devices = profiler->getDeviceInfo();
    std::cout << "\nFound " << devices.size() << " MetaX GPU(s):" << std::endl;
    for (const auto& dev : devices) {
        printDeviceInfo(dev);
    }
    
    // Configure profiler
    ProfilerConfig config;
    config.capture_kernels = true;
    config.capture_memcpy = true;
    config.capture_memset = true;
    config.capture_sync = true;
    config.buffer_size = 1024 * 1024;  // 1M events buffer
    
    if (!profiler->initialize(config)) {
        std::cerr << "Failed to initialize profiler" << std::endl;
        return 1;
    }
    std::cout << "\nProfiler initialized successfully" << std::endl;
    
    // =========================================================================
    // Part 3: Capture GPU Events
    // =========================================================================
    printSeparator("Part 3: Capture GPU Events");
    
    std::cout << "Starting capture..." << std::endl;
    if (!profiler->startCapture()) {
        std::cerr << "Failed to start capture" << std::endl;
        return 1;
    }
    
    // Run GPU workload
    std::cout << "\nRunning GPU workload:" << std::endl;
    runGPUWorkload();
    
    // Stop capture
    std::cout << "\nStopping capture..." << std::endl;
    profiler->stopCapture();
    
    std::cout << "Events captured: " << profiler->eventsCaptured() << std::endl;
    std::cout << "Events dropped:  " << profiler->eventsDropped() << std::endl;
    
    // =========================================================================
    // Part 4: Retrieve and Analyze Events
    // =========================================================================
    printSeparator("Part 4: Analyze Captured Events");
    
    std::vector<TraceEvent> events;
    profiler->getEvents(events);
    
    std::cout << "Retrieved " << events.size() << " events" << std::endl;
    
    // Count by type
    std::map<EventType, int> type_counts;
    for (const auto& ev : events) {
        type_counts[ev.type]++;
    }
    
    std::cout << "\nEvents by type:" << std::endl;
    for (const auto& [type, count] : type_counts) {
        std::cout << "  " << std::setw(20) << std::left << eventTypeToString(type) 
                  << ": " << count << std::endl;
    }
    
    // Print first few events
    std::cout << "\nFirst 10 events:" << std::endl;
    for (size_t i = 0; i < std::min(events.size(), size_t(10)); i++) {
        const auto& ev = events[i];
        std::cout << "  [" << std::setw(3) << i << "] "
                  << std::setw(20) << std::left << eventTypeToString(ev.type)
                  << " | " << ev.name << std::endl;
    }
    
    // =========================================================================
    // Part 5: Export to Files
    // =========================================================================
    printSeparator("Part 5: Export Trace Files");
    
    // Export to SBT format
    {
        SBTWriter writer("metax_trace.sbt");
        writer.writeHeader();
        writer.writeEvents(events);
        writer.finalize();
        std::cout << "Saved to metax_trace.sbt" << std::endl;
    }
    
    // Export to Perfetto JSON
    {
        PerfettoExporter exporter;
        exporter.exportToFile(events, "metax_trace.json");
        std::cout << "Saved to metax_trace.json" << std::endl;
        std::cout << "View at: https://ui.perfetto.dev" << std::endl;
    }
    
    // Cleanup
    profiler->finalize();
#endif
    
    printSeparator("Example Complete");
    return 0;
}
