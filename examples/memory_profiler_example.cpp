/**
 * Memory Profiler Example - REAL GPU Memory Profiling
 * 
 * Demonstrates GPU Memory Profiling with REAL CUDA allocations:
 * - Tracking real cudaMalloc/cudaFree operations
 * - Memory snapshots of actual GPU memory
 * - Leak detection for real allocations
 * - Memory usage reports
 * 
 * Requirements:
 * - NVIDIA GPU with CUDA support
 * - Build with -DTRACESMITH_ENABLE_CUDA=ON
 * 
 */

#include "tracesmith/capture/memory_profiler.hpp"
#include "tracesmith/common/types.hpp"
#include "tracesmith/capture/profiler.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>

#ifdef TRACESMITH_ENABLE_CUDA
#include <cuda_runtime.h>
#endif

using namespace tracesmith;

#ifdef TRACESMITH_ENABLE_CUDA

// Structure to track real GPU allocations
struct GPUAllocation {
    void* ptr;
    size_t size;
    std::string tag;
};

// Real GPU memory operations with profiler tracking
class RealGPUMemoryTest {
public:
    RealGPUMemoryTest(MemoryProfiler& profiler) : profiler_(profiler) {}
    
    // Allocate real GPU memory and track it
    void* allocate(size_t size, const std::string& tag = "") {
        void* ptr = nullptr;
        cudaError_t err = cudaMalloc(&ptr, size);
        if (err != cudaSuccess) {
            std::cerr << "cudaMalloc failed: " << cudaGetErrorString(err) << "\n";
            return nullptr;
        }
        
        // Record in profiler
        profiler_.recordAlloc(reinterpret_cast<uint64_t>(ptr), size, 0);
        
        // Track locally
        allocations_.push_back({ptr, size, tag});
        total_allocated_ += size;
        
        return ptr;
    }
    
    // Free real GPU memory and track it
    void free(void* ptr) {
        if (!ptr) return;
        
        // Find and remove from tracking
        for (auto it = allocations_.begin(); it != allocations_.end(); ++it) {
            if (it->ptr == ptr) {
                total_freed_ += it->size;
                allocations_.erase(it);
                break;
            }
        }
        
        // Record in profiler
        profiler_.recordFree(reinterpret_cast<uint64_t>(ptr));
        
        // Actually free the memory
        cudaFree(ptr);
    }
    
    // Get all current allocations
    const std::vector<GPUAllocation>& getAllocations() const { return allocations_; }
    size_t getTotalAllocated() const { return total_allocated_; }
    size_t getTotalFreed() const { return total_freed_; }
    
    // Free all remaining allocations
    void freeAll() {
        while (!allocations_.empty()) {
            free(allocations_.back().ptr);
        }
    }
    
private:
    MemoryProfiler& profiler_;
    std::vector<GPUAllocation> allocations_;
    size_t total_allocated_ = 0;
    size_t total_freed_ = 0;
};

