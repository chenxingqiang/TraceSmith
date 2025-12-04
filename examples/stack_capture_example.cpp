/**
 * Stack Capture Example
 *
 * Demonstrates cross-platform call stack capturing:
 * - Capturing call stacks at runtime
 * - Symbol resolution and demangling
 * - Configurable capture depth
 * - Attaching stacks to trace events
 * - Performance considerations
 */

#include "tracesmith/common/stack_capture.hpp"
#include "tracesmith/common/types.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <functional>

using namespace tracesmith;

// Function call hierarchy for testing
namespace deep_call {

void capture_at_depth_3(StackCapture& capturer, CallStack& out) {
    capturer.capture(out);
}

void capture_at_depth_2(StackCapture& capturer, CallStack& out) {
    capture_at_depth_3(capturer, out);
}

void capture_at_depth_1(StackCapture& capturer, CallStack& out) {
    capture_at_depth_2(capturer, out);
}

}  // namespace deep_call

// Recursive function to test deep stacks
void recursive_capture(StackCapture& capturer, int depth, CallStack& out) {
    if (depth <= 0) {
        capturer.capture(out);
        return;
    }
    recursive_capture(capturer, depth - 1, out);
}

// Function to demonstrate capturing during "GPU operations"
void simulate_gpu_operation(StackCapture& capturer,
                            std::vector<TraceEvent>& events,
                            const std::string& kernel_name) {
    // Capture stack at the point of kernel launch
    CallStack stack;
    capturer.capture(stack);

    // Create event with stack attached
    TraceEvent event;
    event.type = EventType::KernelLaunch;
    event.name = kernel_name;
    event.timestamp = getCurrentTimestamp();
    event.duration = 100000;  // 100µs
    event.device_id = 0;
    event.stream_id = 0;
    event.call_stack = stack;
    event.thread_id = stack.thread_id;

    events.push_back(event);
}

void run_computation(StackCapture& capturer, std::vector<TraceEvent>& events) {
    simulate_gpu_operation(capturer, events, "matrix_multiply");
    simulate_gpu_operation(capturer, events, "relu_activation");
    simulate_gpu_operation(capturer, events, "softmax");
}

void train_step(StackCapture& capturer, std::vector<TraceEvent>& events) {
    run_computation(capturer, events);
    simulate_gpu_operation(capturer, events, "backward_pass");
    simulate_gpu_operation(capturer, events, "optimizer_step");
}

