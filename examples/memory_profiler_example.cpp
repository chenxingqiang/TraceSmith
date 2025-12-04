/**
 * Memory Profiler Example
 * 
 * Demonstrates GPU Memory Profiling features:
 * - Tracking memory allocations and deallocations
 * - Memory snapshots
 * - Leak detection
 * - Memory usage reports
 * - Category-based memory tracking
 */

#include "tracesmith/memory_profiler.hpp"
#include "tracesmith/types.hpp"
#include <iostream>
#include <iomanip>
#include <random>
#include <thread>

using namespace tracesmith;

// Simulate GPU memory operations
void simulateMemoryOperations(MemoryProfiler& profiler) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> addr_dist(0x10000000, 0x7FFFFFFF);
    std::uniform_int_distribution<size_t> size_dist(1024, 16 * 1024 * 1024);  // 1KB - 16MB
    
    std::vector<uint64_t> allocated_addresses;
    
    // Phase 1: Allocate model parameters
    std::cout << "\n--- Phase 1: Allocating Model Parameters ---\n";
    for (int i = 0; i < 5; ++i) {
        uint64_t addr = addr_dist(gen);
        size_t size = size_dist(gen);
        profiler.recordAllocation(addr, size, MemoryCategory::Parameter, 0);
        allocated_addresses.push_back(addr);
        std::cout << "  Allocated parameter: 0x" << std::hex << addr << std::dec 
                  << " (" << (size / 1024) << " KB)\n";
    }
    
    // Phase 2: Allocate activations during forward pass
    std::cout << "\n--- Phase 2: Allocating Activations ---\n";
    for (int i = 0; i < 8; ++i) {
        uint64_t addr = addr_dist(gen);
        size_t size = size_dist(gen);
        profiler.recordAllocation(addr, size, MemoryCategory::Activation, 0);
        allocated_addresses.push_back(addr);
        std::cout << "  Allocated activation: 0x" << std::hex << addr << std::dec 
                  << " (" << (size / 1024) << " KB)\n";
    }
    
    // Take a snapshot
    std::cout << "\n--- Taking Memory Snapshot (after forward pass) ---\n";
    auto snapshot = profiler.takeSnapshot();
    
    // Phase 3: Allocate gradients during backward pass
    std::cout << "\n--- Phase 3: Allocating Gradients ---\n";
    for (int i = 0; i < 5; ++i) {
        uint64_t addr = addr_dist(gen);
        size_t size = size_dist(gen);
        profiler.recordAllocation(addr, size, MemoryCategory::Gradient, 0);
        allocated_addresses.push_back(addr);
        std::cout << "  Allocated gradient: 0x" << std::hex << addr << std::dec 
                  << " (" << (size / 1024) << " KB)\n";
    }
    
    // Phase 4: Free some temporary allocations (simulating cleanup)
    std::cout << "\n--- Phase 4: Freeing Temporary Allocations ---\n";
    std::shuffle(allocated_addresses.begin(), allocated_addresses.end(), gen);
    
    size_t to_free = allocated_addresses.size() / 2;
    for (size_t i = 0; i < to_free; ++i) {
        uint64_t addr = allocated_addresses[i];
        profiler.recordDeallocation(addr);
        std::cout << "  Freed: 0x" << std::hex << addr << std::dec << "\n";
    }
    
    // Phase 5: Allocate workspace memory
    std::cout << "\n--- Phase 5: Allocating Workspace ---\n";
    for (int i = 0; i < 3; ++i) {
        uint64_t addr = addr_dist(gen);
        size_t size = 32 * 1024 * 1024;  // 32MB workspace
        profiler.recordAllocation(addr, size, MemoryCategory::Workspace, 0);
        std::cout << "  Allocated workspace: 0x" << std::hex << addr << std::dec 
                  << " (" << (size / 1024 / 1024) << " MB)\n";
    }
}

