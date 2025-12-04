#pragma once

#include "tracesmith/common/types.hpp"
#include <cstdint>
#include <vector>
#include <string>

namespace tracesmith {

/**
 * Configuration for stack capture
 */
struct StackCaptureConfig {
    uint32_t max_depth = 32;           // Maximum number of stack frames to capture
    uint32_t skip_frames = 2;          // Number of frames to skip (capture/profiler frames)
    bool async_signal_safe = false;    // Use async-signal-safe capture
    bool resolve_symbols = true;       // Resolve symbols immediately
    bool demangle = true;              // Demangle C++ symbols
    
    StackCaptureConfig() = default;
};

/**
 * Stack capture engine
 * 
 * Provides cross-platform stack unwinding with configurable options.
 * Supports macOS, Linux, and Windows.
 */
class StackCapture {
public:
    explicit StackCapture(const StackCaptureConfig& config = StackCaptureConfig());
    
    /**
     * Capture the current call stack
     * 
     * @param out Output call stack
     * @return Number of frames captured
     */
    size_t capture(CallStack& out);
    
    /**
     * Capture call stack with a specific thread ID
     * 
     * @param thread_id Thread ID to associate with the stack
     * @param out Output call stack
     * @return Number of frames captured
     */
    size_t captureWithThreadId(uint64_t thread_id, CallStack& out);
    
    /**
     * Resolve symbols for captured addresses
     * 
     * @param stack Call stack with addresses to resolve
     * @return true if at least one symbol was resolved
     */
    bool resolveSymbols(CallStack& stack);
    
    /**
     * Get the current thread ID
     */
    static uint64_t getCurrentThreadId();
    
    /**
     * Check if stack capture is available on this platform
     */
    static bool isAvailable();
    
private:
    StackCaptureConfig config_;
    
    // Platform-specific capture
    size_t captureImpl(void** addresses, size_t max_depth);
    bool resolveAddress(uint64_t address, StackFrame& frame);
    std::string demangleSymbol(const char* mangled);
};

/**
 * RAII helper to capture stack on construction
 */
class ScopedStackCapture {
public:
    explicit ScopedStackCapture(StackCapture& capturer)
        : capturer_(capturer)
        , captured_(false) {
        capturer_.capture(stack_);
        captured_ = !stack_.empty();
    }
    
    const CallStack& stack() const { return stack_; }
    bool captured() const { return captured_; }
    
private:
    StackCapture& capturer_;
    CallStack stack_;
    bool captured_;
};

} // namespace tracesmith
