/**
 * @file gpu_debug_engine.hpp
 * @brief GPU debugging engine integrating TraceSmith profiling with GDB
 * @version 0.10.0
 * 
 * Provides GPU state management, breakpoints, kernel history tracking,
 * memory monitoring, and trace replay capabilities for GDB debugging.
 */

#pragma once

#include "tracesmith/gdb/gdb_types.hpp"
#include "tracesmith/capture/profiler.hpp"
#include "tracesmith/capture/memory_profiler.hpp"
#include "tracesmith/state/gpu_state_machine.hpp"
#include "tracesmith/replay/replay_engine.hpp"
#include <memory>
#include <mutex>
#include <deque>

namespace tracesmith {
namespace gdb {

/// GPU debug engine configuration
struct GPUDebugConfig {
    size_t kernel_history_size = 1000;      // Max kernel calls to keep
    size_t event_history_size = 10000;      // Max events to keep
    bool auto_capture_on_break = true;      // Capture GPU state on CPU break
    bool capture_callstacks = true;
    uint32_t callstack_depth = 16;
};

/**
 * GPU Debug Engine
 * 
 * Integrates TraceSmith profiling capabilities with GDB debugging:
 * - GPU state capture and querying
 * - GPU breakpoints (kernel launch, memcpy, etc.)
 * - Kernel execution history
 * - Memory allocation tracking
 * - Trace capture and replay
 */
class GPUDebugEngine {
public:
    explicit GPUDebugEngine(const GPUDebugConfig& config = GPUDebugConfig{});
    ~GPUDebugEngine();
    
    // Non-copyable
    GPUDebugEngine(const GPUDebugEngine&) = delete;
    GPUDebugEngine& operator=(const GPUDebugEngine&) = delete;
    
    // ============================================================
    // Initialization
    // ============================================================
    
    /// Initialize for target process
    bool initialize(pid_t target_pid);
    
    /// Finalize and cleanup
    void finalize();
    
    /// Check if initialized
    bool isInitialized() const { return initialized_; }
    
    // ============================================================
    // GPU State
    // ============================================================
    
    /// Get current GPU state snapshot
    GPUStateSnapshot getGPUState();
    
    /// Get device information
    std::vector<DeviceInfo> getDevices() const;
    
    /// Get memory usage for device (-1 for all devices)
    MemorySnapshot getMemoryUsage(int device_id = -1);
    
    /// Get stream states
    std::vector<GPUStateSnapshot::StreamState> getStreamStates();
    
    // ============================================================
    // Kernel History
    // ============================================================
    
    /// Get recent kernel calls
    std::vector<KernelCallInfo> getKernelHistory(size_t count = 100);
    
    /// Get currently running kernels
    std::vector<KernelCallInfo> getActiveKernels();
    
    /// Get kernel by name pattern (wildcard * supported)
    std::vector<KernelCallInfo> findKernels(const std::string& pattern);
    
    // ============================================================
    // GPU Breakpoints
    // ============================================================
    
    /// Set GPU breakpoint, returns breakpoint ID
    int setGPUBreakpoint(const GPUBreakpoint& bp);
    
    /// Remove GPU breakpoint by ID
    bool removeGPUBreakpoint(int bp_id);
    
    /// Enable/disable GPU breakpoint
    bool enableGPUBreakpoint(int bp_id, bool enable);
    
    /// List all GPU breakpoints
    std::vector<GPUBreakpoint> listGPUBreakpoints() const;
    
    /// Check event against breakpoints, returns matching breakpoint if any
    std::optional<GPUBreakpoint> checkBreakpoints(const TraceEvent& event);
    
    // ============================================================
    // GPU Memory Access
    // ============================================================
    
    /// Read GPU memory (returns empty on failure)
    std::vector<uint8_t> readGPUMemory(int device, uint64_t addr, size_t len);
    
    /// Write GPU memory
    bool writeGPUMemory(int device, uint64_t addr, const std::vector<uint8_t>& data);
    
    /// Get memory allocations
    std::vector<MemoryAllocation> getMemoryAllocations(int device = -1);
    
    // ============================================================
    // Trace Capture
    // ============================================================
    
    /// Start trace capture
    bool startCapture();
    
    /// Stop trace capture
    bool stopCapture();
    
    /// Check if capturing
    bool isCapturing() const;
    
    /// Get captured events
    std::vector<TraceEvent> getCapturedEvents();
    
    /// Save trace to file
    bool saveTrace(const std::string& filename);
    
    // ============================================================
    // Trace Replay
    // ============================================================
    
    /// Load trace for replay
    bool loadTrace(const std::string& filename);
    
    /// Get replay state
    ReplayState getReplayState() const;
    
    /// Control replay
    bool controlReplay(const ReplayControl& control);
    
    /// Get current replay event
    std::optional<TraceEvent> getCurrentReplayEvent();
    
    // ============================================================
    // Event Callback
    // ============================================================
    
    /// Callback for GPU events (event, matching breakpoint or nullptr)
    using EventCallback = std::function<void(const TraceEvent&, const GPUBreakpoint*)>;
    
    /// Set callback for GPU events
    void setEventCallback(EventCallback callback);
    
    // ============================================================
    // Process Integration
    // ============================================================
    
    /// Called when CPU stops (breakpoint, signal, etc.)
    void onProcessStop();
    
    /// Called when CPU resumes
    void onProcessResume();

private:
    GPUDebugConfig config_;
    bool initialized_ = false;
    pid_t target_pid_ = 0;
    
    // TraceSmith components
    std::unique_ptr<IPlatformProfiler> profiler_;
    std::unique_ptr<MemoryProfiler> memory_profiler_;
    std::unique_ptr<GPUStateMachine> state_machine_;
    std::unique_ptr<ReplayEngine> replay_engine_;
    
    // State tracking
    std::deque<KernelCallInfo> kernel_history_;
    std::deque<TraceEvent> event_history_;
    std::vector<GPUBreakpoint> gpu_breakpoints_;
    int next_gpu_bp_id_ = 1;
    
    // Capture state
    bool capturing_ = false;
    std::vector<TraceEvent> captured_events_;
    
    // Replay state
    ReplayState replay_state_;
    std::vector<TraceEvent> replay_events_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Callback
    EventCallback event_callback_;
    
    // Internal helpers
    void handleEvent(const TraceEvent& event);
    void addToKernelHistory(const TraceEvent& event);
    bool matchesPattern(const std::string& name, const std::string& pattern) const;
};

} // namespace gdb
} // namespace tracesmith
