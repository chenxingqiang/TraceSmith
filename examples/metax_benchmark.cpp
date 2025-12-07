/**
 * TraceSmith MetaX GPU Benchmark Example
 * 
 * A comprehensive benchmark demonstrating MCPTI profiling capabilities
 * on MetaX GPUs with various workloads and profiling scenarios.
 * 
 * This example shows:
 * - High-throughput event capture
 * - Multi-stream profiling
 * - Kernel execution statistics
 * - Memory bandwidth analysis
 * - Timeline generation
 * 
 * Build: cmake -DTRACESMITH_ENABLE_MACA=ON ..
 * Run: ./metax_benchmark [iterations]
 */

#include <tracesmith/tracesmith.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <map>
#include <numeric>
#include <algorithm>

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/mc_runtime_api.h>
#endif

using namespace tracesmith;
using Clock = std::chrono::high_resolution_clock;

// Benchmark configuration
struct BenchmarkConfig {
    int iterations = 100;
    size_t data_size = 4 * 1024 * 1024;  // 4 MB
    int num_streams = 4;
    bool verbose = false;
};

// Benchmark results
struct BenchmarkResults {
    double total_time_ms = 0;
    size_t total_events = 0;
    size_t kernel_events = 0;
    size_t memcpy_events = 0;
    size_t memset_events = 0;
    size_t sync_events = 0;
    double events_per_second = 0;
    double avg_kernel_time_us = 0;
    double total_bandwidth_gbps = 0;
};

void printSeparator(const std::string& title = "") {
    std::cout << "\n";
    if (!title.empty()) {
        std::cout << "=== " << title << " ===" << std::endl;
    }
    std::cout << std::string(60, '-') << std::endl;
}

#ifdef TRACESMITH_ENABLE_MACA
// Multi-stream workload
void runMultiStreamWorkload(const BenchmarkConfig& config) {
    std::vector<mcStream_t> streams(config.num_streams);
    std::vector<float*> d_buffers(config.num_streams);
    
    // Create streams and allocate memory
    for (int i = 0; i < config.num_streams; i++) {
        mcStreamCreate(&streams[i]);
        mcMalloc((void**)&d_buffers[i], config.data_size);
    }
    
    // Host buffer
    std::vector<float> h_buffer(config.data_size / sizeof(float));
    
    // Run iterations across streams
    for (int iter = 0; iter < config.iterations; iter++) {
        int stream_idx = iter % config.num_streams;
        
        // H2D transfer
        mcMemcpyAsync(d_buffers[stream_idx], h_buffer.data(), 
                      config.data_size, mcMemcpyHostToDevice, 
                      streams[stream_idx]);
        
        // Memset
        mcMemsetAsync(d_buffers[stream_idx], 0, config.data_size, 
                      streams[stream_idx]);
        
        // D2H transfer
        mcMemcpyAsync(h_buffer.data(), d_buffers[stream_idx],
                      config.data_size, mcMemcpyDeviceToHost,
                      streams[stream_idx]);
    }
    
    // Synchronize all streams
    for (int i = 0; i < config.num_streams; i++) {
        mcStreamSynchronize(streams[i]);
    }
    
    // Cleanup
    for (int i = 0; i < config.num_streams; i++) {
        mcFree(d_buffers[i]);
        mcStreamDestroy(streams[i]);
    }
}

// Memory bandwidth test
void runBandwidthTest(const BenchmarkConfig& config) {
    float* d_src = nullptr;
    float* d_dst = nullptr;
    
    mcMalloc((void**)&d_src, config.data_size);
    mcMalloc((void**)&d_dst, config.data_size);
    
    // Device to device copies
    for (int i = 0; i < config.iterations / 2; i++) {
        mcMemcpy(d_dst, d_src, config.data_size, mcMemcpyDeviceToDevice);
    }
    
    mcDeviceSynchronize();
    
    mcFree(d_src);
    mcFree(d_dst);
}

// Analyze captured events
BenchmarkResults analyzeEvents(const std::vector<TraceEvent>& events, 
                               double elapsed_ms) {
    BenchmarkResults results;
    results.total_events = events.size();
    results.total_time_ms = elapsed_ms;
    
    std::vector<uint64_t> kernel_durations;
    size_t total_bytes_transferred = 0;
    
    for (const auto& ev : events) {
        switch (ev.type) {
            case EventType::KernelLaunch:
            case EventType::KernelComplete:
                results.kernel_events++;
                if (ev.duration > 0) {
                    kernel_durations.push_back(ev.duration);
                }
                break;
                
            case EventType::MemcpyH2D:
            case EventType::MemcpyD2H:
            case EventType::MemcpyD2D:
                results.memcpy_events++;
                // Extract bytes from metadata
                if (ev.metadata.count("bytes")) {
                    total_bytes_transferred += std::stoull(ev.metadata.at("bytes"));
                }
                break;
                
            case EventType::MemsetDevice:
                results.memset_events++;
                if (ev.metadata.count("bytes")) {
                    total_bytes_transferred += std::stoull(ev.metadata.at("bytes"));
                }
                break;
                
            case EventType::StreamSync:
            case EventType::DeviceSync:
                results.sync_events++;
                break;
                
            default:
                break;
        }
    }
    
    // Calculate statistics
    results.events_per_second = (results.total_events / elapsed_ms) * 1000.0;
    
    if (!kernel_durations.empty()) {
        uint64_t total_duration = std::accumulate(
            kernel_durations.begin(), kernel_durations.end(), 0ULL);
        results.avg_kernel_time_us = (total_duration / kernel_durations.size()) / 1000.0;
    }
    
    // Bandwidth in GB/s
    results.total_bandwidth_gbps = (total_bytes_transferred / elapsed_ms) / 1e6;
    
    return results;
}