// Run real GPU memory operations
void runRealGPUMemoryTest(MemoryProfiler& profiler) {
    RealGPUMemoryTest gpu_mem(profiler);
    
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "REAL GPU MEMORY OPERATIONS\n";
    std::cout << std::string(60, '=') << "\n";
    
    // Phase 1: Allocate model parameters (weights)
    std::cout << "\n--- Phase 1: Allocating Model Parameters ---\n";
    std::vector<void*> parameters;
    const size_t param_sizes[] = {4 * 1024 * 1024, 8 * 1024 * 1024, 2 * 1024 * 1024, 
                                   16 * 1024 * 1024, 1 * 1024 * 1024};  // Various sizes
    for (size_t i = 0; i < 5; ++i) {
        void* ptr = gpu_mem.allocate(param_sizes[i], "parameter");
        if (ptr) {
            parameters.push_back(ptr);
            std::cout << "  ✓ Parameter " << i << ": " << ptr 
                      << " (" << (param_sizes[i] / 1024 / 1024) << " MB)\n";
        }
    }
    
    // Phase 2: Allocate activations (forward pass)
    std::cout << "\n--- Phase 2: Allocating Activations (Forward Pass) ---\n";
    std::vector<void*> activations;
    for (int i = 0; i < 8; ++i) {
        size_t size = (i + 1) * 2 * 1024 * 1024;  // 2MB, 4MB, 6MB, ...
        void* ptr = gpu_mem.allocate(size, "activation");
        if (ptr) {
            activations.push_back(ptr);
            std::cout << "  ✓ Activation " << i << ": " << ptr 
                      << " (" << (size / 1024 / 1024) << " MB)\n";
        }
    }
    
    // Take snapshot after forward pass
    std::cout << "\n--- Memory Snapshot (After Forward Pass) ---\n";
    auto snapshot1 = profiler.takeSnapshot();
    std::cout << "  Live allocations: " << snapshot1.live_allocations << "\n";
    std::cout << "  Live bytes: " << (snapshot1.live_bytes / 1024 / 1024) << " MB\n";
    
    // Phase 3: Allocate gradients (backward pass)
    std::cout << "\n--- Phase 3: Allocating Gradients (Backward Pass) ---\n";
    std::vector<void*> gradients;
    for (int i = 0; i < 5; ++i) {
        size_t size = param_sizes[i];  // Same size as parameters
        void* ptr = gpu_mem.allocate(size, "gradient");
        if (ptr) {
            gradients.push_back(ptr);
            std::cout << "  ✓ Gradient " << i << ": " << ptr 
                      << " (" << (size / 1024 / 1024) << " MB)\n";
        }
    }
    
    // Phase 4: Free activations (after backward pass)
    std::cout << "\n--- Phase 4: Freeing Activations ---\n";
    for (size_t i = 0; i < activations.size(); ++i) {
        std::cout << "  ✗ Free activation " << i << ": " << activations[i] << "\n";
        gpu_mem.free(activations[i]);
    }
    activations.clear();
    
    // Phase 5: Allocate workspace for optimizer
    std::cout << "\n--- Phase 5: Allocating Optimizer Workspace ---\n";
    std::vector<void*> workspace;
    for (int i = 0; i < 3; ++i) {
        size_t size = 32 * 1024 * 1024;  // 32MB each
        void* ptr = gpu_mem.allocate(size, "workspace");
        if (ptr) {
            workspace.push_back(ptr);
            std::cout << "  ✓ Workspace " << i << ": " << ptr << " (32 MB)\n";
        }
    }
    
    // Take snapshot after all operations
    std::cout << "\n--- Memory Snapshot (After All Operations) ---\n";
    auto snapshot2 = profiler.takeSnapshot();
    std::cout << "  Live allocations: " << snapshot2.live_allocations << "\n";
    std::cout << "  Live bytes: " << (snapshot2.live_bytes / 1024 / 1024) << " MB\n";
    std::cout << "  Peak bytes: " << (snapshot2.peak_bytes / 1024 / 1024) << " MB\n";
    
    // Free gradients
    std::cout << "\n--- Phase 6: Freeing Gradients ---\n";
    for (size_t i = 0; i < gradients.size(); ++i) {
        std::cout << "  ✗ Free gradient " << i << ": " << gradients[i] << "\n";
        gpu_mem.free(gradients[i]);
    }
    gradients.clear();
    
    // Free workspace
    std::cout << "\n--- Phase 7: Freeing Workspace ---\n";
    for (size_t i = 0; i < workspace.size(); ++i) {
        std::cout << "  ✗ Free workspace " << i << ": " << workspace[i] << "\n";
        gpu_mem.free(workspace[i]);
    }
    workspace.clear();
    
    // Intentionally leave parameters allocated to demonstrate leak detection
    std::cout << "\n--- Parameters NOT freed (for leak detection demo) ---\n";
    std::cout << "  " << parameters.size() << " parameter allocations intentionally leaked\n";
    
    // Summary
    std::cout << "\n--- GPU Memory Test Summary ---\n";
    std::cout << "  Total allocated: " << (gpu_mem.getTotalAllocated() / 1024 / 1024) << " MB\n";
    std::cout << "  Total freed: " << (gpu_mem.getTotalFreed() / 1024 / 1024) << " MB\n";
    std::cout << "  Remaining: " << ((gpu_mem.getTotalAllocated() - gpu_mem.getTotalFreed()) / 1024 / 1024) << " MB\n";
    
    // Note: parameters are intentionally not freed to demonstrate leak detection
    // In production code, you would call: gpu_mem.freeAll();
}

#endif // TRACESMITH_ENABLE_CUDA

