/**
 * TraceSmith Phase 2 Example
 * 
 * Demonstrates Phase 2 features:
 * - Call stack capture with StackCapture
 * - Instruction stream building
 * - Dependency analysis
 * - DOT export for visualization
 */

#include <tracesmith/tracesmith.hpp>
#include <tracesmith/stack_capture.hpp>
#include <tracesmith/instruction_stream.hpp>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <random>

using namespace tracesmith;

// Generate sample events that simulate a GPU pipeline
std::vector<TraceEvent> generatePipelineEvents() {
    std::vector<TraceEvent> events;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> duration_dist(10000, 100000);  // 10-100 µs
    
    Timestamp base_time = getCurrentTimestamp();
    Timestamp current_time = base_time;
    uint32_t correlation_id = 1;
    
    // Simulate a 3-stage pipeline on multiple streams
    const int num_iterations = 5;
    
    for (int iter = 0; iter < num_iterations; ++iter) {
        // Stage 1: Preprocess on stream 0
        TraceEvent preprocess;
        preprocess.type = EventType::KernelLaunch;
        preprocess.name = "preprocess_kernel";
        preprocess.timestamp = current_time;
        preprocess.duration = duration_dist(gen);
        preprocess.device_id = 0;
        preprocess.stream_id = 0;
        preprocess.correlation_id = correlation_id++;
        preprocess.kernel_params = KernelParams{256, 1, 1, 256, 1, 1, 0, 32};
        events.push_back(preprocess);
        current_time += preprocess.duration + 5000;
        
        // Stage 2: Compute on stream 1 (depends on preprocess)
        TraceEvent compute;
        compute.type = EventType::KernelLaunch;
        compute.name = "compute_kernel";
        compute.timestamp = current_time;
        compute.duration = duration_dist(gen) * 2;  // Longer
        compute.device_id = 0;
        compute.stream_id = 1;
        compute.correlation_id = correlation_id++;
        compute.kernel_params = KernelParams{512, 512, 1, 32, 8, 1, 4096, 48};
        events.push_back(compute);
        current_time += compute.duration + 5000;
        
        // Stage 3: Postprocess on stream 0
        TraceEvent postprocess;
        postprocess.type = EventType::KernelLaunch;
        postprocess.name = "postprocess_kernel";
        postprocess.timestamp = current_time;
        postprocess.duration = duration_dist(gen);
        postprocess.device_id = 0;
        postprocess.stream_id = 0;
        postprocess.correlation_id = correlation_id++;
        postprocess.kernel_params = KernelParams{128, 1, 1, 128, 1, 1, 0, 24};
        events.push_back(postprocess);
        current_time += postprocess.duration + 5000;
        
        // Memory copy between stages
        if (iter < num_iterations - 1) {
            TraceEvent memcpy_event;
            memcpy_event.type = EventType::MemcpyD2D;
            memcpy_event.name = "intermediate_copy";
            memcpy_event.timestamp = current_time;
            memcpy_event.duration = 20000;
            memcpy_event.device_id = 0;
            memcpy_event.stream_id = 2;
            memcpy_event.correlation_id = correlation_id++;
            memcpy_event.memory_params = MemoryParams{};
            memcpy_event.memory_params->size_bytes = 4 * 1024 * 1024;  // 4MB
            events.push_back(memcpy_event);
            current_time += memcpy_event.duration + 5000;
        }
        
        // Synchronization point
        TraceEvent sync;
        sync.type = EventType::StreamSync;
        sync.name = "cudaStreamSynchronize";
        sync.timestamp = current_time;
        sync.duration = 1000;
        sync.device_id = 0;
        sync.stream_id = 0;
        sync.correlation_id = correlation_id++;
        events.push_back(sync);
        current_time += sync.duration + 10000;
    }
    
    return events;
}

