# TraceSmith GDB RSP Backend Integration Design

**Version**: 0.10.0  
**Status**: Design Phase  
**Author**: TraceSmith Team  
**Date**: 2025-12-05

## 1. Overview

This document describes the detailed design for integrating TraceSmith as a GDB Remote Serial Protocol (RSP) backend, enabling GPU debugging capabilities through standard GDB commands.

## 2. Requirements

### 2.1 Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-1 | Automatic GPU state capture at CPU breakpoints | High |
| FR-2 | GPU kernel call history query | High |
| FR-3 | GPU memory monitoring (allocation/free tracking) | High |
| FR-4 | Trace replay debugging | Medium |
| FR-5 | GPU breakpoints (kernel launch, memcpy, etc.) | High |
| FR-6 | GPU memory read/write | Medium |
| FR-7 | Integration with existing TraceSmith modules | High |

### 2.2 Non-Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-1 | Standard GDB RSP protocol compliance | High |
| NFR-2 | Cross-platform support (Linux, macOS) | Medium |
| NFR-3 | Low overhead during profiling | High |
| NFR-4 | Thread-safe operation | High |

## 3. Architecture

### 3.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        GDB Client                                   │
└─────────────────────────────┬───────────────────────────────────────┘
                              │ RSP over TCP/Unix Socket
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    TraceSmith GDB Stub                              │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    RSPHandler                                │   │
│  │  • Packet parsing/encoding                                   │   │
│  │  • Connection management                                     │   │
│  │  • Command dispatch                                          │   │
│  └─────────────────────────────────────────────────────────────┘   │
│  ┌──────────────────────┐  ┌────────────────────────────────────┐   │
│  │  ProcessController   │  │       GPUDebugEngine               │   │
│  │  • ptrace operations │  │  • GPU state management            │   │
│  │  • CPU breakpoints   │◄─►│  • GPU breakpoints                │   │
│  │  • Memory access     │  │  • Kernel history                  │   │
│  │  • Thread control    │  │  • Memory monitoring               │   │
│  └──────────────────────┘  │  • Trace capture/replay            │   │
│                            └────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    TraceSmith Core Libraries                        │
│  ┌─────────────┐ ┌──────────────┐ ┌─────────────┐ ┌──────────────┐  │
│  │ tracesmith- │ │ tracesmith-  │ │ tracesmith- │ │ tracesmith-  │  │
│  │ capture     │ │ state        │ │ replay      │ │ format       │  │
│  └─────────────┘ └──────────────┘ └─────────────┘ └──────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 Module Structure

```
include/tracesmith/gdb/
├── gdb_types.hpp           # Common types for GDB integration
├── rsp_packet.hpp          # RSP packet parser/encoder
├── rsp_handler.hpp         # RSP protocol handler
├── process_controller.hpp  # Process control via ptrace
└── gpu_debug_engine.hpp    # GPU debugging engine

src/gdb/
├── CMakeLists.txt
├── rsp_packet.cpp
├── rsp_handler.cpp
├── process_controller.cpp
└── gpu_debug_engine.cpp

tools/
└── tracesmith-gdbserver.cpp  # Main executable
```

## 4. Detailed Design

### 4.1 GDB Types (`gdb_types.hpp`)

