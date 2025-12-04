/**
 * Enhanced Perfetto Export Test
 * 
 * Tests the new features in Perfetto exporter:
 * - GPU-specific track naming
 * - Process/thread metadata
 * - Flow events for dependencies
 * - Rich event arguments (kernel params, memory params, perf counters)
 */

#include "tracesmith/state/perfetto_exporter.hpp"
#include "tracesmith/common/types.hpp"
#include <iostream>
#include <vector>
#include <memory>

using namespace tracesmith;

int main() {
    std::vector<TraceEvent> events;
    
    // Create sample events with rich metadata
    
    // 1. Memory allocation
    TraceEvent alloc_event;
    alloc_event.timestamp = 1000000;  // 1ms
    alloc_event.duration = 50000;     // 50µs
    alloc_event.type = EventType::MemAlloc;
    alloc_event.name = "cudaMalloc";
    alloc_event.device_id = 0;
    alloc_event.stream_id = 0;
    alloc_event.correlation_id = 1;
    
    MemoryParams mem_params;
    mem_params.size_bytes = 1024 * 1024;  // 1MB
    mem_params.dst_address = 0x100000000;
    alloc_event.memory_params = mem_params;
    
    events.push_back(alloc_event);
    
    // 2. Memcpy H2D
    TraceEvent memcpy_event;
    memcpy_event.timestamp = 1100000;  // 1.1ms
    memcpy_event.duration = 200000;    // 200µs
    memcpy_event.type = EventType::MemcpyH2D;
    memcpy_event.name = "cudaMemcpy";
    memcpy_event.device_id = 0;
    memcpy_event.stream_id = 1;
    memcpy_event.correlation_id = 1;  // Same correlation as alloc
    
    MemoryParams memcpy_params;
    memcpy_params.size_bytes = 1024 * 1024;
    memcpy_params.src_address = 0x7fff00000000;
    memcpy_params.dst_address = 0x100000000;
    memcpy_event.memory_params = memcpy_params;
    
    events.push_back(memcpy_event);
    
    // 3. Kernel launch
    TraceEvent kernel_event;
    kernel_event.timestamp = 1350000;  // 1.35ms
    kernel_event.duration = 500000;    // 500µs
    kernel_event.type = EventType::KernelLaunch;
    kernel_event.name = "vectorAdd";
    kernel_event.device_id = 0;
    kernel_event.stream_id = 1;
    kernel_event.correlation_id = 2;
    
    KernelParams kernel_params;
    kernel_params.grid_x = 256;
    kernel_params.grid_y = 1;
    kernel_params.grid_z = 1;
    kernel_params.block_x = 256;
    kernel_params.block_y = 1;
    kernel_params.block_z = 1;
    kernel_params.shared_mem_bytes = 0;
    kernel_params.registers_per_thread = 32;
    kernel_event.kernel_params = kernel_params;
    
    events.push_back(kernel_event);
    
    // 4. Kernel completion
    TraceEvent kernel_complete;
    kernel_complete.timestamp = 1850000;  // 1.85ms
    kernel_complete.duration = 0;
    kernel_complete.type = EventType::KernelComplete;
    kernel_complete.name = "vectorAdd";
    kernel_complete.device_id = 0;
    kernel_complete.stream_id = 1;
    kernel_complete.correlation_id = 2;  // Same correlation as kernel launch
    kernel_complete.kernel_params = kernel_params;  // Reuse params
    
    events.push_back(kernel_complete);
    
    // 5. Memcpy D2H
    TraceEvent memcpy_d2h;
    memcpy_d2h.timestamp = 1900000;  // 1.9ms
    memcpy_d2h.duration = 180000;    // 180µs
    memcpy_d2h.type = EventType::MemcpyD2H;
    memcpy_d2h.name = "cudaMemcpy";
    memcpy_d2h.device_id = 0;
    memcpy_d2h.stream_id = 1;
    memcpy_d2h.correlation_id = 3;
    
    MemoryParams memcpy_d2h_params;
    memcpy_d2h_params.size_bytes = 1024 * 1024;
    memcpy_d2h_params.src_address = 0x100000000;
    memcpy_d2h_params.dst_address = 0x7fff00000000;
    memcpy_d2h.memory_params = memcpy_d2h_params;
    
    events.push_back(memcpy_d2h);
    
    // 6. Stream synchronization
    TraceEvent sync_event;
    sync_event.timestamp = 2100000;  // 2.1ms
    sync_event.duration = 50000;     // 50µs
    sync_event.type = EventType::StreamSync;
    sync_event.name = "cudaStreamSynchronize";
    sync_event.device_id = 0;
    sync_event.stream_id = 1;
    sync_event.correlation_id = 3;  // Same correlation as D2H
    
    events.push_back(sync_event);
    
    // Export with enhanced features
    std::cout << "Exporting " << events.size() << " events with enhanced Perfetto format...\n";
    
    PerfettoExporter exporter;
    exporter.setEnableGPUTracks(true);
    exporter.setEnableFlowEvents(true);
    
    PerfettoMetadata metadata;
    metadata.process_name = "GPU Application";
    metadata.thread_name = "Main Thread";
    exporter.setMetadata(metadata);
    
    // Export to file
    std::string output_file = "perfetto_enhanced_trace.json";
    if (exporter.exportToFile(events, output_file)) {
        std::cout << "✓ Successfully exported to " << output_file << "\n";
        std::cout << "\nView in:\n";
        std::cout << "  - https://ui.perfetto.dev\n";
        std::cout << "  - chrome://tracing\n";
        std::cout << "\nFeatures included:\n";
        std::cout << "  ✓ GPU-specific tracks (Compute, Memory, Sync)\n";
        std::cout << "  ✓ Process/thread metadata\n";
        std::cout << "  ✓ Flow events for dependencies\n";
        std::cout << "  ✓ Kernel parameters (grid/block dimensions)\n";
        std::cout << "  ✓ Memory parameters (addresses, sizes)\n";
    } else {
        std::cerr << "✗ Failed to export trace\n";
        return 1;
    }
    
    // Also print a snippet to stdout
    std::string json_string = exporter.exportToString(events);
    std::cout << "\nFirst 500 characters of JSON:\n";
    std::cout << json_string.substr(0, 500) << "...\n";
    
    return 0;
}