int main() {
    std::cout << "TraceSmith Memory Profiler Example\n";
    std::cout << "===================================\n";
    
    // Create and configure memory profiler
    MemoryProfiler::Config config;
    config.track_allocations = true;
    config.track_peak = true;
    config.detect_leaks = true;
    config.alignment = 256;  // Typical GPU alignment
    
    MemoryProfiler profiler(config);
    
    std::cout << "\nMemory Profiler Configuration:\n";
    std::cout << "  Track allocations: " << (config.track_allocations ? "Yes" : "No") << "\n";
    std::cout << "  Track peak: " << (config.track_peak ? "Yes" : "No") << "\n";
    std::cout << "  Detect leaks: " << (config.detect_leaks ? "Yes" : "No") << "\n";
    std::cout << "  Alignment: " << config.alignment << " bytes\n";
    
    // Run simulated memory operations
    simulateMemoryOperations(profiler);
    
    // Generate memory report
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Memory Usage Report\n";
    std::cout << std::string(60, '=') << "\n";
    
    auto report = profiler.generateReport();
    
    std::cout << "\nSummary:\n";
    std::cout << "  Total allocated:     " << std::setw(12) << report.total_allocated 
              << " bytes (" << (report.total_allocated / 1024 / 1024) << " MB)\n";
    std::cout << "  Total freed:         " << std::setw(12) << report.total_freed 
              << " bytes (" << (report.total_freed / 1024 / 1024) << " MB)\n";
    std::cout << "  Current usage:       " << std::setw(12) << report.current_usage 
              << " bytes (" << (report.current_usage / 1024 / 1024) << " MB)\n";
    std::cout << "  Peak usage:          " << std::setw(12) << report.peak_usage 
              << " bytes (" << (report.peak_usage / 1024 / 1024) << " MB)\n";
    std::cout << "  Allocation count:    " << std::setw(12) << report.allocation_count << "\n";
    std::cout << "  Deallocation count:  " << std::setw(12) << report.deallocation_count << "\n";
    std::cout << "  Live allocations:    " << std::setw(12) << report.live_allocations << "\n";
    
    // Category breakdown
    std::cout << "\nUsage by Category:\n";
    for (const auto& [category, usage] : report.usage_by_category) {
        std::string cat_name;
        switch (category) {
            case MemoryCategory::Parameter: cat_name = "Parameter"; break;
            case MemoryCategory::Activation: cat_name = "Activation"; break;
            case MemoryCategory::Gradient: cat_name = "Gradient"; break;
            case MemoryCategory::Workspace: cat_name = "Workspace"; break;
            case MemoryCategory::Optimizer: cat_name = "Optimizer"; break;
            default: cat_name = "Other"; break;
        }
        std::cout << "  " << std::setw(12) << std::left << cat_name << ": " 
                  << std::setw(12) << std::right << usage 
                  << " bytes (" << (usage / 1024 / 1024) << " MB)\n";
    }
    
    // Leak detection
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Leak Detection\n";
    std::cout << std::string(60, '-') << "\n";
    
    auto leaks = profiler.detectLeaks();
    if (leaks.empty()) {
        std::cout << "  ✓ No memory leaks detected\n";
    } else {
        std::cout << "  ⚠ Potential leaks detected: " << leaks.size() << "\n";
        size_t shown = 0;
        for (const auto& leak : leaks) {
            if (shown++ >= 5) {
                std::cout << "  ... and " << (leaks.size() - 5) << " more\n";
                break;
            }
            std::cout << "    - 0x" << std::hex << leak.address << std::dec 
                      << " (" << (leak.size / 1024) << " KB)";
            std::string cat_name;
            switch (leak.category) {
                case MemoryCategory::Parameter: cat_name = "Parameter"; break;
                case MemoryCategory::Activation: cat_name = "Activation"; break;
                case MemoryCategory::Gradient: cat_name = "Gradient"; break;
                case MemoryCategory::Workspace: cat_name = "Workspace"; break;
                default: cat_name = "Other"; break;
            }
            std::cout << " [" << cat_name << "]\n";
        }
    }
    
    // Take final snapshot
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Final Memory Snapshot\n";
    std::cout << std::string(60, '-') << "\n";
    
    auto final_snapshot = profiler.takeSnapshot();
    std::cout << "  Timestamp: " << final_snapshot.timestamp << "\n";
    std::cout << "  Total size: " << (final_snapshot.total_size / 1024 / 1024) << " MB\n";
    std::cout << "  Allocation count: " << final_snapshot.allocations.size() << "\n";
    
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "Memory Profiler Example Complete!\n";
    std::cout << std::string(60, '=') << "\n\n";
    
    std::cout << "Features Demonstrated:\n";
    std::cout << "  ✓ Tracking allocations by category\n";
    std::cout << "  ✓ Memory snapshots\n";
    std::cout << "  ✓ Peak usage tracking\n";
    std::cout << "  ✓ Leak detection\n";
    std::cout << "  ✓ Detailed memory reports\n";
    
    return 0;
}