```cpp
#pragma once

#include "tracesmith/common/types.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace tracesmith {
namespace gdb {

/// GDB signal codes
enum class Signal : int {
    None = 0,
    SIGHUP = 1,
    SIGINT = 2,
    SIGQUIT = 3,
    SIGTRAP = 5,
    SIGABRT = 6,
    SIGKILL = 9,
    SIGSEGV = 11,
    SIGTERM = 15,
    SIGSTOP = 19,
    SIGCONT = 18
};

/// Stop reason for target
enum class StopReason {
    None,
    Breakpoint,         // CPU breakpoint hit
    Watchpoint,         // Memory watchpoint triggered
    Signal,             // Signal received
    Exited,             // Process exited
    GPUBreakpoint,      // GPU breakpoint hit
    GPUEvent            // GPU event occurred
};

/// GPU breakpoint types
enum class GPUBreakpointType : uint8_t {
    KernelLaunch,       // Break on kernel launch
    KernelComplete,     // Break on kernel completion
    MemAlloc,           // Break on GPU memory allocation
    MemFree,            // Break on GPU memory free
    MemcpyH2D,          // Break on host-to-device copy
    MemcpyD2H,          // Break on device-to-host copy
    MemcpyD2D,          // Break on device-to-device copy
    Synchronize,        // Break on device synchronization
    AnyEvent            // Break on any GPU event
};

/// GPU breakpoint definition
struct GPUBreakpoint {
    int id = -1;
    GPUBreakpointType type = GPUBreakpointType::KernelLaunch;
    std::string kernel_pattern;   // Wildcard pattern for kernel name
    int device_id = -1;           // -1 means any device
    bool enabled = true;
    uint64_t hit_count = 0;
    
    bool matches(const TraceEvent& event) const;
};

/// GPU state snapshot
struct GPUStateSnapshot {
    Timestamp timestamp;
    std::vector<DeviceInfo> devices;
    
    // Memory state per device
    struct DeviceMemoryState {
        uint32_t device_id;
        uint64_t total_memory;
        uint64_t used_memory;
        uint64_t free_memory;
        size_t allocation_count;
    };
    std::vector<DeviceMemoryState> memory_states;
    
    // Stream states
    struct StreamState {
        uint32_t device_id;
        uint32_t stream_id;
        GPUState state;
        size_t pending_operations;
    };
    std::vector<StreamState> stream_states;
    
    // Active kernels
    std::vector<TraceEvent> active_kernels;
    
    // Recent events (last N)
    std::vector<TraceEvent> recent_events;
};

/// Kernel call info for history
struct KernelCallInfo {
    uint64_t call_id;
    std::string kernel_name;
    Timestamp launch_time;
    Timestamp complete_time;        // 0 if still running
    uint32_t device_id;
    uint32_t stream_id;
    KernelParams params;
    std::optional<CallStack> host_callstack;
    
    bool isComplete() const { return complete_time > 0; }
    Timestamp duration() const { 
        return isComplete() ? (complete_time - launch_time) : 0; 
    }
};

/// Stop event from target
struct StopEvent {
    StopReason reason = StopReason::None;
    Signal signal = Signal::None;
    int exit_code = 0;
    
    // For GPU events
    std::optional<TraceEvent> gpu_event;
    std::optional<GPUBreakpoint> gpu_breakpoint;
    
    // Location info
    uint64_t pc = 0;          // Program counter
    pid_t thread_id = 0;
    
    std::string description() const;
};

/// Trace replay control
struct ReplayControl {
    enum class Command {
        Start,
        Stop,
        Pause,
        Resume,
        StepEvent,
        StepKernel,
        GotoTimestamp,
        GotoEvent
    };
    
    Command command;
    uint64_t target_timestamp = 0;
    size_t target_event_index = 0;
};

/// Replay state
struct ReplayState {
    bool active = false;
    bool paused = false;
    size_t current_event_index = 0;
    size_t total_events = 0;
    Timestamp current_timestamp = 0;
    Timestamp total_duration = 0;
    std::string trace_file;
};

} // namespace gdb
} // namespace tracesmith
```

### 4.2 RSP Packet (`rsp_packet.hpp`)

