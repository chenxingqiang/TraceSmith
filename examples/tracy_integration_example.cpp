/**
 * Tracy Integration Example for TraceSmith
 * 
 * This example demonstrates the bidirectional integration between
 * Tracy Profiler and TraceSmith, showcasing:
 * 
 * 1. Using Tracy zones alongside TraceSmith profiling
 * 2. Exporting TraceSmith events to Tracy
 * 3. Memory profiling with both systems
 * 4. Frame marking for game/real-time applications
 * 5. Counter/plot visualization
 * 
 * Build with:
 *   cmake .. -DTRACESMITH_ENABLE_TRACY=ON
 *   make tracy_integration_example
 * 
 * Run with Tracy server:
 *   1. Start Tracy server (tracy-profiler or tracy-gui)
 *   2. Run ./bin/tracy_integration_example
 *   3. View real-time profiling in Tracy
 */

#include <tracesmith/tracesmith.hpp>
#include <tracesmith/tracy/tracy_client.hpp>
#include <tracesmith/tracy/tracy_exporter.hpp>
#include <tracesmith/tracy/tracy_gpu_context.hpp>

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>
#include <random>

using namespace tracesmith;

// Namespace alias to avoid ambiguity with tracy:: namespace from Tracy library
namespace ts_tracy = tracesmith::tracy;

// =============================================================================
// Simulated Workloads
// =============================================================================

/**
 * Simulate a GPU kernel with variable execution time
 */
void simulateKernel(const std::string& name, int complexity) {
    TracySmithZoneScopedC("simulateKernel", ts_tracy::colors::KernelLaunch);
    
    // Simulate work proportional to complexity
    volatile double result = 0;
    for (int i = 0; i < complexity * 10000; ++i) {
        result += std::sin(static_cast<double>(i)) * std::cos(static_cast<double>(i));
    }
    
    // Small sleep to simulate GPU execution time
    std::this_thread::sleep_for(std::chrono::microseconds(complexity * 100));
}

/**
 * Simulate memory operations
 */