int main() {
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  TraceSmith Memory Profiler Example                       ║\n";
    std::cout << "║  REAL GPU Memory Profiling                 ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n\n";
    
#ifndef TRACESMITH_ENABLE_CUDA
    std::cerr << "ERROR: This example requires CUDA support.\n";
    std::cerr << "Please rebuild with: cmake .. -DTRACESMITH_ENABLE_CUDA=ON\n";
    return 1;
#else
    // Check CUDA availability
    if (!isCUDAAvailable()) {
        std::cerr << "ERROR: No CUDA-capable GPU found.\n";
        return 1;
    }
    
    int device_count = getCUDADeviceCount();
    std::cout << "CUDA available: " << device_count << " device(s)\n";
    
    // Query GPU memory info
    size_t free_mem, total_mem;
    cudaMemGetInfo(&free_mem, &total_mem);
    std::cout << "GPU Memory: " << (free_mem / 1024 / 1024) << " MB free / " 
              << (total_mem / 1024 / 1024) << " MB total\n";
    
    // Create and configure memory profiler
    MemoryProfiler::Config config;
    config.snapshot_interval_ms = 100;
    config.leak_threshold_ns = 1000000000ULL;  // 1 second (short for demo)
    config.track_call_stacks = false;
    config.detect_double_free = true;
    
    MemoryProfiler profiler(config);
    
    std::cout << "\nMemory Profiler Configuration:\n";
    std::cout << "  Snapshot interval: " << config.snapshot_interval_ms << " ms\n";
    std::cout << "  Leak threshold: " << (config.leak_threshold_ns / 1000000) << " ms\n";
    std::cout << "  Detect double free: " << (config.detect_double_free ? "Yes" : "No") << "\n";
    
    // Run REAL GPU memory operations
    auto start = std::chrono::high_resolution_clock::now();
    runRealGPUMemoryTest(profiler);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Generate memory report
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "MEMORY USAGE REPORT (Real GPU Data)\n";
    std::cout << std::string(60, '=') << "\n";
    
    auto report = profiler.generateReport();
    
    std::cout << "\nSummary:\n";
    std::cout << "  Total allocated:     " << std::setw(12) << report.total_bytes_allocated 
              << " bytes (" << (report.total_bytes_allocated / 1024 / 1024) << " MB)\n";
    std::cout << "  Total freed:         " << std::setw(12) << report.total_bytes_freed 
              << " bytes (" << (report.total_bytes_freed / 1024 / 1024) << " MB)\n";
    std::cout << "  Current usage:       " << std::setw(12) << report.current_memory_usage 
              << " bytes (" << (report.current_memory_usage / 1024 / 1024) << " MB)\n";
    std::cout << "  Peak usage:          " << std::setw(12) << report.peak_memory_usage 
              << " bytes (" << (report.peak_memory_usage / 1024 / 1024) << " MB)\n";
    std::cout << "  Allocation count:    " << std::setw(12) << report.total_allocations << "\n";
    std::cout << "  Deallocation count:  " << std::setw(12) << report.total_frees << "\n";
    std::cout << "  Min alloc size:      " << std::setw(12) << report.min_allocation_size 
              << " bytes (" << (report.min_allocation_size / 1024 / 1024) << " MB)\n";
    std::cout << "  Max alloc size:      " << std::setw(12) << report.max_allocation_size 
              << " bytes (" << (report.max_allocation_size / 1024 / 1024) << " MB)\n";
    
    // Leak detection
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Leak Detection (Real GPU Allocations)\n";
    std::cout << std::string(60, '-') << "\n";
    
    const auto& leaks = report.potential_leaks;
    if (leaks.empty()) {
        std::cout << "  ✓ No memory leaks detected\n";
    } else {
        std::cout << "  ⚠ Potential leaks detected: " << leaks.size() << "\n";
        size_t total_leaked = 0;
        for (const auto& leak : leaks) {
            std::cout << "    - 0x" << std::hex << leak.ptr << std::dec 
                      << " (" << (leak.size / 1024 / 1024) << " MB)\n";
            total_leaked += leak.size;
        }
        std::cout << "  Total leaked: " << (total_leaked / 1024 / 1024) << " MB\n";
    }
    
    // Final GPU memory status
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Final GPU Memory Status\n";
    std::cout << std::string(60, '-') << "\n";
    
    cudaMemGetInfo(&free_mem, &total_mem);
    std::cout << "  GPU Memory: " << (free_mem / 1024 / 1024) << " MB free / " 
              << (total_mem / 1024 / 1024) << " MB total\n";
    
    // Clean up leaked memory (in real code, you'd want to fix the leaks)
    std::cout << "\n--- Cleaning up leaked memory ---\n";
    cudaDeviceReset();
    
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Memory Profiler Example Complete!\n";
    std::cout << "  Test duration: " << duration.count() << " ms\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    std::cout << "Features Demonstrated (REAL GPU):\n";
    std::cout << "  ✓ Real cudaMalloc/cudaFree tracking\n";
    std::cout << "  ✓ GPU memory snapshots\n";
    std::cout << "  ✓ Peak usage tracking\n";
    std::cout << "  ✓ Leak detection for real allocations\n";
    std::cout << "  ✓ Detailed memory reports\n";
    
    return 0;
#endif
}
