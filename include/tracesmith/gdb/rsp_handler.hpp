/**
 * @file rsp_handler.hpp
 * @brief GDB Remote Serial Protocol handler
 * @version 0.10.0
 * 
 * Main RSP protocol handler that:
 * - Accepts GDB client connections
 * - Parses and dispatches RSP commands
 * - Integrates CPU debugging (ProcessController) with GPU debugging (GPUDebugEngine)
 * - Implements TraceSmith extensions via monitor commands
 */

#pragma once

#include "tracesmith/gdb/gdb_types.hpp"
#include "tracesmith/gdb/rsp_packet.hpp"
#include "tracesmith/gdb/process_controller.hpp"
#include "tracesmith/gdb/gpu_debug_engine.hpp"
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <thread>

namespace tracesmith {
namespace gdb {

/// RSP Handler configuration
struct RSPConfig {
    int port = 1234;
    std::string unix_socket;        // If set, use Unix socket instead of TCP
    bool verbose = false;
    bool enable_gpu_extensions = true;
    bool enable_no_ack_mode = true;
    size_t max_packet_size = 16384;
};

/**
 * RSP Protocol Handler
 * 
 * Implements the GDB Remote Serial Protocol for debugging with GPU extensions.
 * 
 * Usage:
 * @code
 * RSPHandler handler;
 * handler.initialize(pid);  // or handler.initialize({"./program", "arg1"})
 * handler.listen();
 * handler.run();  // Blocking event loop
 * @endcode
 * 
 * TraceSmith GPU extensions are accessed via monitor commands:
 * @code
 * (gdb) monitor ts status      # GPU status
 * (gdb) monitor ts kernels     # Kernel history
 * (gdb) monitor ts break kernel matmul*  # GPU breakpoint
 * @endcode
 */
class RSPHandler {
public:
    explicit RSPHandler(const RSPConfig& config = RSPConfig{});
    ~RSPHandler();
    
    // Non-copyable
    RSPHandler(const RSPHandler&) = delete;
    RSPHandler& operator=(const RSPHandler&) = delete;
    
    // ============================================================
    // Initialization
    // ============================================================
    
    /// Initialize by attaching to existing process
    bool initialize(pid_t pid);
    
    /// Initialize by spawning new process
    bool initialize(const std::vector<std::string>& args);
    
    /// Start listening for GDB connections
    bool listen();
    
    /// Run main event loop (blocking)
    void run();
    
    /// Stop the handler
    void stop();
    
    /// Check if running
    bool isRunning() const { return running_; }
    
    // ============================================================
    // Component Access
    // ============================================================
    
    /// Get process controller
    ProcessController* processController() { return process_.get(); }
    const ProcessController* processController() const { return process_.get(); }
    
    /// Get GPU debug engine
    GPUDebugEngine* gpuEngine() { return gpu_engine_.get(); }
    const GPUDebugEngine* gpuEngine() const { return gpu_engine_.get(); }
    
    // ============================================================
    // Configuration
    // ============================================================
    
    /// Get current configuration
    const RSPConfig& config() const { return config_; }
    
    /// Set verbose mode
    void setVerbose(bool v) { config_.verbose = v; }

private:
    RSPConfig config_;
    std::atomic<bool> running_{false};
    bool no_ack_mode_ = false;
    
    int server_fd_ = -1;
    int client_fd_ = -1;
    
    std::unique_ptr<ProcessController> process_;
    std::unique_ptr<GPUDebugEngine> gpu_engine_;
    
    // ============================================================
    // Packet I/O
    // ============================================================
    
    std::string receivePacket();
    bool sendPacket(const std::string& data);
    bool sendRaw(const std::string& data);
    bool waitForAck();
    
    // ============================================================
    // Command Dispatch
    // ============================================================
    
    std::string handlePacket(const std::string& packet);
    
    // Query handlers
    std::string handleQuery(const std::string& query);
    std::string handleQuerySet(const std::string& query);
    std::string handleVCommand(const std::string& cmd);
    std::string handleMonitor(const std::string& cmd);
    
    // Standard GDB commands
    std::string handleReadRegisters();
    std::string handleWriteRegisters(const std::string& data);
    std::string handleReadMemory(uint64_t addr, size_t len);
    std::string handleWriteMemory(uint64_t addr, const std::string& data);
    std::string handleBinaryWrite(uint64_t addr, size_t len, const std::string& data);
    std::string handleContinue(int signal = 0);
    std::string handleStep(int signal = 0);
    std::string handleBreakpoint(char op, int type, uint64_t addr, int kind);
    std::string handleStopReason();
    std::string handleThreadOps(char op, const std::string& args);
    std::string handleThreadAlive(pid_t tid);
    
    // ============================================================
    // TraceSmith Extensions (via monitor)
    // ============================================================
    
    std::string handleTSHelp();
    std::string handleTSStatus();
    std::string handleTSDevices();
    std::string handleTSMemory(const std::string& args);
    std::string handleTSKernels(const std::string& args);
    std::string handleTSKernelSearch(const std::string& pattern);
    std::string handleTSStreams();
    std::string handleTSBreakpoint(const std::string& args);
    std::string handleTSGPUMemory(const std::string& args);
    std::string handleTSAllocations(const std::string& args);
    std::string handleTSTraceStart();
    std::string handleTSTraceStop();
    std::string handleTSTraceSave(const std::string& filename);
    std::string handleTSTraceLoad(const std::string& filename);
    std::string handleTSReplay(const std::string& args);
    
    // ============================================================
    // Helpers
    // ============================================================
    
    void waitForTarget();
    StopEvent waitForStop();
    std::string formatStopReply(const StopEvent& event);
    std::string formatGPUBreakpointHit(const GPUBreakpoint& bp, const TraceEvent& event);
    void log(const std::string& msg);
};

} // namespace gdb
} // namespace tracesmith