void simulateMemoryOperations(ts_tracy::TracyExporter& exporter) {
    TracySmithZoneScopedC("simulateMemoryOperations", ts_tracy::colors::MemAlloc);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(1024, 1024 * 1024);
    
    // Simulate allocations
    std::vector<std::pair<uint64_t, size_t>> allocations;
    
    for (int i = 0; i < 10; ++i) {
        size_t size = size_dist(gen);
        uint64_t ptr = reinterpret_cast<uint64_t>(new char[size]);
        allocations.push_back({ptr, size});
        
        // Create memory event
        MemoryEvent alloc_event;
        alloc_event.ptr = ptr;
        alloc_event.bytes = size;
        alloc_event.is_allocation = true;
        alloc_event.allocator_name = "SimulatedGPU";
        alloc_event.timestamp = getCurrentTimestamp();
        
        exporter.emitMemoryEvent(alloc_event);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Simulate frees
    for (const auto& [ptr, size] : allocations) {
        MemoryEvent free_event;
        free_event.ptr = ptr;
        free_event.bytes = size;
        free_event.is_allocation = false;
        free_event.allocator_name = "SimulatedGPU";
        free_event.timestamp = getCurrentTimestamp();
        
        exporter.emitMemoryEvent(free_event);
        
        delete[] reinterpret_cast<char*>(ptr);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/**
 * Simulate a frame with multiple kernel executions
 */
void simulateFrame(ts_tracy::TracyExporter& exporter, int frame_number) {
    std::string frame_name = "Frame_" + std::to_string(frame_number);
    exporter.markFrameStart(frame_name.c_str());
    
    TracySmithZoneScopedC("simulateFrame", ts_tracy::colors::Default);
    
    // Generate random kernel workloads
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> kernel_count(3, 8);
    std::uniform_int_distribution<> complexity(1, 10);
    
    int num_kernels = kernel_count(gen);
    
    // Simulate kernel launches
    for (int i = 0; i < num_kernels; ++i) {
        std::string kernel_name = "kernel_" + std::to_string(i);
        int kernel_complexity = complexity(gen);
        
        // Create TraceSmith event
        TraceEvent event;
        event.type = EventType::KernelLaunch;
        event.name = kernel_name;
        event.timestamp = getCurrentTimestamp();
        event.device_id = 0;
        event.stream_id = i % 4;  // Simulate multiple streams
        
        // Add kernel parameters
        KernelParams params;
        params.grid_x = 256;
        params.grid_y = 256;
        params.grid_z = 1;
        params.block_x = 16;
        params.block_y = 16;
        params.block_z = 1;
        event.kernel_params = params;
        
        // Execute simulated kernel
        auto start = getCurrentTimestamp();
        simulateKernel(kernel_name, kernel_complexity);
        auto end = getCurrentTimestamp();
        
        event.duration = end - start;
        
        // Export to Tracy
        exporter.emitEvent(event);
        
        // Emit kernel duration as counter
        double duration_ms = static_cast<double>(event.duration) / 1000000.0;
        exporter.emitPlotValue("Kernel Duration (ms)", duration_ms);
    }
    
    // Update counter for active streams
    exporter.emitPlotValue("Active Streams", static_cast<int64_t>(num_kernels % 4 + 1));
    
    exporter.markFrameEnd(frame_name.c_str());
}

// =============================================================================
// Example: Basic Tracy Integration
// =============================================================================

void exampleBasicIntegration() {
    std::cout << "\n=== Example 1: Basic Tracy Integration ===\n";
    
    // Check if Tracy is enabled
    std::cout << "Tracy enabled: " << (ts_tracy::isTracyEnabled() ? "Yes" : "No") << "\n";
    std::cout << "Tracy connected: " << (ts_tracy::isTracyConnected() ? "Yes" : "No") << "\n";
    
    // Use unified profiling macros
    TracySmithZoneScopedC("exampleBasicIntegration", ts_tracy::colors::Default);
    
    // Log a message
    TracySmithMessage("Starting basic integration example");
    
    // Perform some work
    for (int i = 0; i < 5; ++i) {
        TracySmithZoneScopedC("iteration", ts_tracy::colors::KernelLaunch);
        simulateKernel("basic_kernel", 3);
    }
    
    TracySmithMessage("Basic integration example complete");
}

// =============================================================================
// Example: TraceSmith Event Export to Tracy
// =============================================================================

void exampleEventExport() {
    std::cout << "\n=== Example 2: TraceSmith Event Export to Tracy ===\n";
    
    TracySmithZoneScopedC("exampleEventExport", ts_tracy::colors::Default);
    
    // Create and configure Tracy exporter
    ts_tracy::TracyExporterConfig config;
    config.enable_gpu_zones = true;
    config.enable_memory_tracking = true;
    config.enable_counters = true;
    config.auto_configure_plots = true;
    
    ts_tracy::TracyExporter exporter(config);
    if (!exporter.initialize()) {
        std::cout << "Warning: Tracy exporter initialization failed (Tracy may not be enabled)\n";
    }
    
    // Create GPU context
    uint8_t gpu_ctx = exporter.createGpuContext(0, "Simulated GPU");
    std::cout << "Created GPU context: " << static_cast<int>(gpu_ctx) << "\n";
    
    // Create and export some TraceSmith events
    std::vector<TraceEvent> events;
    
    // Kernel launch event
    TraceEvent kernel_event;
    kernel_event.type = EventType::KernelLaunch;
    kernel_event.name = "matmul_f32";
    kernel_event.timestamp = getCurrentTimestamp();
    kernel_event.duration = 1500000;  // 1.5ms
    kernel_event.device_id = 0;
    kernel_event.stream_id = 0;
    kernel_event.metadata["grid_dim"] = "256x256x1";
    kernel_event.metadata["block_dim"] = "16x16x1";
    events.push_back(kernel_event);
    
    // Memory copy event
    TraceEvent memcpy_event;
    memcpy_event.type = EventType::MemcpyH2D;
    memcpy_event.name = "data_transfer";
    memcpy_event.timestamp = getCurrentTimestamp();
    memcpy_event.duration = 500000;  // 0.5ms
    memcpy_event.device_id = 0;
    memcpy_event.stream_id = 1;
    MemoryParams mem_params;
    mem_params.size_bytes = 4 * 1024 * 1024;  // 4MB
    memcpy_event.memory_params = mem_params;
    events.push_back(memcpy_event);
    
    // Export events
    exporter.exportEvents(events);
    
    std::cout << "Exported " << events.size() << " events to Tracy\n";
    std::cout << "Total events emitted: " << exporter.eventsEmitted() << "\n";
}

// =============================================================================
// Example: Frame-based Profiling
// =============================================================================

void exampleFrameProfiling() {
    std::cout << "\n=== Example 3: Frame-based Profiling ===\n";
    
    TracySmithZoneScopedC("exampleFrameProfiling", ts_tracy::colors::Default);
    
    ts_tracy::TracyExporter exporter;
    exporter.initialize();
    
    // Configure plots for frame profiling
    exporter.configurePlot("Frame Time (ms)", ts_tracy::PlotType::Number, false, true);
    exporter.configurePlot("Active Kernels", ts_tracy::PlotType::Number, true, false);
    
    const int num_frames = 10;
    
    for (int frame = 0; frame < num_frames; ++frame) {
        auto frame_start = std::chrono::high_resolution_clock::now();
        
        // Simulate frame
        simulateFrame(exporter, frame);
        
        // Calculate frame time
        auto frame_end = std::chrono::high_resolution_clock::now();
        double frame_time_ms = std::chrono::duration<double, std::milli>(
            frame_end - frame_start).count();
        
        exporter.emitPlotValue("Frame Time (ms)", frame_time_ms);
        
        // Target ~30 FPS
        if (frame_time_ms < 33.3) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(33.3 - frame_time_ms)));
        }
        
        // Mark frame boundary for Tracy
        ts_tracy::markFrame();
    }
    
    std::cout << "Completed " << num_frames << " frames\n";
    std::cout << "Events emitted: " << exporter.eventsEmitted() << "\n";
}