```cpp
#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace tracesmith {
namespace gdb {

/// RSP packet types
enum class RSPPacketType {
    // Basic commands
    Unknown,
    Ack,                // '+'
    Nack,               // '-'
    Interrupt,          // '\x03'
    
    // Standard GDB commands
    ReadRegisters,      // 'g'
    WriteRegisters,     // 'G'
    ReadMemory,         // 'm'
    WriteMemory,        // 'M'
    BinaryWrite,        // 'X'
    Continue,           // 'c'
    ContinueSignal,     // 'C'
    Step,               // 's'
    StepSignal,         // 'S'
    Kill,               // 'k'
    Detach,             // 'D'
    
    // Breakpoint commands
    InsertBreakpoint,   // 'Z'
    RemoveBreakpoint,   // 'z'
    
    // Query commands
    Query,              // 'q'
    QuerySet,           // 'Q'
    
    // Extended commands
    ExtendedMode,       // '!'
    RestartReason,      // '?'
    ThreadAlive,        // 'T'
    SetThread,          // 'H'
    
    // vCommands
    VCommand            // 'v'
};

/// RSP breakpoint type codes
enum class RSPBreakpointType : int {
    Software = 0,       // Software breakpoint
    Hardware = 1,       // Hardware breakpoint
    WriteWatch = 2,     // Write watchpoint
    ReadWatch = 3,      // Read watchpoint
    AccessWatch = 4     // Access watchpoint
};

/// RSP packet parser and encoder
class RSPPacket {
public:
    RSPPacket() = default;
    explicit RSPPacket(const std::string& data);
    
    /// Encode data into RSP packet format: $<data>#<checksum>
    static std::string encode(const std::string& data);
    
    /// Decode RSP packet, returns nullopt if invalid
    static std::optional<std::string> decode(const std::string& packet);
    
    /// Calculate checksum for data
    static uint8_t checksum(const std::string& data);
    
    /// Parse packet type from decoded data
    static RSPPacketType parseType(const std::string& data);
    
    /// Encode standard responses
    static std::string ok();                           // "OK"
    static std::string error(int code);                // "E<code>"
    static std::string empty();                        // ""
    static std::string stopReply(int signal);          // "S<signal>"
    static std::string stopReplyThread(int signal, pid_t tid);  // "T<signal>thread:<tid>;"
    static std::string exitReply(int code);            // "W<code>"
    
    /// Encode hex data
    static std::string toHex(const std::vector<uint8_t>& data);
    static std::string toHex(const std::string& str);
    static std::string toHex(uint64_t value, int width = 0);
    
    /// Decode hex data
    static std::vector<uint8_t> fromHex(const std::string& hex);
    static uint64_t hexToUint64(const std::string& hex);
    
    /// Escape binary data
    static std::string escapeBinary(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> unescapeBinary(const std::string& data);

private:
    std::string data_;
};

/// RSP query parser
struct RSPQuery {
    std::string name;
    std::vector<std::string> args;
    
    static RSPQuery parse(const std::string& query);
};

} // namespace gdb
} // namespace tracesmith
```

### 4.3 RSP Handler (`rsp_handler.hpp`)

```cpp
#pragma once

#include "tracesmith/gdb/gdb_types.hpp"
#include "tracesmith/gdb/rsp_packet.hpp"
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <thread>

namespace tracesmith {
namespace gdb {

// Forward declarations
class ProcessController;
class GPUDebugEngine;

/// RSP Handler configuration
struct RSPConfig {
    int port = 1234;
    std::string unix_socket;        // If set, use Unix socket instead of TCP
    bool verbose = false;
    bool enable_gpu_extensions = true;
    size_t max_packet_size = 4096;
};

/// RSP protocol handler
class RSPHandler {
public:
    explicit RSPHandler(const RSPConfig& config = RSPConfig{});
    ~RSPHandler();
    
    // Non-copyable
    RSPHandler(const RSPHandler&) = delete;
    RSPHandler& operator=(const RSPHandler&) = delete;
    
    /// Initialize with target process
    bool initialize(pid_t pid);
    bool initialize(const std::vector<std::string>& args);
    
    /// Start listening for connections
    bool listen();
    
    /// Run main event loop (blocking)
    void run();
    
    /// Stop the handler
    void stop();
    
    /// Check if running
    bool isRunning() const { return running_; }
    
    /// Get process controller
    ProcessController* processController() { return process_.get(); }
    
    /// Get GPU debug engine
    GPUDebugEngine* gpuEngine() { return gpu_engine_.get(); }

private:
    RSPConfig config_;
    std::atomic<bool> running_{false};
    
    int server_fd_ = -1;
    int client_fd_ = -1;
    
    std::unique_ptr<ProcessController> process_;
    std::unique_ptr<GPUDebugEngine> gpu_engine_;
    
    // Packet handling
    std::string receivePacket();
    bool sendPacket(const std::string& data);
    bool sendRaw(const std::string& data);
    
    // Command handlers
    std::string handlePacket(const std::string& packet);
    std::string handleQuery(const std::string& query);
    std::string handleQuerySet(const std::string& query);
    std::string handleVCommand(const std::string& cmd);
    std::string handleMonitor(const std::string& cmd);
    
    // Standard GDB commands
    std::string handleReadRegisters();
    std::string handleWriteRegisters(const std::string& data);
    std::string handleReadMemory(uint64_t addr, size_t len);
    std::string handleWriteMemory(uint64_t addr, const std::string& data);
    std::string handleContinue(int signal = 0);
    std::string handleStep(int signal = 0);
    std::string handleBreakpoint(char op, int type, uint64_t addr, int kind);
    std::string handleStopReason();
    std::string handleThreadOps(char op, pid_t tid);
    
    // TraceSmith extensions (via monitor commands)
    std::string handleGPUStatus();
    std::string handleGPUDevices();
    std::string handleGPUMemory(int device_id);
    std::string handleGPUKernels(int count);
    std::string handleGPUStreams();
    std::string handleGPUBreakpoint(const std::string& args);
    std::string handleGPUMemoryRead(int device, uint64_t addr, size_t len);
    std::string handleTraceStart();
    std::string handleTraceStop();
    std::string handleTraceSave(const std::string& filename);
    std::string handleTraceLoad(const std::string& filename);
    std::string handleReplayControl(const std::string& cmd);
    
    // Helper functions
    void waitForTarget();
    StopEvent waitForStop();
    std::string formatStopReply(const StopEvent& event);
};

} // namespace gdb
} // namespace tracesmith
```

