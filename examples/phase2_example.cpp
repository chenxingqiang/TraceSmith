/**
 * TraceSmith Phase 2 Example
 * 
 * Demonstrates Phase 2 features:
 * - Call stack capture
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

using namespace tracesmith;

// Example function hierarchy to generate interesting call stacks
void launchKernel(SimulationProfiler* profiler, const std::string& name) {
    profiler->generateKernelEvent(name, 0);
}

void processData(SimulationProfiler* profiler) {
    launchKernel(profiler, "preprocess_kernel");
    launchKernel(profiler, "compute_kernel");
    launchKernel(profiler, "postprocess_kernel");
}

void runPipeline(SimulationProfiler* profiler) {
    processData(profiler);
}

int main() {
    std::cout << "=== TraceSmith Phase 2: Call Stack & Instruction Stream ===\n\n";
    
    // ================================================================
    // Part 1: Call Stack Capture
    // ================================================================
    std::cout << "Part 1: Call Stack Capture\n";
    std::cout << "----------------------------\n\n";
    
    if (!StackCapture::isAvailable()) {
        std::cout << "Stack capture not available on this platform\n";
        return 1;
    }
    
    std::cout << "Stack capture is available!\n";
    std::cout << "Current thread ID: " << StackCapture::getCurrentThreadId() << "\n\n";
    
    // Capture call stack
    StackCaptureConfig config;
    config.max_depth = 10;
    config.resolve_symbols = true;
    config.demangle = true;
    
    StackCapture capturer(config);
    CallStack stack;
    
    size_t frames = capturer.capture(stack);
    std::cout << "Captured " << frames << " stack frames:\n";
    
    for (size_t i = 0; i < std::min(size_t(5), stack.frames.size()); ++i) {
        const auto& frame = stack.frames[i];
        std::cout << "  [" << i << "] " << std::hex << "0x" << frame.address << std::dec;
        
        if (!frame.function_name.empty()) {
            std::cout << " " << frame.function_name;
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
    
    std::cout << "\n";
    
    // ================================================================
    // Part 2: Event Capture with Call Stacks
    // ================================================================
    std::cout << "Part 2: Event Capture with Call Stacks\n";
    std::cout << "----------------------------------------\n\n";
    
    auto profiler = std::make_unique<SimulationProfiler>();
    
    ProfilerConfig prof_config;
    prof_config.buffer_size = 10000;
    prof_config.capture_callstacks = true;
    prof_config.callstack_depth = 16;
    
    profiler->initialize(prof_config);
    profiler->setEventRate(50);  // Lower rate for easier analysis
    
    profiler->startCapture();
    
    // Generate some events with call stacks
    runPipeline(profiler.get());
    
    // Wait a bit for more events
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    profiler->stopCapture();
    
    std::vector<TraceEvent> events;
    profiler->getEvents(events);
    
    std::cout << "Captured " << events.size() << " events\n";
    
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
            std::cout << "  Stream: " << event.stream_id << "\n";
            std::cout << "  Call stack depth: " << event.call_stack->depth() << "\n";
            
            for (size_t i = 0; i < std::min(size_t(3), event.call_stack->frames.size()); ++i) {
                const auto& frame = event.call_stack->frames[i];
                std::cout << "    [" << i << "] ";
                if (!frame.function_name.empty()) {
                    std::cout << frame.function_name;
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
    size_t sequential = 0, sync = 0, other = 0;
    for (const auto& dep : dependencies) {
        switch (dep.type) {
            case DependencyType::Sequential:
                sequential++;
                break;
            case DependencyType::Synchronization:
                sync++;
                break;
            default:
                other++;
                break;
        }
    }
    
    std::cout << "  Sequential:      " << sequential << "\n";
    std::cout << "  Synchronization: " << sync << "\n";
    std::cout << "  Other:           " << other << "\n\n";
    
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
    
    std::cout << "\n=== Phase 2 Example Complete ===\n";
    
    return 0;
}