// =============================================================================
// Example: Memory Profiling Integration
// =============================================================================

void exampleMemoryProfiling() {
    std::cout << "\n=== Example 4: Memory Profiling Integration ===\n";
    
    TracySmithZoneScopedC("exampleMemoryProfiling", ts_tracy::colors::MemAlloc);
    
    ts_tracy::TracyExporterConfig config;
    config.enable_memory_tracking = true;
    
    ts_tracy::TracyExporter exporter(config);
    exporter.initialize();
    
    exporter.configurePlot("GPU Memory (MB)", ts_tracy::PlotType::Memory);
    
    // Simulate memory operations
    simulateMemoryOperations(exporter);
    
    std::cout << "Memory profiling complete\n";
    std::cout << "Events emitted: " << exporter.eventsEmitted() << "\n";
}

// =============================================================================
// Example: GPU Zone Profiling
// =============================================================================

void exampleGpuZoneProfiling() {
    std::cout << "\n=== Example 5: GPU Zone Profiling ===\n";
    
    TracySmithZoneScopedC("exampleGpuZoneProfiling", ts_tracy::colors::KernelLaunch);
    
    ts_tracy::TracyExporter exporter;
    exporter.initialize();
    
    uint8_t gpu_ctx = exporter.createGpuContext(0, "Test GPU");
    
    // Emit several GPU zones
    std::vector<std::string> kernel_names = {
        "conv2d_forward",
        "relu_activation",
        "batch_norm",
        "pooling_max",
        "fully_connected"
    };
    
    for (const auto& name : kernel_names) {
        auto cpu_start = getCurrentTimestamp();
        
        // Simulate kernel execution
        simulateKernel(name, 5);
        
        auto cpu_end = getCurrentTimestamp();
        
        // GPU timestamps (simulated with slight offset)
        auto gpu_start = cpu_start + 1000;  // 1μs launch latency
        auto gpu_end = cpu_end - 500;       // Complete slightly before CPU sees it
        
        exporter.emitGpuZone(gpu_ctx, name, cpu_start, cpu_end, 
                             gpu_start, gpu_end, ts_tracy::colors::KernelLaunch);
    }
    
    std::cout << "GPU zones emitted: " << exporter.gpuZonesEmitted() << "\n";
}

// =============================================================================
// Example: Using Global Exporter
// =============================================================================