int main() {
    std::cout << "TraceSmith Stack Capture Example\n";
    std::cout << "=================================\n\n";

    // ================================================================
    // Part 1: Check Availability
    // ================================================================
    std::cout << "Part 1: Platform Information\n";
    std::cout << "----------------------------\n";

    bool available = StackCapture::isAvailable();
    std::cout << "  Stack capture available: " << (available ? "Yes" : "No") << "\n";
    std::cout << "  Current thread ID: " << StackCapture::getCurrentThreadId() << "\n";

#ifdef __APPLE__
    std::cout << "  Platform: macOS (using backtrace())\n";
#elif defined(__linux__)
    std::cout << "  Platform: Linux (using libunwind or backtrace)\n";
#elif defined(_WIN32)
    std::cout << "  Platform: Windows (using CaptureStackBackTrace)\n";
#else
    std::cout << "  Platform: Unknown\n";
#endif
    std::cout << "\n";

    // ================================================================
    // Part 2: Basic Capture
    // ================================================================
    std::cout << "Part 2: Basic Capture\n";
    std::cout << "---------------------\n";

    StackCaptureConfig config;
    config.max_depth = 32;
    config.skip_frames = 0;
    config.resolve_symbols = true;
    config.demangle = true;

    std::cout << "  Configuration:\n";
    std::cout << "    max_depth: " << config.max_depth << "\n";
    std::cout << "    skip_frames: " << config.skip_frames << "\n";
    std::cout << "    resolve_symbols: " << (config.resolve_symbols ? "true" : "false") << "\n";
    std::cout << "    demangle: " << (config.demangle ? "true" : "false") << "\n\n";

    StackCapture capturer(config);
    CallStack stack;
    capturer.capture(stack);

    std::cout << "  Captured " << stack.frames.size() << " frames:\n";
    for (size_t i = 0; i < std::min(size_t(10), stack.frames.size()); ++i) {
        const auto& frame = stack.frames[i];
        std::cout << "    [" << std::setw(2) << i << "] ";
        std::cout << std::hex << "0x" << std::setw(12) << std::setfill('0')
                  << frame.address << std::dec << std::setfill(' ');

        if (!frame.function_name.empty()) {
            std::string func = frame.function_name;
            if (func.length() > 45) {
                func = func.substr(0, 42) + "...";
            }
            std::cout << " " << func;
        }
        std::cout << "\n";
    }
    if (stack.frames.size() > 10) {
        std::cout << "    ... and " << (stack.frames.size() - 10) << " more frames\n";
    }
    std::cout << "\n";

    // ================================================================
    // Part 3: Nested Function Calls
    // ================================================================
    std::cout << "Part 3: Nested Function Calls\n";
    std::cout << "-----------------------------\n";

    CallStack nested_stack;
    deep_call::capture_at_depth_1(capturer, nested_stack);

    std::cout << "  Captured from 3 levels of nesting:\n";
    std::cout << "  Thread ID: " << nested_stack.thread_id << "\n";
    std::cout << "  Frames captured: " << nested_stack.frames.size() << "\n";

    // Look for our test functions
    int found = 0;
    for (const auto& frame : nested_stack.frames) {
        if (frame.function_name.find("capture_at_depth") != std::string::npos) {
            found++;
        }
    }
    std::cout << "  Found " << found << " test function frames\n\n";

    // ================================================================
    // Part 4: Recursive Capture
    // ================================================================
    std::cout << "Part 4: Recursive Capture (10 levels)\n";
    std::cout << "--------------------------------------\n";

    CallStack recursive_stack;
    recursive_capture(capturer, 10, recursive_stack);

    std::cout << "  Frames captured: " << recursive_stack.frames.size() << "\n";

    // Count recursive calls
    int recursive_count = 0;
    for (const auto& frame : recursive_stack.frames) {
        if (frame.function_name.find("recursive_capture") != std::string::npos) {
            recursive_count++;
        }
    }
    std::cout << "  Recursive frames found: " << recursive_count << "\n\n";

    // ================================================================
    // Part 5: Attaching Stacks to Events
    // ================================================================
    std::cout << "Part 5: Attaching Stacks to Trace Events\n";
    std::cout << "-----------------------------------------\n";

    std::vector<TraceEvent> events;
    train_step(capturer, events);

    std::cout << "  Generated " << events.size() << " events with call stacks:\n";
    for (const auto& event : events) {
        std::cout << "    - " << event.name;
        if (event.call_stack.has_value()) {
            std::cout << " (stack depth: " << event.call_stack->depth() << ")";
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    // Show stack for first event
    if (!events.empty() && events[0].call_stack.has_value()) {
        std::cout << "  First event call stack:\n";
        const auto& ev_stack = events[0].call_stack.value();
        for (size_t i = 0; i < std::min(size_t(5), ev_stack.frames.size()); ++i) {
            const auto& frame = ev_stack.frames[i];
            std::string func = frame.function_name.empty() ?
                              "<unknown>" : frame.function_name;
            if (func.length() > 50) func = func.substr(0, 47) + "...";
            std::cout << "      [" << i << "] " << func << "\n";
        }
    }
    std::cout << "\n";

    // ================================================================
    // Part 6: Performance Measurement
    // ================================================================
    std::cout << "Part 6: Performance Measurement\n";
    std::cout << "--------------------------------\n";

    const int iterations = 1000;

    // Measure capture time
    auto start = std::chrono::high_resolution_clock::now();
    CallStack perf_stack;
    for (int i = 0; i < iterations; ++i) {
        capturer.capture(perf_stack);
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double avg_us = static_cast<double>(duration.count()) / iterations;

    std::cout << "  Iterations: " << iterations << "\n";
    std::cout << "  Total time: " << duration.count() << " µs\n";
    std::cout << "  Average per capture: " << std::fixed << std::setprecision(2)
              << avg_us << " µs\n";
    std::cout << "  Captures per second: " << std::fixed << std::setprecision(0)
              << (1000000.0 / avg_us) << "\n\n";

    // ================================================================
    // Part 7: Skip Frames Configuration
    // ================================================================
    std::cout << "Part 7: Skip Frames Configuration\n";
    std::cout << "----------------------------------\n";

    StackCaptureConfig skip_config;
    skip_config.max_depth = 16;
    skip_config.skip_frames = 3;  // Skip first 3 frames
    skip_config.resolve_symbols = true;

    StackCapture skip_capturer(skip_config);
    CallStack skip_stack;
    skip_capturer.capture(skip_stack);

    std::cout << "  Skip frames: 3\n";
    std::cout << "  Frames captured: " << skip_stack.frames.size() << "\n";
    std::cout << "  First frame function: ";
    if (!skip_stack.frames.empty() && !skip_stack.frames[0].function_name.empty()) {
        std::string func = skip_stack.frames[0].function_name;
        if (func.length() > 50) func = func.substr(0, 47) + "...";
        std::cout << func;
    } else {
        std::cout << "<unknown>";
    }
    std::cout << "\n\n";

    // ================================================================
    // Summary
    // ================================================================
    std::cout << std::string(60, '=') << "\n";
    std::cout << "Stack Capture Example Complete!\n";
    std::cout << std::string(60, '=') << "\n\n";

    std::cout << "Features Demonstrated:\n";
    std::cout << "  ✓ Cross-platform stack capture\n";
    std::cout << "  ✓ Symbol resolution\n";
    std::cout << "  ✓ Function name demangling\n";
    std::cout << "  ✓ Configurable capture depth\n";
    std::cout << "  ✓ Frame skipping\n";
    std::cout << "  ✓ Attaching stacks to trace events\n";
    std::cout << "  ✓ Performance measurement (~" << std::fixed << std::setprecision(0)
              << avg_us << " µs/capture)\n";

    return 0;
}

