/**
 * MetaX C500/C550 GPU Profiling Example
 * 
 * This example demonstrates how to use TraceSmith with MetaX GPUs
 * using the MCPTI (MACA Profiling Tools Interface).
 * 
 * Build with:
 *   cmake -DTRACESMITH_ENABLE_MACA=ON ..
 *   make metax_basic_example
 * 
 * Requirements:
 *   - MetaX GPU (C500, C550, etc.)
 *   - MACA SDK installed at /opt/maca or /opt/maca-3.0.0
 *   - MACA driver loaded
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <tracesmith/tracesmith.hpp>

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/mc_runtime_api.h>
#endif

using namespace tracesmith;

void printSeparator(const char* title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

void printDeviceInfo() {
    printSeparator("MetaX GPU Information");
    
#ifdef TRACESMITH_ENABLE_MACA
    int device_count = 0;
    mcError_t err = mcGetDeviceCount(&device_count);
    
    if (err != mcSuccess) {
        std::cerr << "Failed to get device count: " << mcGetErrorString(err) << std::endl;
        return;
    }
    
    std::cout << "Number of MetaX GPUs: " << device_count << "\n\n";
    
    for (int i = 0; i < device_count; ++i) {
        mcDeviceProp_t prop;
        if (mcGetDeviceProperties(&prop, i) == mcSuccess) {
            std::cout << "Device " << i << ": " << prop.name << "\n";
            std::cout << "  Compute Capability: " << prop.major << "." << prop.minor << "\n";
            std::cout << "  Total Memory: " << (prop.totalGlobalMem / (1024*1024*1024.0)) << " GB\n";
            std::cout << "  Multiprocessors: " << prop.multiProcessorCount << "\n";
            std::cout << "  Clock Rate: " << (prop.clockRate / 1000.0) << " MHz\n";
            std::cout << "  Memory Clock: " << (prop.memoryClockRate / 1000.0) << " MHz\n";
            std::cout << "  Memory Bus Width: " << prop.memoryBusWidth << " bit\n";
            std::cout << "  L2 Cache Size: " << (prop.l2CacheSize / 1024) << " KB\n";
            std::cout << "  Max Threads per Block: " << prop.maxThreadsPerBlock << "\n";
            std::cout << "  Warp Size: " << prop.warpSize << "\n";
            std::cout << "\n";
        }
    }
#else
    std::cout << "MACA support not enabled in this build.\n";
    std::cout << "Rebuild with -DTRACESMITH_ENABLE_MACA=ON\n";
#endif
}

void runGPUWorkload() {
    printSeparator("Running GPU Workload");
    
#ifdef TRACESMITH_ENABLE_MACA
    const size_t N = 1024 * 1024;  // 1M elements
    const size_t bytes = N * sizeof(float);
    
    std::cout << "Allocating " << (bytes / (1024*1024)) << " MB on device...\n";
    
    // Host memory
    float* h_data = new float[N];
    for (size_t i = 0; i < N; ++i) {
        h_data[i] = static_cast<float>(i);
    }
    
    // Device memory
    float* d_data = nullptr;
    mcError_t err = mcMalloc(&d_data, bytes);
    if (err != mcSuccess) {
        std::cerr << "mcMalloc failed: " << mcGetErrorString(err) << std::endl;
        delete[] h_data;
        return;
    }
    
    // Create stream
    mcStream_t stream;
    mcStreamCreate(&stream);
    
    std::cout << "Performing memory operations...\n";
    
    // H2D copy
    auto start = std::chrono::high_resolution_clock::now();
    mcMemcpyAsync(d_data, h_data, bytes, mcMemcpyHostToDevice, stream);
    mcStreamSynchronize(stream);
    auto h2d_time = std::chrono::high_resolution_clock::now() - start;
    
    std::cout << "  H2D Transfer: " << std::chrono::duration<double, std::milli>(h2d_time).count() << " ms\n";
    
    // Memset
    start = std::chrono::high_resolution_clock::now();
    mcMemsetAsync(d_data, 0, bytes, stream);
    mcStreamSynchronize(stream);
    auto memset_time = std::chrono::high_resolution_clock::now() - start;
    
    std::cout << "  Memset: " << std::chrono::duration<double, std::milli>(memset_time).count() << " ms\n";
    
    // D2H copy
    start = std::chrono::high_resolution_clock::now();
    mcMemcpyAsync(h_data, d_data, bytes, mcMemcpyDeviceToHost, stream);
    mcStreamSynchronize(stream);
    auto d2h_time = std::chrono::high_resolution_clock::now() - start;
    
    std::cout << "  D2H Transfer: " << std::chrono::duration<double, std::milli>(d2h_time).count() << " ms\n";
    
    // Cleanup
    mcStreamDestroy(stream);
    mcFree(d_data);
    delete[] h_data;
    
    std::cout << "GPU workload completed.\n";
#else
    std::cout << "MACA support not enabled. Skipping GPU workload.\n";
#endif
}

void profileWithMCPTI() {
    printSeparator("Profiling with MCPTI");
    
#ifdef TRACESMITH_ENABLE_MACA
    // Create MCPTI profiler
    auto profiler = createProfiler(PlatformType::MACA);
    
    if (!profiler) {
        std::cerr << "Failed to create MCPTI profiler.\n";
        std::cerr << "Make sure MACA driver is loaded and GPU is accessible.\n";
        return;
    }
    
    // Get device info
    auto devices = profiler->getDeviceInfo();
    std::cout << "Profiler detected " << devices.size() << " device(s)\n\n";
    
    for (const auto& dev : devices) {
        std::cout << "Device " << dev.device_id << ": " << dev.name << "\n";
        std::cout << "  Vendor: " << dev.vendor << "\n";
        std::cout << "  Memory: " << (dev.total_memory / (1024*1024*1024.0)) << " GB\n";
    }
    
    // Configure profiler
    ProfilerConfig config;
    config.capture_kernels = true;
    config.capture_memcpy = true;
    config.capture_memset = true;
    config.capture_sync = true;
    
    if (!profiler->initialize(config)) {
        std::cerr << "Failed to initialize profiler.\n";
        return;
    }
    
    std::cout << "\nStarting capture...\n";
    profiler->startCapture();
    
    // Run GPU workload
    runGPUWorkload();
    
    profiler->stopCapture();
    std::cout << "Capture stopped.\n";
    
    // Get captured events
    std::vector<TraceEvent> events;
    profiler->getEvents(events);
    
    std::cout << "\nCaptured " << events.size() << " events\n";
    std::cout << "  Total captured: " << profiler->eventsCaptured() << "\n";
    std::cout << "  Dropped: " << profiler->eventsDropped() << "\n";
    
    // Analyze events
    if (!events.empty()) {
        std::cout << "\nEvent breakdown:\n";
        
        int kernel_count = 0, memcpy_count = 0, memset_count = 0, sync_count = 0;
        
        for (const auto& event : events) {
            switch (event.type) {
                case EventType::KernelLaunch:
                case EventType::KernelComplete:
                    kernel_count++;
                    break;
                case EventType::MemcpyH2D:
                case EventType::MemcpyD2H:
                case EventType::MemcpyD2D:
                    memcpy_count++;
                    break;
                case EventType::MemsetDevice:
                    memset_count++;
                    break;
                case EventType::StreamSync:
                case EventType::DeviceSync:
                    sync_count++;
                    break;
                default:
                    break;
            }
        }
        
        std::cout << "  Kernel events: " << kernel_count << "\n";
        std::cout << "  Memcpy events: " << memcpy_count << "\n";
        std::cout << "  Memset events: " << memset_count << "\n";
        std::cout << "  Sync events: " << sync_count << "\n";
        
        // Save to file
        const std::string sbt_file = "metax_trace.sbt";
        SBTWriter writer(sbt_file);
        writer.writeEvents(events);
        writer.finalize();
        
        std::cout << "\nTrace saved to: " << sbt_file << "\n";
        
        // Export to Perfetto
        const std::string json_file = "metax_trace.json";
        PerfettoExporter exporter;
        exporter.exportEvents(events, json_file);
        
        std::cout << "Perfetto JSON exported to: " << json_file << "\n";
        std::cout << "View at: https://ui.perfetto.dev\n";
    }
    
    profiler->finalize();
#else
    std::cout << "MACA support not enabled in this build.\n";
#endif
}

int main() {
    std::cout << "TraceSmith MetaX GPU Profiling Example\n";
    std::cout << "Version: " << getVersionString() << "\n";
    std::cout << std::string(60, '=') << "\n";
    
    // Check MACA availability
    std::cout << "\nChecking MetaX GPU availability...\n";
    
    if (isMACAAvailable()) {
        std::cout << "MetaX GPU detected!\n";
        std::cout << "  Device count: " << getMACADeviceCount() << "\n";
        std::cout << "  Driver version: " << getMACADriverVersion() << "\n";
        
        printDeviceInfo();
        profileWithMCPTI();
    } else {
        std::cout << "No MetaX GPU detected.\n";
        std::cout << "\nPossible reasons:\n";
        std::cout << "  1. No MetaX GPU installed\n";
        std::cout << "  2. MACA driver not loaded\n";
        std::cout << "  3. TraceSmith built without MACA support\n";
        
#ifdef TRACESMITH_ENABLE_MACA
        std::cout << "\nMACA support is enabled in this build.\n";
        std::cout << "Check if MACA driver is loaded: mx-smi\n";
#else
        std::cout << "\nMACA support is NOT enabled in this build.\n";
        std::cout << "Rebuild with: cmake -DTRACESMITH_ENABLE_MACA=ON ..\n";
#endif
    }
    
    printSeparator("Example Complete");
    return 0;
}