void exampleGlobalExporter() {
    std::cout << "\n=== Example 6: Global Tracy Exporter ===\n";
    
    TracySmithZoneScopedC("exampleGlobalExporter", ts_tracy::colors::Default);
    
    // Configure global exporter (before first use)
    ts_tracy::TracyExporterConfig config;
    config.gpu_context_name = "Global GPU";
    config.auto_configure_plots = true;
    ts_tracy::setGlobalTracyExporterConfig(config);
    
    // Get global exporter
    auto& exporter = ts_tracy::getGlobalTracyExporter();
    
    // Use it from anywhere in the application
    TraceEvent event;
    event.type = EventType::Marker;
    event.name = "global_exporter_test";
    event.timestamp = getCurrentTimestamp();
    
    exporter.emitEvent(event);
    
    // Emit counter
    exporter.emitPlotValue("Test Counter", 42.0);
    
    std::cout << "Global exporter events: " << exporter.eventsEmitted() << "\n";
}

// =============================================================================
// Example: Full GPU Timeline (NEW - fixes message-based limitation)
// =============================================================================

void exampleFullGpuTimeline() {
    std::cout << "\n=== Example 7: Full GPU Timeline (Ascend/MetaX) ===\n";
    
    TracySmithZoneScopedC("exampleFullGpuTimeline", ts_tracy::colors::Default);
    
    // Create GPU contexts for different platforms
    // These create REAL GPU timelines in Tracy (not message-based)
    auto& ascend_ctx = ts_tracy::getOrCreateGpuContext(
        "Ascend 910B NPU", ts_tracy::GpuContextType::Ascend, 0);
    
    auto& metax_ctx = ts_tracy::getOrCreateGpuContext(
        "MetaX C500 GPU", ts_tracy::GpuContextType::MACA, 0);
    
    std::cout << "Created GPU contexts:\n";
    std::cout << "  - " << ascend_ctx.name() << " (ID: " << (int)ascend_ctx.contextId() << ")\n";
    std::cout << "  - " << metax_ctx.name() << " (ID: " << (int)metax_ctx.contextId() << ")\n";
    
    // Simulate kernel executions on Ascend NPU
    std::cout << "\nSimulating Ascend NPU kernels...\n";
    {
        std::vector<std::string> ascend_kernels = {
            "AscendMatMul", "AscendConv2D", "AscendBatchNorm", "AscendSoftmax"
        };
        
        for (const auto& kernel : ascend_kernels) {
            auto cpu_start = static_cast<int64_t>(getCurrentTimestamp());
            
            // Simulate kernel work
            volatile double result = 0;
            for (int i = 0; i < 50000; ++i) {
                result += std::sin(static_cast<double>(i));
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            
            auto cpu_end = static_cast<int64_t>(getCurrentTimestamp());
            
            // GPU timestamps (simulated - in real code these come from the profiler)
            auto gpu_start = cpu_start + 1000;  // 1μs launch latency
            auto gpu_end = cpu_end - 500;
            
            // Emit to full GPU timeline
            ascend_ctx.emitGpuZone(kernel.c_str(), cpu_start, cpu_end, 
                                   gpu_start, gpu_end, 0, 0xFF6600);
        }
    }
    
    // Simulate kernel executions on MetaX GPU
    std::cout << "Simulating MetaX GPU kernels...\n";
    {
        std::vector<std::string> metax_kernels = {
            "MetaXGEMM", "MetaXReduce", "MetaXElementwise", "MetaXTranspose"
        };
        
        for (const auto& kernel : metax_kernels) {
            auto cpu_start = static_cast<int64_t>(getCurrentTimestamp());
            
            // Simulate kernel work
            volatile double result = 0;
            for (int i = 0; i < 30000; ++i) {
                result += std::cos(static_cast<double>(i));
            }
            std::this_thread::sleep_for(std::chrono::microseconds(300));
            
            auto cpu_end = static_cast<int64_t>(getCurrentTimestamp());
            
            auto gpu_start = cpu_start + 800;
            auto gpu_end = cpu_end - 300;
            
            // Emit to full GPU timeline
            metax_ctx.emitGpuZone(kernel.c_str(), cpu_start, cpu_end,
                                  gpu_start, gpu_end, 0, 0x00FF66);
        }
    }
    
    // Also demonstrate using TracySmithGpuZone macro (RAII)
    std::cout << "Using RAII GPU zone...\n";
    {
        TracySmithGpuZone(ascend_ctx, "AscendTrainingStep");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    std::cout << "\nGPU zones emitted:\n";
    std::cout << "  - Ascend: " << ascend_ctx.zonesEmitted() << " zones\n";
    std::cout << "  - MetaX: " << metax_ctx.zonesEmitted() << " zones\n";
    std::cout << "\nThese appear as FULL GPU timelines in Tracy (not messages)!\n";
}

// =============================================================================
// Example: TraceSmith Events to Full GPU Timeline
// =============================================================================

void exampleTracesmithToGpuTimeline() {
    std::cout << "\n=== Example 8: TraceSmith Events → Full GPU Timeline ===\n";
    
    TracySmithZoneScopedC("exampleTracesmithToGpuTimeline", ts_tracy::colors::Default);
    
    // Create GPU context using PlatformType
    auto& gpu_ctx = ts_tracy::getOrCreateGpuContext(PlatformType::MACA, 0);
    
    // Create TraceSmith events (simulating what a profiler would capture)
    std::vector<TraceEvent> events;
    
    auto base_time = getCurrentTimestamp();
    
    // Kernel launch events
    for (int i = 0; i < 5; ++i) {
        TraceEvent event;
        event.type = EventType::KernelLaunch;
        event.name = "compute_kernel_" + std::to_string(i);
        event.timestamp = base_time + i * 2000000;  // 2ms apart
        event.duration = 1500000;  // 1.5ms each
        event.device_id = 0;
        event.stream_id = i % 2;
        events.push_back(event);
    }
    
    // Memory copy events
    TraceEvent h2d;
    h2d.type = EventType::MemcpyH2D;
    h2d.name = "data_upload";
    h2d.timestamp = base_time + 10000000;
    h2d.duration = 500000;
    events.push_back(h2d);
    
    TraceEvent d2h;
    d2h.type = EventType::MemcpyD2H;
    d2h.name = "result_download";
    d2h.timestamp = base_time + 12000000;
    d2h.duration = 300000;
    events.push_back(d2h);
    
    // Emit all events to full GPU timeline
    gpu_ctx.emitGpuZones(events);
    
    std::cout << "Converted " << events.size() << " TraceSmith events to GPU timeline\n";
    std::cout << "GPU zones emitted: " << gpu_ctx.zonesEmitted() << "\n";
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║   TraceSmith + Tracy Integration Example               ║\n";
    std::cout << "╠════════════════════════════════════════════════════════╣\n";
    std::cout << "║   Demonstrating bidirectional profiling integration    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n\n";
    
    // Set application info
    ts_tracy::setAppInfo("TraceSmith Tracy Integration Example");
    
    // Check Tracy status
    std::cout << "Tracy Integration Status:\n";
    std::cout << "  - Compile-time enabled: " << (ts_tracy::isTracyEnabled() ? "Yes" : "No") << "\n";
    std::cout << "  - Server connected: " << (ts_tracy::isTracyConnected() ? "Yes" : "No") << "\n";
    
    if (!ts_tracy::isTracyEnabled()) {
        std::cout << "\nNote: Tracy is not enabled. Rebuild with -DTRACESMITH_ENABLE_TRACY=ON\n";
        std::cout << "      to enable full Tracy integration.\n\n";
    }
    
    // Run examples
    exampleBasicIntegration();
    exampleEventExport();
    exampleFrameProfiling();
    exampleMemoryProfiling();
    exampleGpuZoneProfiling();
    exampleGlobalExporter();
    exampleFullGpuTimeline();          // NEW: Full GPU timeline
    exampleTracesmithToGpuTimeline();  // NEW: TraceSmith → GPU timeline
    
    std::cout << "\n════════════════════════════════════════════════════════\n";
    std::cout << "All examples completed successfully!\n";
    std::cout << "\nIf Tracy server is connected, you should see:\n";
    std::cout << "  - Zone timelines with kernel executions\n";
    std::cout << "  - Memory allocation graphs\n";
    std::cout << "  - Frame time plots\n";
    std::cout << "  - GPU zone visualizations\n";
    std::cout << "  - FULL GPU timelines for Ascend/MetaX (not message-based!)\n";
    std::cout << "════════════════════════════════════════════════════════\n";
    
    return 0;
}