int main() {
    std::cout << "=== TraceSmith Phase 2: Call Stack & Instruction Stream ===\n\n";
    
    // ================================================================
    // Part 1: Call Stack Capture
    // ================================================================
    std::cout << "Part 1: Call Stack Capture\n";
    std::cout << "----------------------------\n\n";
    
    if (!StackCapture::isAvailable()) {
        std::cout << "Note: Stack capture not fully available on this platform\n";
        std::cout << "      Using fallback implementation\n\n";
    } else {
        std::cout << "Stack capture is available!\n";
    }
    
    std::cout << "Current thread ID: " << StackCapture::getCurrentThreadId() << "\n\n";
    
    // Capture call stack
    StackCaptureConfig config;
    config.max_depth = 16;
    config.resolve_symbols = true;
    config.demangle = true;
    config.skip_frames = 0;
    
    StackCapture capturer(config);
    CallStack stack = capturer.capture();
    
    std::cout << "Captured " << stack.frames.size() << " stack frames:\n";
    
    for (size_t i = 0; i < std::min(size_t(8), stack.frames.size()); ++i) {
        const auto& frame = stack.frames[i];
        std::cout << "  [" << i << "] " << std::hex << "0x" << frame.address << std::dec;
        
        if (!frame.function_name.empty()) {
            // Truncate long names
            std::string func = frame.function_name;
            if (func.length() > 50) {
                func = func.substr(0, 47) + "...";
            }
            std::cout << " " << func;
        }
        if (!frame.file_name.empty()) {
            std::cout << " (" << frame.file_name;
            if (frame.line_number > 0) {
                std::cout << ":" << frame.line_number;
            }
            std::cout << ")";
        }
        std::cout << "\n";
    }
    
    if (stack.frames.size() > 8) {
        std::cout << "  ... and " << (stack.frames.size() - 8) << " more frames\n";
    }
    std::cout << "\n";
    
    // ================================================================
    // Part 2: Event Generation with Context
    // ================================================================
    std::cout << "Part 2: Event Generation with Context\n";
    std::cout << "----------------------------------------\n\n";
    
    std::vector<TraceEvent> events = generatePipelineEvents();
    
    // Attach call stacks to some events
    for (size_t i = 0; i < events.size(); i += 3) {
        events[i].call_stack = capturer.capture();
    }
    
    std::cout << "Generated " << events.size() << " events\n";
    
    // Count events with call stacks
    size_t with_stacks = 0;
    for (const auto& event : events) {
        if (event.call_stack.has_value() && !event.call_stack->empty()) {
            with_stacks++;
        }
    }
    
    std::cout << "Events with call stacks: " << with_stacks << "\n";
    
    // Show first event with call stack
    for (const auto& event : events) {
        if (event.call_stack.has_value() && !event.call_stack->empty()) {
            std::cout << "\nExample event with call stack:\n";
            std::cout << "  Event: " << event.name << "\n";
            std::cout << "  Type: " << eventTypeToString(event.type) << "\n";
            std::cout << "  Stream: " << event.stream_id << "\n";
            std::cout << "  Call stack depth: " << event.call_stack->depth() << "\n";
            
            for (size_t i = 0; i < std::min(size_t(3), event.call_stack->frames.size()); ++i) {
                const auto& frame = event.call_stack->frames[i];
                std::cout << "    [" << i << "] ";
                if (!frame.function_name.empty()) {
                    std::string func = frame.function_name;
                    if (func.length() > 40) func = func.substr(0, 37) + "...";
                    std::cout << func;
                } else {
                    std::cout << std::hex << "0x" << frame.address << std::dec;
                }
                std::cout << "\n";
            }
            break;
        }
    }
    std::cout << "\n";
    
    // ================================================================
    // Part 3: Instruction Stream Analysis
    // ================================================================
    std::cout << "Part 3: Instruction Stream Analysis\n";
    std::cout << "-------------------------------------\n\n";
    
    InstructionStreamBuilder builder;
    builder.addEvents(events);
    builder.analyze();
    
    auto stats = builder.getStatistics();
    std::cout << "Instruction Stream Statistics:\n";
    std::cout << "  Total operations:     " << stats.total_operations << "\n";
    std::cout << "  Kernel launches:      " << stats.kernel_launches << "\n";
    std::cout << "  Memory operations:    " << stats.memory_operations << "\n";
    std::cout << "  Synchronizations:     " << stats.synchronizations << "\n";
    std::cout << "  Total dependencies:   " << stats.total_dependencies << "\n";
    
    std::cout << "\n  Operations per stream:\n";
    for (const auto& [stream_id, count] : stats.operations_per_stream) {
        std::cout << "    Stream " << stream_id << ": " << count << "\n";
    }
    std::cout << "\n";
    
    // ================================================================
    // Part 4: Dependency Analysis
    // ================================================================
    std::cout << "Part 4: Dependency Analysis\n";
    std::cout << "----------------------------\n\n";
    
    auto dependencies = builder.getDependencies();
    std::cout << "Found " << dependencies.size() << " dependencies\n";
    
    // Categorize dependencies
    size_t sequential = 0, sync = 0, memory = 0, other = 0;
    for (const auto& dep : dependencies) {
        switch (dep.type) {
            case DependencyType::Sequential:
                sequential++;
                break;
            case DependencyType::Synchronization:
                sync++;
                break;
            case DependencyType::MemoryDependency:
                memory++;
                break;
            default:
                other++;
                break;
        }
    }
    
    std::cout << "  Sequential:       " << sequential << "\n";
    std::cout << "  Synchronization:  " << sync << "\n";
    std::cout << "  Memory:           " << memory << "\n";
    std::cout << "  Other:            " << other << "\n\n";
    
    // Show first few dependencies
    std::cout << "Sample dependencies:\n";
    for (size_t i = 0; i < std::min(size_t(5), dependencies.size()); ++i) {
        const auto& dep = dependencies[i];
        std::cout << "  " << dep.from_correlation_id << " -> " << dep.to_correlation_id;
        
        switch (dep.type) {
            case DependencyType::Sequential:
                std::cout << " (Sequential)";
                break;
            case DependencyType::Synchronization:
                std::cout << " (Sync)";
                break;
            case DependencyType::MemoryDependency:
                std::cout << " (Memory)";
                break;
            default:
                break;
        }
        
        if (!dep.description.empty()) {
            std::cout << ": " << dep.description;
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    
    // ================================================================
    // Part 5: DOT Export
    // ================================================================
    std::cout << "Part 5: Visualization Export\n";
    std::cout << "-----------------------------\n\n";
    
    std::string dot = builder.exportToDot();
    
    std::ofstream dot_file("instruction_stream.dot");
    if (dot_file.is_open()) {
        dot_file << dot;
        dot_file.close();
        std::cout << "Exported dependency graph to: instruction_stream.dot\n";
        std::cout << "Visualize with: dot -Tpng instruction_stream.dot -o graph.png\n";
    }
    
    // Also save trace
    std::cout << "\nSaving trace to phase2_trace.sbt...\n";
    SBTWriter writer("phase2_trace.sbt");
    
    TraceMetadata metadata;
    metadata.application_name = "Phase2Example";
    metadata.start_time = events.front().timestamp;
    metadata.end_time = events.back().timestamp;
    writer.writeMetadata(metadata);
    
    std::vector<DeviceInfo> devices;
    DeviceInfo device;
    device.device_id = 0;
    device.name = "TraceSmith GPU";
    device.vendor = "TraceSmith";
    devices.push_back(device);
    writer.writeDeviceInfo(devices);
    
    for (const auto& event : events) {
        writer.writeEvent(event);
    }
    writer.finalize();
    
    std::cout << "Saved to: phase2_trace.sbt\n";
    
    std::cout << "\n=== Phase 2 Example Complete ===\n";
    
    return 0;
}
