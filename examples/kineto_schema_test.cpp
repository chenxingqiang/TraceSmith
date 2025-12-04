/**
 * Kineto Schema Example
 * 
 * Demonstrates TraceSmith's Kineto-compatible event schema features:
 * - thread_id tracking
 * - Flexible metadata (operator names, shapes, FLOPS)
 * - Structured flow information
 * - PyTorch profiler compatibility
 */

#include "tracesmith/common/types.hpp"
#include "tracesmith/state/perfetto_exporter.hpp"
#include <iostream>
#include <thread>

using namespace tracesmith;

// Helper to get current thread ID as uint32_t
uint32_t getCurrentThreadId() {
    auto tid = std::this_thread::get_id();
    return std::hash<std::thread::id>{}(tid);
}

int main() {
    std::vector<TraceEvent> events;
    
    std::cout << "Creating trace events with Kineto schema features...\n\n";
    
    // 1. Memory allocation with thread tracking
    TraceEvent alloc_event(EventType::MemAlloc);
    alloc_event.timestamp = 1000000;
    alloc_event.duration = 50000;
    alloc_event.name = "cudaMalloc";
    alloc_event.device_id = 0;
    alloc_event.stream_id = 0;
    alloc_event.correlation_id = 1;
    
    // NEW: Add thread ID
    alloc_event.thread_id = getCurrentThreadId();
    
    // NEW: Add metadata
    alloc_event.metadata["allocator"] = "cuda_allocator";
    alloc_event.metadata["size"] = "4194304";  // 4MB
    
    MemoryParams mem_params;
    mem_params.size_bytes = 4 * 1024 * 1024;
    mem_params.dst_address = 0x7f0000000000;
    alloc_event.memory_params = mem_params;
    
    events.push_back(alloc_event);
    std::cout << "✓ Created memory allocation event with thread_id=" << alloc_event.thread_id << "\n";
    std::cout << "  Metadata: allocator=" << alloc_event.metadata["allocator"] << "\n";
    
    // 2. Forward pass kernel with operator metadata
    TraceEvent fwd_kernel(EventType::KernelLaunch);
    fwd_kernel.timestamp = 2000000;
    fwd_kernel.duration = 800000;
    fwd_kernel.name = "vectorAdd_forward";
    fwd_kernel.device_id = 0;
    fwd_kernel.stream_id = 1;
    fwd_kernel.correlation_id = 2;
    fwd_kernel.thread_id = getCurrentThreadId();
    
    // NEW: Add PyTorch-style metadata
    fwd_kernel.metadata["operator"] = "aten::add";
    fwd_kernel.metadata["input_shape"] = "[1024, 1024]";
    fwd_kernel.metadata["output_shape"] = "[1024, 1024]";
    fwd_kernel.metadata["flops"] = "2097152";  // 2M FLOPs
    fwd_kernel.metadata["is_training"] = "true";
    
    // NEW: Set flow information (forward pass start)
    fwd_kernel.flow_info = FlowInfo(42, FlowType::FwdBwd, true);
    
    KernelParams kernel_params;
    kernel_params.grid_x = 256;
    kernel_params.grid_y = 1;
    kernel_params.grid_z = 1;
    kernel_params.block_x = 256;
    kernel_params.block_y = 1;
    kernel_params.block_z = 1;
    kernel_params.shared_mem_bytes = 0;
    kernel_params.registers_per_thread = 32;
    fwd_kernel.kernel_params = kernel_params;
    
    events.push_back(fwd_kernel);
    std::cout << "✓ Created forward kernel with operator metadata\n";
    std::cout << "  Operator: " << fwd_kernel.metadata["operator"] << "\n";
    std::cout << "  Shape: " << fwd_kernel.metadata["input_shape"] << "\n";
    std::cout << "  Flow: id=" << fwd_kernel.flow_info.id << ", type=FwdBwd, start=true\n";
    
    // 3. Backward pass kernel (correlated with forward)
    TraceEvent bwd_kernel(EventType::KernelLaunch);
    bwd_kernel.timestamp = 3000000;
    bwd_kernel.duration = 950000;
    bwd_kernel.name = "vectorAdd_backward";
    bwd_kernel.device_id = 0;
    bwd_kernel.stream_id = 1;
    bwd_kernel.correlation_id = 3;
    bwd_kernel.thread_id = getCurrentThreadId();
    
    // Metadata for backward pass
    bwd_kernel.metadata["operator"] = "aten::add_backward";
    bwd_kernel.metadata["input_shape"] = "[1024, 1024]";
    bwd_kernel.metadata["gradient_shape"] = "[1024, 1024]";
    bwd_kernel.metadata["flops"] = "2097152";
    
    // NEW: Set flow information (forward pass end, backward start)
    bwd_kernel.flow_info = FlowInfo(42, FlowType::FwdBwd, false);  // end of flow
    
    bwd_kernel.kernel_params = kernel_params;
    
    events.push_back(bwd_kernel);
    std::cout << "✓ Created backward kernel correlated with forward\n";
    std::cout << "  Operator: " << bwd_kernel.metadata["operator"] << "\n";
    std::cout << "  Flow: id=" << bwd_kernel.flow_info.id << ", type=FwdBwd, start=false\n";
    
    // 4. Async CPU-GPU operation
    TraceEvent async_memcpy(EventType::MemcpyH2D);
    async_memcpy.timestamp = 4000000;
    async_memcpy.duration = 300000;
    async_memcpy.name = "cudaMemcpyAsync";
    async_memcpy.device_id = 0;
    async_memcpy.stream_id = 2;
    async_memcpy.correlation_id = 4;
    async_memcpy.thread_id = getCurrentThreadId();
    
    // Metadata for async operation
    async_memcpy.metadata["transfer_type"] = "host_to_device";
    async_memcpy.metadata["async"] = "true";
    async_memcpy.metadata["stream_name"] = "data_stream";
    
    // NEW: Async CPU-GPU flow
    async_memcpy.flow_info = FlowInfo(100, FlowType::AsyncCpuGpu, true);
    
    MemoryParams memcpy_params;
    memcpy_params.size_bytes = 1024 * 1024;
    memcpy_params.src_address = 0x7fff00000000;
    memcpy_params.dst_address = 0x7f0000000000;
    async_memcpy.memory_params = memcpy_params;
    
    events.push_back(async_memcpy);
    std::cout << "✓ Created async memory copy\n";
    std::cout << "  Flow: id=" << async_memcpy.flow_info.id << ", type=AsyncCpuGpu\n";
    
    // 5. Custom event with flexible metadata
    TraceEvent custom_event(EventType::Marker);
    custom_event.timestamp = 5000000;
    custom_event.name = "training_step_end";
    custom_event.device_id = 0;
    custom_event.stream_id = 1;
    custom_event.thread_id = getCurrentThreadId();
    
    // Rich metadata for custom events
    custom_event.metadata["step"] = "42";
    custom_event.metadata["loss"] = "0.123";
    custom_event.metadata["learning_rate"] = "0.001";
    custom_event.metadata["batch_size"] = "128";
    custom_event.metadata["accuracy"] = "0.95";
    
    events.push_back(custom_event);
    std::cout << "✓ Created custom marker with training metrics\n";
    std::cout << "  Metadata entries: " << custom_event.metadata.size() << "\n";
    
    // Export to Perfetto format
    std::cout << "\n" << "Exporting to Perfetto JSON...\n";
    
    PerfettoExporter exporter;
    exporter.setEnableGPUTracks(true);
    exporter.setEnableFlowEvents(true);
    
    std::string output_file = "kineto_schema_trace.json";
    if (exporter.exportToFile(events, output_file)) {
        std::cout << "✓ Successfully exported to " << output_file << "\n";
        std::cout << "\n" << "Kineto Schema Features Demonstrated:\n";
        std::cout << "  ✓ thread_id tracking (all events)\n";
        std::cout << "  ✓ Flexible metadata (operator names, shapes, metrics)\n";
        std::cout << "  ✓ Structured flow information (FwdBwd, AsyncCpuGpu)\n";
        std::cout << "  ✓ PyTorch profiler compatibility\n";
        std::cout << "\n" << "View trace in:\n";
        std::cout << "  - Perfetto UI: https://ui.perfetto.dev\n";
        std::cout << "  - Chrome Tracing: chrome://tracing\n";
        std::cout << "\n" << "The trace includes:\n";
        std::cout << "  - " << events.size() << " events with rich metadata\n";
        std::cout << "  - Thread ID tracking for debugging\n";
        std::cout << "  - Forward-backward correlation\n";
        std::cout << "  - Async operation flows\n";
        std::cout << "  - Training metrics\n";
    } else {
        std::cerr << "✗ Failed to export trace\n";
        return 1;
    }
    
    // Print a sample of the JSON to show the new fields
    std::string json_sample = exporter.exportToString(events);
    std::cout << "\n" << "Sample JSON (first 800 chars):\n";
    std::cout << json_sample.substr(0, 800) << "...\n";
    
    std::cout << "\n" << "✅ Kineto schema implementation complete!\n";
    
    return 0;
}