### 4.4 Process Controller (`process_controller.hpp`)

```cpp
#pragma once

#include "tracesmith/gdb/gdb_types.hpp"
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <sys/types.h>

namespace tracesmith {
namespace gdb {

/// CPU register set (x86_64)
struct RegisterSet {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cs, ss, ds, es, fs, gs;
    
    // Serialize to GDB format (hex string)
    std::string toHex() const;
    
    // Deserialize from GDB format
    static RegisterSet fromHex(const std::string& hex);
    
    // Get register count
    static constexpr size_t count() { return 24; }
};

/// Software breakpoint info
struct Breakpoint {
    int id = -1;
    uint64_t address = 0;
    uint8_t original_byte = 0;    // Saved instruction byte
    bool enabled = true;
    uint64_t hit_count = 0;
};

/// Process controller using ptrace
class ProcessController {
public:
    ProcessController();
    ~ProcessController();
    
    // Non-copyable
    ProcessController(const ProcessController&) = delete;
    ProcessController& operator=(const ProcessController&) = delete;
    
    /// Attach to existing process
    bool attach(pid_t pid);
    
    /// Spawn new process
    bool spawn(const std::vector<std::string>& args);
    
    /// Detach from process
    bool detach();
    
    /// Kill process
    bool kill();
    
    /// Check if attached
    bool isAttached() const { return pid_ > 0; }
    
    /// Get target PID
    pid_t pid() const { return pid_; }
    
    // Execution control
    
    /// Continue execution
    bool continueExecution(int signal = 0);
    
    /// Single step
    bool singleStep(int signal = 0);
    
    /// Interrupt (send SIGSTOP)
    bool interrupt();
    
    /// Wait for target to stop
    StopEvent waitForStop();
    
    // Thread control
    
    /// Get list of threads
    std::vector<pid_t> getThreads() const;
    
    /// Get current thread
    pid_t currentThread() const { return current_thread_; }
    
    /// Select thread for operations
    bool selectThread(pid_t tid);
    
    /// Check if thread is alive
    bool isThreadAlive(pid_t tid) const;
    
    // Register access
    
    /// Read all registers
    RegisterSet readRegisters();
    
    /// Write all registers
    bool writeRegisters(const RegisterSet& regs);
    
    /// Read single register
    uint64_t readRegister(int reg_num);
    
    /// Write single register
    bool writeRegister(int reg_num, uint64_t value);
    
    // Memory access
    
    /// Read memory
    std::vector<uint8_t> readMemory(uint64_t addr, size_t len);
    
    /// Write memory
    bool writeMemory(uint64_t addr, const std::vector<uint8_t>& data);
    
    // Breakpoints
    
    /// Set software breakpoint
    int setBreakpoint(uint64_t addr);
    
    /// Remove breakpoint
    bool removeBreakpoint(int bp_id);
    
    /// Remove breakpoint by address
    bool removeBreakpointAt(uint64_t addr);
    
    /// Enable/disable breakpoint
    bool enableBreakpoint(int bp_id, bool enable);
    
    /// Get breakpoint info
    const Breakpoint* getBreakpoint(int bp_id) const;
    
    /// List all breakpoints
    std::vector<Breakpoint> listBreakpoints() const;
    
    /// Check if address has breakpoint
    bool hasBreakpointAt(uint64_t addr) const;
    
    // Callback for GPU events (called during wait)
    using GPUEventCallback = std::function<void(const TraceEvent&)>;
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
    
    // Helper functions
    bool ptraceOp(int request, pid_t tid, void* addr, void* data);
    void updateThreadList();
    bool insertBreakpointInstruction(uint64_t addr, uint8_t& original);
    bool removeBreakpointInstruction(uint64_t addr, uint8_t original);
};

} // namespace gdb
} // namespace tracesmith
```