void printResults(const BenchmarkResults& results) {
    std::cout << std::fixed << std::setprecision(2);
    
    std::cout << "\nBenchmark Results:" << std::endl;
    std::cout << "  Total time:        " << results.total_time_ms << " ms" << std::endl;
    std::cout << "  Total events:      " << results.total_events << std::endl;
    std::cout << "  Events/second:     " << results.events_per_second << std::endl;
    
    std::cout << "\nEvent Breakdown:" << std::endl;
    std::cout << "  Kernel events:     " << results.kernel_events << std::endl;
    std::cout << "  Memcpy events:     " << results.memcpy_events << std::endl;
    std::cout << "  Memset events:     " << results.memset_events << std::endl;
    std::cout << "  Sync events:       " << results.sync_events << std::endl;
    
    if (results.avg_kernel_time_us > 0) {
        std::cout << "\nKernel Statistics:" << std::endl;
        std::cout << "  Avg kernel time:   " << results.avg_kernel_time_us << " µs" << std::endl;
    }
    
    if (results.total_bandwidth_gbps > 0) {
        std::cout << "\nMemory Statistics:" << std::endl;
        std::cout << "  Effective BW:      " << results.total_bandwidth_gbps << " GB/s" << std::endl;
    }
}
#endif

int main(int argc, char* argv[]) {
    std::cout << "TraceSmith MetaX GPU Benchmark" << std::endl;
    std::cout << "Version: " << getVersionString() << std::endl;
    
    BenchmarkConfig config;
    if (argc > 1) {
        config.iterations = std::atoi(argv[1]);
    }
    
    printSeparator("Configuration");
    std::cout << "Iterations:  " << config.iterations << std::endl;
    std::cout << "Data size:   " << (config.data_size / (1024*1024)) << " MB" << std::endl;
    std::cout << "Streams:     " << config.num_streams << std::endl;
    
#ifdef TRACESMITH_ENABLE_MACA
    // Check MACA availability
    printSeparator("Platform Detection");
    
    if (!isMACAAvailable()) {
        std::cerr << "MetaX GPU not detected" << std::endl;
        return 1;
    }
    
    std::cout << "MetaX GPU detected" << std::endl;
    std::cout << "Device count: " << getMACADeviceCount() << std::endl;
    
    // Create profiler
    printSeparator("Initialize Profiler");
    
    auto profiler = createProfiler(PlatformType::MACA);
    if (!profiler) {
        std::cerr << "Failed to create profiler" << std::endl;
        return 1;
    }
    
    // Print device info
    auto devices = profiler->getDeviceInfo();
    for (const auto& dev : devices) {
        std::cout << "Device " << dev.device_id << ": " << dev.name << std::endl;
        std::cout << "  Memory: " << (dev.total_memory / (1024*1024*1024)) << " GB" << std::endl;
        std::cout << "  CUs: " << dev.multiprocessor_count << std::endl;
    }
    
    // Configure and initialize
    ProfilerConfig prof_config;
    prof_config.capture_kernels = true;
    prof_config.capture_memcpy = true;
    prof_config.capture_memset = true;
    prof_config.capture_sync = true;
    prof_config.buffer_size = 10 * 1024 * 1024;  // 10M events
    
    if (!profiler->initialize(prof_config)) {
        std::cerr << "Failed to initialize profiler" << std::endl;
        return 1;
    }
    
    // =========================================================================
    // Benchmark 1: Multi-Stream Workload
    // =========================================================================
    printSeparator("Benchmark 1: Multi-Stream Workload");
    
    profiler->startCapture();
    auto start = Clock::now();
    
    runMultiStreamWorkload(config);
    
    auto end = Clock::now();
    profiler->stopCapture();
    
    double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    std::vector<TraceEvent> events;
    profiler->getEvents(events);
    
    auto results1 = analyzeEvents(events, elapsed_ms);
    printResults(results1);
    
    // Export benchmark 1 results
    {
        SBTWriter writer("metax_multistream.sbt");
        writer.writeHeader();
        writer.writeEvents(events);
        writer.finalize();
        std::cout << "\nSaved to metax_multistream.sbt" << std::endl;
    }
    
    // =========================================================================
    // Benchmark 2: Memory Bandwidth
    // =========================================================================
    printSeparator("Benchmark 2: Memory Bandwidth Test");
    
    events.clear();
    profiler->startCapture();
    start = Clock::now();
    
    runBandwidthTest(config);
    
    end = Clock::now();
    profiler->stopCapture();
    
    elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    profiler->getEvents(events);
    
    auto results2 = analyzeEvents(events, elapsed_ms);
    printResults(results2);
    
    // Export benchmark 2 results
    {
        PerfettoExporter exporter;
        exporter.exportToFile(events, "metax_bandwidth.json");
        std::cout << "\nSaved to metax_bandwidth.json" << std::endl;
    }
    
    // =========================================================================
    // Summary
    // =========================================================================
    printSeparator("Summary");
    
    std::cout << "Total events captured: " 
              << (results1.total_events + results2.total_events) << std::endl;
    std::cout << "Output files:" << std::endl;
    std::cout << "  - metax_multistream.sbt" << std::endl;
    std::cout << "  - metax_bandwidth.json" << std::endl;
    std::cout << "\nView traces at: https://ui.perfetto.dev" << std::endl;
    
    profiler->finalize();
    
#else
    std::cerr << "MACA support not enabled" << std::endl;
    std::cerr << "Rebuild with -DTRACESMITH_ENABLE_MACA=ON" << std::endl;
    return 1;
#endif
    
    printSeparator("Benchmark Complete");
    return 0;
}
