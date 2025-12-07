/**
 * MetaX GPU Benchmark Example
 * 
 * This example benchmarks memory bandwidth and profiling overhead
 * on MetaX GPUs (C500, C550) using MCPTI.
 * 
 * Build with:
 *   cmake -DTRACESMITH_ENABLE_MACA=ON ..
 *   make metax_benchmark
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <tracesmith/tracesmith.hpp>

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/mc_runtime_api.h>
#endif

using namespace tracesmith;
using Clock = std::chrono::high_resolution_clock;

struct BenchmarkResult {
    std::string name;
    double mean_ms;
    double stddev_ms;
    double bandwidth_gbps;
    size_t bytes;
};

void printResult(const BenchmarkResult& result) {
    std::cout << std::left << std::setw(30) << result.name
              << std::right << std::setw(12) << std::fixed << std::setprecision(3) 
              << result.mean_ms << " ms"
              << std::setw(12) << result.stddev_ms << " ms";
    
    if (result.bandwidth_gbps > 0) {
        std::cout << std::setw(12) << result.bandwidth_gbps << " GB/s";
    }
    std::cout << "\n";
}

class MetaXBenchmark {
public:
    MetaXBenchmark(int device_id = 0) : device_id_(device_id) {
#ifdef TRACESMITH_ENABLE_MACA
        mcSetDevice(device_id);
        mcGetDeviceProperties(&prop_, device_id);
        
        std::cout << "Benchmark Device: " << prop_.name << "\n";
        std::cout << "  Total Memory: " << (prop_.totalGlobalMem / (1024.0*1024*1024)) << " GB\n";
        std::cout << "  Memory Clock: " << (prop_.memoryClockRate / 1000.0) << " MHz\n";
        std::cout << "  Memory Bus: " << prop_.memoryBusWidth << " bit\n";
        
        // Theoretical peak bandwidth
        double peak_bandwidth = 2.0 * prop_.memoryClockRate * 1000.0 * (prop_.memoryBusWidth / 8.0) / 1e9;
        std::cout << "  Peak Bandwidth: " << std::fixed << std::setprecision(1) << peak_bandwidth << " GB/s\n\n";
#endif
    }
    
    ~MetaXBenchmark() = default;
    
    BenchmarkResult benchmarkH2D(size_t bytes, int iterations = 10) {
        BenchmarkResult result;
        result.name = "Host to Device";
        result.bytes = bytes;
        
#ifdef TRACESMITH_ENABLE_MACA
        std::vector<double> times;
        
        // Allocate
        float* h_data = new float[bytes / sizeof(float)];
        float* d_data = nullptr;
        mcMalloc(&d_data, bytes);
        
        // Warm up
        mcMemcpy(d_data, h_data, bytes, mcMemcpyHostToDevice);
        mcDeviceSynchronize();
        
        // Benchmark
        for (int i = 0; i < iterations; ++i) {
            auto start = Clock::now();
            mcMemcpy(d_data, h_data, bytes, mcMemcpyHostToDevice);
            mcDeviceSynchronize();
            auto end = Clock::now();
            
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            times.push_back(ms);
        }
        
        // Calculate statistics
        double sum = std::accumulate(times.begin(), times.end(), 0.0);
        result.mean_ms = sum / times.size();
        
        double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
        result.stddev_ms = std::sqrt(sq_sum / times.size() - result.mean_ms * result.mean_ms);
        
        result.bandwidth_gbps = (bytes / 1e9) / (result.mean_ms / 1000.0);
        
        // Cleanup
        mcFree(d_data);
        delete[] h_data;
#endif
        return result;
    }
    
    BenchmarkResult benchmarkD2H(size_t bytes, int iterations = 10) {
        BenchmarkResult result;
        result.name = "Device to Host";
        result.bytes = bytes;
        
#ifdef TRACESMITH_ENABLE_MACA
        std::vector<double> times;
        
        float* h_data = new float[bytes / sizeof(float)];
        float* d_data = nullptr;
        mcMalloc(&d_data, bytes);
        
        mcMemcpy(d_data, h_data, bytes, mcMemcpyHostToDevice);
        mcDeviceSynchronize();
        
        for (int i = 0; i < iterations; ++i) {
            auto start = Clock::now();
            mcMemcpy(h_data, d_data, bytes, mcMemcpyDeviceToHost);
            mcDeviceSynchronize();
            auto end = Clock::now();
            
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            times.push_back(ms);
        }
        
        double sum = std::accumulate(times.begin(), times.end(), 0.0);
        result.mean_ms = sum / times.size();
        
        double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
        result.stddev_ms = std::sqrt(sq_sum / times.size() - result.mean_ms * result.mean_ms);
        
        result.bandwidth_gbps = (bytes / 1e9) / (result.mean_ms / 1000.0);
        
        mcFree(d_data);
        delete[] h_data;
#endif
        return result;
    }
    
    BenchmarkResult benchmarkD2D(size_t bytes, int iterations = 10) {
        BenchmarkResult result;
        result.name = "Device to Device";
        result.bytes = bytes;
        
#ifdef TRACESMITH_ENABLE_MACA
        std::vector<double> times;
        
        float* d_src = nullptr;
        float* d_dst = nullptr;
        mcMalloc(&d_src, bytes);
        mcMalloc(&d_dst, bytes);
        mcMemset(d_src, 0, bytes);
        mcDeviceSynchronize();
        
        // Warm up
        mcMemcpy(d_dst, d_src, bytes, mcMemcpyDeviceToDevice);
        mcDeviceSynchronize();
        
        for (int i = 0; i < iterations; ++i) {
            auto start = Clock::now();
            mcMemcpy(d_dst, d_src, bytes, mcMemcpyDeviceToDevice);
            mcDeviceSynchronize();
            auto end = Clock::now();
            
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            times.push_back(ms);
        }
        
        double sum = std::accumulate(times.begin(), times.end(), 0.0);
        result.mean_ms = sum / times.size();
        
        double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
        result.stddev_ms = std::sqrt(sq_sum / times.size() - result.mean_ms * result.mean_ms);
        
        result.bandwidth_gbps = (bytes / 1e9) / (result.mean_ms / 1000.0);
        
        mcFree(d_src);
        mcFree(d_dst);
#endif
        return result;
    }
    
    void benchmarkProfilingOverhead(int iterations = 100) {
        std::cout << "\n=== Profiling Overhead Test ===\n\n";
        
#ifdef TRACESMITH_ENABLE_MACA
        const size_t bytes = 64 * 1024 * 1024;  // 64 MB
        
        float* h_data = new float[bytes / sizeof(float)];
        float* d_data = nullptr;
        mcMalloc(&d_data, bytes);
        
        // Without profiling
        std::vector<double> times_noprofile;
        for (int i = 0; i < iterations; ++i) {
            auto start = Clock::now();
            mcMemcpy(d_data, h_data, bytes, mcMemcpyHostToDevice);
            mcDeviceSynchronize();
            auto end = Clock::now();
            times_noprofile.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
        
        double mean_noprofile = std::accumulate(times_noprofile.begin(), times_noprofile.end(), 0.0) / times_noprofile.size();
        
        // With MCPTI profiling
        auto profiler = createProfiler(PlatformType::MACA);
        if (!profiler) {
            std::cerr << "Failed to create profiler\n";
            mcFree(d_data);
            delete[] h_data;
            return;
        }
        
        ProfilerConfig config;
        config.capture_memcpy = true;
        profiler->initialize(config);
        profiler->startCapture();
        
        std::vector<double> times_profile;
        for (int i = 0; i < iterations; ++i) {
            auto start = Clock::now();
            mcMemcpy(d_data, h_data, bytes, mcMemcpyHostToDevice);
            mcDeviceSynchronize();
            auto end = Clock::now();
            times_profile.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        }
        
        profiler->stopCapture();
        
        double mean_profile = std::accumulate(times_profile.begin(), times_profile.end(), 0.0) / times_profile.size();
        
        double overhead = ((mean_profile - mean_noprofile) / mean_noprofile) * 100.0;
        
        std::cout << "Without profiling: " << std::fixed << std::setprecision(3) << mean_noprofile << " ms\n";
        std::cout << "With MCPTI profiling: " << mean_profile << " ms\n";
        std::cout << "Overhead: " << std::setprecision(1) << overhead << "%\n";
        
        std::vector<TraceEvent> events;
        profiler->getEvents(events);
        std::cout << "Events captured: " << events.size() << "\n";
        
        profiler->finalize();
        mcFree(d_data);
        delete[] h_data;
#else
        std::cout << "MACA support not enabled.\n";
#endif
    }
    
private:
    int device_id_;
#ifdef TRACESMITH_ENABLE_MACA
    mcDeviceProp_t prop_;
#endif
};

int main() {
    std::cout << "TraceSmith MetaX GPU Benchmark\n";
    std::cout << "Version: " << getVersionString() << "\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    if (!isMACAAvailable()) {
        std::cout << "No MetaX GPU detected.\n";
        std::cout << "Build with -DTRACESMITH_ENABLE_MACA=ON and ensure driver is loaded.\n";
        return 1;
    }
    
    std::cout << "MetaX GPUs detected: " << getMACADeviceCount() << "\n\n";
    
    MetaXBenchmark benchmark(0);
    
    // Memory bandwidth tests
    std::cout << "\n=== Memory Bandwidth Tests ===\n\n";
    
    std::vector<size_t> sizes = {
        1 * 1024 * 1024,    // 1 MB
        16 * 1024 * 1024,   // 16 MB
        64 * 1024 * 1024,   // 64 MB
        256 * 1024 * 1024,  // 256 MB
    };
    
    std::cout << std::left << std::setw(30) << "Test"
              << std::right << std::setw(15) << "Mean"
              << std::setw(15) << "StdDev"
              << std::setw(15) << "Bandwidth" << "\n";
    std::cout << std::string(75, '-') << "\n";
    
    for (size_t size : sizes) {
        std::cout << "\nSize: " << (size / (1024*1024)) << " MB\n";
        
        printResult(benchmark.benchmarkH2D(size));
        printResult(benchmark.benchmarkD2H(size));
        printResult(benchmark.benchmarkD2D(size));
    }
    
    // Profiling overhead test
    benchmark.benchmarkProfilingOverhead();
    
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Benchmark complete!\n";
    
    return 0;
}