### 4.5 GPU Debug Engine (`gpu_debug_engine.hpp`)

```cpp
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

/// GPU debug engine - integrates TraceSmith profiling with GDB debugging
class GPUDebugEngine {
public:
    explicit GPUDebugEngine(const GPUDebugConfig& config = GPUDebugConfig{});
    ~GPUDebugEngine();
    
    // Non-copyable
    GPUDebugEngine(const GPUDebugEngine&) = delete;
    GPUDebugEngine& operator=(const GPUDebugEngine&) = delete;
    
    /// Initialize for target process
    bool initialize(pid_t target_pid);
    
    /// Finalize and cleanup
    void finalize();
    
    /// Check if initialized
    bool isInitialized() const { return initialized_; }
    
    // GPU State
    
    /// Get current GPU state snapshot
    GPUStateSnapshot getGPUState();
    
    /// Get device information
    std::vector<DeviceInfo> getDevices() const;
    
    /// Get memory usage for device
    MemoryProfiler::MemorySnapshot getMemoryUsage(int device_id = -1);
    
    /// Get stream states
    std::vector<GPUStateSnapshot::StreamState> getStreamStates();
    
    // Kernel History
    
    /// Get recent kernel calls
    std::vector<KernelCallInfo> getKernelHistory(size_t count = 100);
    
    /// Get currently running kernels
    std::vector<KernelCallInfo> getActiveKernels();
    
    /// Get kernel by name pattern
    std::vector<KernelCallInfo> findKernels(const std::string& pattern);
    
    // GPU Breakpoints
    
    /// Set GPU breakpoint
    int setGPUBreakpoint(const GPUBreakpoint& bp);
    
    /// Remove GPU breakpoint
    bool removeGPUBreakpoint(int bp_id);
    
    /// Enable/disable GPU breakpoint
    bool enableGPUBreakpoint(int bp_id, bool enable);
    
    /// List GPU breakpoints
    std::vector<GPUBreakpoint> listGPUBreakpoints() const;
    
    /// Check event against breakpoints
    std::optional<GPUBreakpoint> checkBreakpoints(const TraceEvent& event);
    
    // GPU Memory Access
    
    /// Read GPU memory (returns empty on failure)
    std::vector<uint8_t> readGPUMemory(int device, uint64_t addr, size_t len);
    
    /// Write GPU memory
    bool writeGPUMemory(int device, uint64_t addr, const std::vector<uint8_t>& data);
    
    /// Get memory allocations
    std::vector<MemoryProfiler::AllocationInfo> getMemoryAllocations(int device = -1);
    
    // Trace Capture
    
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
    
    // Trace Replay
    
    /// Load trace for replay
    bool loadTrace(const std::string& filename);
    
    /// Get replay state
    ReplayState getReplayState() const;
    
    /// Control replay
    bool controlReplay(const ReplayControl& control);
    
    /// Get current replay event
    std::optional<TraceEvent> getCurrentReplayEvent();
    
    // Event Callback (for GPU breakpoints)
    
    using EventCallback = std::function<void(const TraceEvent&, const GPUBreakpoint*)>;
    void setEventCallback(EventCallback callback);
    
    // Integration with process stop
    
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
    
    // Replay state
    ReplayState replay_state_;
    std::vector<TraceEvent> replay_events_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Callback
    EventCallback event_callback_;
    
    // Internal event handler
    void handleEvent(const TraceEvent& event);
    void addToKernelHistory(const TraceEvent& event);
    bool matchesPattern(const std::string& name, const std::string& pattern);
};

} // namespace gdb
} // namespace tracesmith
```

