/**
 * @file process_controller.hpp
 * @brief Process control via ptrace for GDB stub
 * @version 0.10.0
 * 
 * Provides low-level process control operations including:
 * - Process attach/spawn/detach
 * - Execution control (continue, step, interrupt)
 * - Register and memory access
 * - Software breakpoint management
 */

#pragma once

#include "tracesmith/gdb/gdb_types.hpp"
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <sys/types.h>

namespace tracesmith {
namespace gdb {

/// CPU register set (x86_64)
struct RegisterSet {
    uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0;
    uint64_t rsi = 0, rdi = 0, rbp = 0, rsp = 0;
    uint64_t r8 = 0, r9 = 0, r10 = 0, r11 = 0;
    uint64_t r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    uint64_t rip = 0, rflags = 0;
    uint64_t cs = 0, ss = 0, ds = 0, es = 0, fs = 0, gs = 0;
    
    /// Serialize to GDB hex format (little-endian)
    std::string toHex() const;
    
    /// Deserialize from GDB hex format
    static RegisterSet fromHex(const std::string& hex);
    
    /// Get register count
    static constexpr size_t count() { return 24; }
};

/// Software breakpoint info
struct Breakpoint {
    int id = -1;
    uint64_t address = 0;
    uint8_t original_byte = 0;    // Saved instruction byte (int3 replacement)
    bool enabled = true;
    uint64_t hit_count = 0;
};

/**
 * Process Controller
 * 
 * Controls target process using ptrace system call.
 * Provides:
 * - Process lifecycle management (attach, spawn, detach, kill)
 * - Execution control (continue, step, interrupt)
 * - Register read/write
 * - Memory read/write
 * - Software breakpoint management
 * - Thread enumeration
 */
class ProcessController {
public:
    ProcessController();
    ~ProcessController();
    
    // Non-copyable
    ProcessController(const ProcessController&) = delete;
    ProcessController& operator=(const ProcessController&) = delete;
    
    // ============================================================
    // Process Lifecycle
    // ============================================================
    
    /// Attach to existing process
    bool attach(pid_t pid);
    
    /// Spawn new process with arguments
    bool spawn(const std::vector<std::string>& args);
    
    /// Detach from process (leaves it running)
    bool detach();
    
    /// Kill the target process
    bool kill();
    
    /// Check if attached to a process
    bool isAttached() const { return pid_ > 0 && attached_; }
    
    /// Get target PID
    pid_t pid() const { return pid_; }
    
    // ============================================================
    // Execution Control
    // ============================================================
    
    /// Continue execution (optionally with signal)
    bool continueExecution(int signal = 0);
    
    /// Single step one instruction
    bool singleStep(int signal = 0);
    
    /// Interrupt running process (send SIGSTOP)
    bool interrupt();
    
    /// Wait for target to stop, returns stop event
    StopEvent waitForStop();
    
    // ============================================================
    // Thread Control
    // ============================================================
    
    /// Get list of all threads in process
    std::vector<pid_t> getThreads() const;
    
    /// Get current thread for operations
    pid_t currentThread() const { return current_thread_; }
    
    /// Select thread for subsequent operations
    bool selectThread(pid_t tid);
    
    /// Check if specific thread is alive
    bool isThreadAlive(pid_t tid) const;
    
    // ============================================================
    // Register Access
    // ============================================================
    
    /// Read all general-purpose registers
    RegisterSet readRegisters();
    
    /// Write all general-purpose registers
    bool writeRegisters(const RegisterSet& regs);
    
    /// Read single register by number
    uint64_t readRegister(int reg_num);
    
    /// Write single register by number
    bool writeRegister(int reg_num, uint64_t value);
    
    // ============================================================
    // Memory Access
    // ============================================================
    
    /// Read memory from target (returns empty on error)
    std::vector<uint8_t> readMemory(uint64_t addr, size_t len);
    
    /// Write memory to target
    bool writeMemory(uint64_t addr, const std::vector<uint8_t>& data);
    
    // ============================================================
    // Breakpoints
    // ============================================================
    
    /// Set software breakpoint at address, returns breakpoint ID
    int setBreakpoint(uint64_t addr);
    
    /// Remove breakpoint by ID
    bool removeBreakpoint(int bp_id);
    
    /// Remove breakpoint at address
    bool removeBreakpointAt(uint64_t addr);
    
    /// Enable/disable breakpoint
    bool enableBreakpoint(int bp_id, bool enable);
    
    /// Get breakpoint info by ID
    const Breakpoint* getBreakpoint(int bp_id) const;
    
    /// List all breakpoints
    std::vector<Breakpoint> listBreakpoints() const;
    
    /// Check if address has an active breakpoint
    bool hasBreakpointAt(uint64_t addr) const;
    
    // ============================================================
    // GPU Event Integration
    // ============================================================
    
    /// Callback for GPU events detected during wait
    using GPUEventCallback = std::function<void(const TraceEvent&)>;
    
    /// Set callback for GPU events
    void setGPUEventCallback(GPUEventCallback callback);

private:
    pid_t pid_ = 0;
    pid_t current_thread_ = 0;
    bool attached_ = false;
    
    std::map<int, Breakpoint> breakpoints_;
    std::map<uint64_t, int> addr_to_bp_;
    int next_bp_id_ = 1;
    
    mutable std::set<pid_t> threads_;
    
    GPUEventCallback gpu_callback_;
    
    // Internal helpers
    bool ptraceOp(int request, pid_t tid, void* addr, void* data);
    void updateThreadList() const;
    bool insertBreakpointInstruction(uint64_t addr, uint8_t& original);
    bool removeBreakpointInstruction(uint64_t addr, uint8_t original);
    void handleBreakpointHit(pid_t tid);
};

} // namespace gdb
} // namespace tracesmith