## 5. GDB Extension Commands

### 5.1 Monitor Commands

All TraceSmith extensions are accessed via GDB's `monitor` command:

```
(gdb) monitor ts help

TraceSmith GDB Extensions v0.10.0
=================================

GPU Status & Info:
  monitor ts status              Show GPU status summary
  monitor ts devices             List all GPU devices
  monitor ts memory [DEV]        Show GPU memory usage
  monitor ts streams             Show stream states

Kernel History:
  monitor ts kernels [N]         Show last N kernel calls (default: 10)
  monitor ts kernel-search PAT   Search kernels by name pattern

GPU Breakpoints:
  monitor ts break kernel NAME   Break on kernel launch (wildcard *)
  monitor ts break complete NAME Break on kernel completion
  monitor ts break memcpy [DIR]  Break on memcpy (h2d/d2h/d2d/any)
  monitor ts break alloc         Break on memory allocation
  monitor ts break free          Break on memory free
  monitor ts break sync          Break on synchronization
  monitor ts break list          List all GPU breakpoints
  monitor ts break delete N      Delete GPU breakpoint N
  monitor ts break enable N      Enable GPU breakpoint N
  monitor ts break disable N     Disable GPU breakpoint N

GPU Memory:
  monitor ts gpu read DEV ADDR LEN   Read GPU memory
  monitor ts gpu write DEV ADDR DATA Write GPU memory
  monitor ts gpu allocs [DEV]        List memory allocations

Trace Capture:
  monitor ts trace start         Start capturing GPU events
  monitor ts trace stop          Stop capturing
  monitor ts trace save FILE     Save trace to file
  monitor ts trace load FILE     Load trace for replay

Trace Replay:
  monitor ts replay start        Start trace replay
  monitor ts replay stop         Stop replay
  monitor ts replay pause        Pause replay
  monitor ts replay resume       Resume replay
  monitor ts replay step         Replay next event
  monitor ts replay step-kernel  Replay to next kernel
  monitor ts replay goto TS      Go to timestamp (ns)
  monitor ts replay status       Show replay status
```

## 6. Test Plan

### 6.1 Unit Tests

#### 6.1.1 RSP Packet Tests (`test_rsp_packet.cpp`)

```cpp
// Test RSP packet encoding
TEST(RSPPacketTest, EncodeBasic)
TEST(RSPPacketTest, EncodeEmpty)
TEST(RSPPacketTest, EncodeSpecialChars)
TEST(RSPPacketTest, Checksum)

// Test RSP packet decoding
TEST(RSPPacketTest, DecodeBasic)
TEST(RSPPacketTest, DecodeInvalid)
TEST(RSPPacketTest, DecodeChecksumMismatch)

// Test hex conversion
TEST(RSPPacketTest, ToHexBytes)
TEST(RSPPacketTest, ToHexString)
TEST(RSPPacketTest, ToHexUint64)
TEST(RSPPacketTest, FromHex)
TEST(RSPPacketTest, HexToUint64)

// Test binary escape
TEST(RSPPacketTest, EscapeBinary)
TEST(RSPPacketTest, UnescapeBinary)

// Test packet type parsing
TEST(RSPPacketTest, ParseTypeBasic)
TEST(RSPPacketTest, ParseTypeQuery)
TEST(RSPPacketTest, ParseTypeBreakpoint)

// Test response encoding
TEST(RSPPacketTest, ResponseOK)
TEST(RSPPacketTest, ResponseError)
TEST(RSPPacketTest, ResponseStopReply)
```

#### 6.1.2 GPU Debug Engine Tests (`test_gpu_debug_engine.cpp`)

```cpp
// Test GPU breakpoint matching
TEST(GPUDebugEngineTest, BreakpointMatchKernelExact)
TEST(GPUDebugEngineTest, BreakpointMatchKernelWildcard)
TEST(GPUDebugEngineTest, BreakpointMatchMemcpy)
TEST(GPUDebugEngineTest, BreakpointMatchDevice)
TEST(GPUDebugEngineTest, BreakpointDisabled)

// Test kernel history
TEST(GPUDebugEngineTest, KernelHistoryAdd)
TEST(GPUDebugEngineTest, KernelHistoryLimit)
TEST(GPUDebugEngineTest, KernelHistorySearch)

// Test GPU state
TEST(GPUDebugEngineTest, GetGPUState)
TEST(GPUDebugEngineTest, StreamStates)
TEST(GPUDebugEngineTest, MemoryState)

// Test replay
TEST(GPUDebugEngineTest, ReplayLoad)
TEST(GPUDebugEngineTest, ReplayStep)
TEST(GPUDebugEngineTest, ReplayGotoTimestamp)
```

#### 6.1.3 GDB Types Tests (`test_gdb_types.cpp`)

```cpp
// Test GPU breakpoint
TEST(GDBTypesTest, GPUBreakpointDefault)
TEST(GDBTypesTest, GPUBreakpointMatches)

// Test GPU state snapshot
TEST(GDBTypesTest, GPUStateSnapshotDefault)

// Test kernel call info
TEST(GDBTypesTest, KernelCallInfoDuration)
TEST(GDBTypesTest, KernelCallInfoIsComplete)

// Test stop event
TEST(GDBTypesTest, StopEventDescription)

// Test replay control
TEST(GDBTypesTest, ReplayControlCommands)
```

### 6.2 Integration Tests

```cpp
// Test RSP handler with mock client
TEST(RSPHandlerTest, HandleReadRegisters)
TEST(RSPHandlerTest, HandleReadMemory)
TEST(RSPHandlerTest, HandleBreakpoint)
TEST(RSPHandlerTest, HandleMonitorStatus)
TEST(RSPHandlerTest, HandleMonitorKernels)
TEST(RSPHandlerTest, HandleMonitorGPUBreakpoint)
```

## 7. Usage Example

```bash
# Terminal 1: Start TraceSmith GDB Server
$ tracesmith-gdbserver --port 1234 -- ./my_cuda_app

# Terminal 2: Connect with GDB
$ gdb ./my_cuda_app
(gdb) target remote :1234
Remote debugging using :1234

(gdb) break main
Breakpoint 1 at 0x401234: file main.cu, line 10.

(gdb) monitor ts status
GPU Status:
  Platform: CUDA
  Devices: 1
  Device 0: NVIDIA GeForce RTX 4090 (24576 MB)
  Active Streams: 0
  Memory Used: 0 MB

(gdb) monitor ts trace start
GPU trace capture started.

(gdb) monitor ts break kernel matmul*
GPU breakpoint 1: kernel launch "matmul*"

(gdb) continue
Continuing.

GPU breakpoint 1 hit: kernel launch "matmul_f32"
  Grid: (256, 256, 1)
  Block: (16, 16, 1)
  Device: 0, Stream: 0

(gdb) monitor ts kernels 5
Kernel History (last 5):
  #1 [0.001ms] relu_kernel <<<(1024,1,1), (256,1,1)>>> 12µs
  #2 [0.015ms] conv2d_f32  <<<(64,64,1), (16,16,1)>>> 234µs
  #3 [0.250ms] matmul_f32  <<<(256,256,1), (16,16,1)>>> [running]

(gdb) monitor ts memory
GPU Memory Usage:
  Device 0: 128.00 MB / 24576.00 MB (0.5%)
  Allocations: 12
  
(gdb) continue
```

## 8. Approval Required

**请审核以上设计文档。如果批准，我将进入 Development Phase 开始实现。**

需要确认的关键设计决策：

1. **模块划分**: RSPHandler, ProcessController, GPUDebugEngine 三层架构
2. **扩展命令**: 通过 `monitor ts <cmd>` 访问所有 TraceSmith 功能
3. **GPU 断点类型**: kernel/memcpy/alloc/free/sync 事件断点
4. **Trace Replay**: 在 GDB 中控制 trace 回放调试
5. **测试策略**: 先写单元测试，再实现功能

