/**
 * @file process_controller.cpp
 * @brief Implementation of Process Controller using ptrace
 */

#include "tracesmith/gdb/process_controller.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <dirent.h>

#ifdef TRACESMITH_PLATFORM_LINUX
#include <sys/ptrace.h>
#include <sys/user.h>
#endif

#ifdef TRACESMITH_PLATFORM_MACOS
#include <mach/mach.h>
#include <mach/task.h>
#endif

namespace tracesmith {
namespace gdb {

// ============================================================
// RegisterSet Implementation
// ============================================================

std::string RegisterSet::toHex() const {
    std::ostringstream oss;
    
    // GDB expects registers in specific order with little-endian byte order
    auto writeReg = [&oss](uint64_t val) {
        for (int i = 0; i < 8; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') 
                << ((val >> (i * 8)) & 0xFF);
        }
    };
    
    writeReg(rax);
    writeReg(rbx);
    writeReg(rcx);
    writeReg(rdx);
    writeReg(rsi);
    writeReg(rdi);
    writeReg(rbp);
    writeReg(rsp);
    writeReg(r8);
    writeReg(r9);
    writeReg(r10);
    writeReg(r11);
    writeReg(r12);
    writeReg(r13);
    writeReg(r14);
    writeReg(r15);
    writeReg(rip);
    writeReg(rflags);
    writeReg(cs);
    writeReg(ss);
    writeReg(ds);
    writeReg(es);
    writeReg(fs);
    writeReg(gs);
    
    return oss.str();
}

RegisterSet RegisterSet::fromHex(const std::string& hex) {
    RegisterSet regs;
    
    auto readReg = [&hex](size_t offset) -> uint64_t {
        if (offset + 16 > hex.size()) return 0;
        
        uint64_t val = 0;
        for (int i = 0; i < 8; ++i) {
            std::string byte_str = hex.substr(offset + i * 2, 2);
            try {
                val |= static_cast<uint64_t>(std::stoi(byte_str, nullptr, 16)) << (i * 8);
            } catch (...) {
                // Invalid hex
            }
        }
        return val;
    };
    
    size_t off = 0;
    regs.rax = readReg(off); off += 16;
    regs.rbx = readReg(off); off += 16;
    regs.rcx = readReg(off); off += 16;
    regs.rdx = readReg(off); off += 16;
    regs.rsi = readReg(off); off += 16;
    regs.rdi = readReg(off); off += 16;
    regs.rbp = readReg(off); off += 16;
    regs.rsp = readReg(off); off += 16;
    regs.r8 = readReg(off); off += 16;
    regs.r9 = readReg(off); off += 16;
    regs.r10 = readReg(off); off += 16;
    regs.r11 = readReg(off); off += 16;
    regs.r12 = readReg(off); off += 16;
    regs.r13 = readReg(off); off += 16;
    regs.r14 = readReg(off); off += 16;
    regs.r15 = readReg(off); off += 16;
    regs.rip = readReg(off); off += 16;
    regs.rflags = readReg(off); off += 16;
    regs.cs = readReg(off); off += 16;
    regs.ss = readReg(off); off += 16;
    regs.ds = readReg(off); off += 16;
    regs.es = readReg(off); off += 16;
    regs.fs = readReg(off); off += 16;
    regs.gs = readReg(off);
    
    return regs;
}

// ============================================================
// ProcessController Implementation
// ============================================================

ProcessController::ProcessController() = default;

ProcessController::~ProcessController() {
    if (isAttached()) {
        detach();
    }
}

bool ProcessController::attach(pid_t pid) {
    if (isAttached()) {
        return false;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
        return false;
    }
    
    // Wait for process to stop
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        return false;
    }
    
    if (!WIFSTOPPED(status)) {
        ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        return false;
    }
    
    // Enable PTRACE_O_TRACECLONE to follow threads
    ptrace(PTRACE_SETOPTIONS, pid, nullptr, 
           PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK);
    
    pid_ = pid;
    current_thread_ = pid;
    attached_ = true;
    
    updateThreadList();
    return true;
#else
    (void)pid;
    return false;  // Not supported on this platform
#endif
}

bool ProcessController::spawn(const std::vector<std::string>& args) {
    if (isAttached() || args.empty()) {
        return false;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    pid_t pid = fork();
    
    if (pid < 0) {
        return false;
    }
    
    if (pid == 0) {
        // Child process
        ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
        
        // Build argv
        std::vector<char*> argv;
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(argv[0], argv.data());
        _exit(127);  // exec failed
    }
    
    // Parent process
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return false;
    }
    
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) {
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        return false;
    }
    
    ptrace(PTRACE_SETOPTIONS, pid, nullptr,
           PTRACE_O_TRACECLONE | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK);
    
    pid_ = pid;
    current_thread_ = pid;
    attached_ = true;
    threads_.insert(pid);
    
    return true;
#else
    (void)args;
    return false;
#endif
}

bool ProcessController::detach() {
    if (!isAttached()) {
        return false;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    // Remove all breakpoints first
    for (const auto& [id, bp] : breakpoints_) {
        if (bp.enabled) {
            removeBreakpointInstruction(bp.address, bp.original_byte);
        }
    }
    breakpoints_.clear();
    addr_to_bp_.clear();
    
    // Detach from all threads
    for (pid_t tid : threads_) {
        ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
    }
    
    pid_ = 0;
    current_thread_ = 0;
    attached_ = false;
    threads_.clear();
    
    return true;
#else
    return false;
#endif
}

bool ProcessController::kill() {
    if (!isAttached()) {
        return false;
    }
    
    ::kill(pid_, SIGKILL);
    
    int status;
    waitpid(pid_, &status, 0);
    
    pid_ = 0;
    current_thread_ = 0;
    attached_ = false;
    threads_.clear();
    breakpoints_.clear();
    addr_to_bp_.clear();
    
    return true;
}

// ============================================================
// Execution Control
// ============================================================

bool ProcessController::continueExecution(int signal) {
    if (!isAttached()) {
        return false;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    return ptrace(PTRACE_CONT, current_thread_, nullptr, 
                  reinterpret_cast<void*>(static_cast<long>(signal))) >= 0;
#else
    (void)signal;
    return false;
#endif
}

bool ProcessController::singleStep(int signal) {
    if (!isAttached()) {
        return false;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    return ptrace(PTRACE_SINGLESTEP, current_thread_, nullptr,
                  reinterpret_cast<void*>(static_cast<long>(signal))) >= 0;
#else
    (void)signal;
    return false;
#endif
}

bool ProcessController::interrupt() {
    if (!isAttached()) {
        return false;
    }
    
    return ::kill(pid_, SIGSTOP) == 0;
}

StopEvent ProcessController::waitForStop() {
    StopEvent event;
    
    if (!isAttached()) {
        return event;
    }
    
    int status;
#ifdef TRACESMITH_PLATFORM_LINUX
    pid_t stopped_pid = waitpid(-1, &status, __WALL);
#else
    pid_t stopped_pid = waitpid(-1, &status, 0);
#endif
    
    if (stopped_pid < 0) {
        return event;
    }
    
    event.thread_id = stopped_pid;
    
    if (WIFEXITED(status)) {
        event.reason = StopReason::Exited;
        event.exit_code = WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status)) {
        event.reason = StopReason::Signal;
        event.signal = static_cast<Signal>(WTERMSIG(status));
    }
    else if (WIFSTOPPED(status)) {
        int sig = WSTOPSIG(status);
        
        if (sig == SIGTRAP) {
            // Check if it's a breakpoint
            RegisterSet regs = readRegisters();
            event.pc = regs.rip;
            
            // Adjust PC for breakpoint (int3 already executed)
            uint64_t bp_addr = regs.rip - 1;
            
            if (hasBreakpointAt(bp_addr)) {
                event.reason = StopReason::Breakpoint;
                event.pc = bp_addr;
                
                // Rewind PC to breakpoint address
                regs.rip = bp_addr;
                writeRegisters(regs);
                
                handleBreakpointHit(stopped_pid);
            } else {
                event.reason = StopReason::Signal;
                event.signal = Signal::Sig_TRAP;
            }
        } else {
            event.reason = StopReason::Signal;
            event.signal = static_cast<Signal>(sig);
        }
    }
    
    current_thread_ = stopped_pid;
    return event;
}

// ============================================================
// Thread Control
// ============================================================

std::vector<pid_t> ProcessController::getThreads() const {
    updateThreadList();
    return std::vector<pid_t>(threads_.begin(), threads_.end());
}

bool ProcessController::selectThread(pid_t tid) {
    updateThreadList();
    
    if (threads_.find(tid) == threads_.end()) {
        return false;
    }
    
    current_thread_ = tid;
    return true;
}

bool ProcessController::isThreadAlive(pid_t tid) const {
    updateThreadList();
    return threads_.find(tid) != threads_.end();
}

void ProcessController::updateThreadList() const {
#ifdef TRACESMITH_PLATFORM_LINUX
    threads_.clear();
    
    std::string task_dir = "/proc/" + std::to_string(pid_) + "/task";
    DIR* dir = opendir(task_dir.c_str());
    
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] != '.') {
                try {
                    pid_t tid = std::stoi(entry->d_name);
                    threads_.insert(tid);
                } catch (...) {
                    // Ignore invalid entries
                }
            }
        }
        closedir(dir);
    }
    
    // Ensure main thread is in the list
    threads_.insert(pid_);
#endif
}

// ============================================================
// Register Access
// ============================================================

RegisterSet ProcessController::readRegisters() {
    RegisterSet regs;
    
    if (!isAttached()) {
        return regs;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    struct user_regs_struct linux_regs;
    if (ptrace(PTRACE_GETREGS, current_thread_, nullptr, &linux_regs) >= 0) {
        regs.rax = linux_regs.rax;
        regs.rbx = linux_regs.rbx;
        regs.rcx = linux_regs.rcx;
        regs.rdx = linux_regs.rdx;
        regs.rsi = linux_regs.rsi;
        regs.rdi = linux_regs.rdi;
        regs.rbp = linux_regs.rbp;
        regs.rsp = linux_regs.rsp;
        regs.r8 = linux_regs.r8;
        regs.r9 = linux_regs.r9;
        regs.r10 = linux_regs.r10;
        regs.r11 = linux_regs.r11;
        regs.r12 = linux_regs.r12;
        regs.r13 = linux_regs.r13;
        regs.r14 = linux_regs.r14;
        regs.r15 = linux_regs.r15;
        regs.rip = linux_regs.rip;
        regs.rflags = linux_regs.eflags;
        regs.cs = linux_regs.cs;
        regs.ss = linux_regs.ss;
        regs.ds = linux_regs.ds;
        regs.es = linux_regs.es;
        regs.fs = linux_regs.fs;
        regs.gs = linux_regs.gs;
    }
#endif
    
    return regs;
}

bool ProcessController::writeRegisters(const RegisterSet& regs) {
    if (!isAttached()) {
        return false;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    struct user_regs_struct linux_regs;
    
    // First read current values
    if (ptrace(PTRACE_GETREGS, current_thread_, nullptr, &linux_regs) < 0) {
        return false;
    }
    
    linux_regs.rax = regs.rax;
    linux_regs.rbx = regs.rbx;
    linux_regs.rcx = regs.rcx;
    linux_regs.rdx = regs.rdx;
    linux_regs.rsi = regs.rsi;
    linux_regs.rdi = regs.rdi;
    linux_regs.rbp = regs.rbp;
    linux_regs.rsp = regs.rsp;
    linux_regs.r8 = regs.r8;
    linux_regs.r9 = regs.r9;
    linux_regs.r10 = regs.r10;
    linux_regs.r11 = regs.r11;
    linux_regs.r12 = regs.r12;
    linux_regs.r13 = regs.r13;
    linux_regs.r14 = regs.r14;
    linux_regs.r15 = regs.r15;
    linux_regs.rip = regs.rip;
    linux_regs.eflags = regs.rflags;
    linux_regs.cs = regs.cs;
    linux_regs.ss = regs.ss;
    linux_regs.ds = regs.ds;
    linux_regs.es = regs.es;
    linux_regs.fs = regs.fs;
    linux_regs.gs = regs.gs;
    
    return ptrace(PTRACE_SETREGS, current_thread_, nullptr, &linux_regs) >= 0;
#else
    (void)regs;
    return false;
#endif
}

uint64_t ProcessController::readRegister(int reg_num) {
    RegisterSet regs = readRegisters();
    
    switch (reg_num) {
        case 0: return regs.rax;
        case 1: return regs.rbx;
        case 2: return regs.rcx;
        case 3: return regs.rdx;
        case 4: return regs.rsi;
        case 5: return regs.rdi;
        case 6: return regs.rbp;
        case 7: return regs.rsp;
        case 8: return regs.r8;
        case 9: return regs.r9;
        case 10: return regs.r10;
        case 11: return regs.r11;
        case 12: return regs.r12;
        case 13: return regs.r13;
        case 14: return regs.r14;
        case 15: return regs.r15;
        case 16: return regs.rip;
        case 17: return regs.rflags;
        default: return 0;
    }
}

bool ProcessController::writeRegister(int reg_num, uint64_t value) {
    RegisterSet regs = readRegisters();
    
    switch (reg_num) {
        case 0: regs.rax = value; break;
        case 1: regs.rbx = value; break;
        case 2: regs.rcx = value; break;
        case 3: regs.rdx = value; break;
        case 4: regs.rsi = value; break;
        case 5: regs.rdi = value; break;
        case 6: regs.rbp = value; break;
        case 7: regs.rsp = value; break;
        case 8: regs.r8 = value; break;
        case 9: regs.r9 = value; break;
        case 10: regs.r10 = value; break;
        case 11: regs.r11 = value; break;
        case 12: regs.r12 = value; break;
        case 13: regs.r13 = value; break;
        case 14: regs.r14 = value; break;
        case 15: regs.r15 = value; break;
        case 16: regs.rip = value; break;
        case 17: regs.rflags = value; break;
        default: return false;
    }
    
    return writeRegisters(regs);
}

// ============================================================
// Memory Access
// ============================================================

std::vector<uint8_t> ProcessController::readMemory(uint64_t addr, size_t len) {
    std::vector<uint8_t> result;
    
    if (!isAttached() || len == 0) {
        return result;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    result.resize(len);
    
    // Read word by word
    size_t offset = 0;
    while (offset < len) {
        errno = 0;
        long word = ptrace(PTRACE_PEEKDATA, current_thread_, 
                          reinterpret_cast<void*>(addr + offset), nullptr);
        
        if (errno != 0) {
            result.resize(offset);
            break;
        }
        
        // Copy bytes from word to result
        size_t bytes_to_copy = std::min(sizeof(long), len - offset);
        memcpy(result.data() + offset, &word, bytes_to_copy);
        offset += sizeof(long);
    }
#else
    (void)addr;
    (void)len;
#endif
    
    return result;
}

bool ProcessController::writeMemory(uint64_t addr, const std::vector<uint8_t>& data) {
    if (!isAttached() || data.empty()) {
        return false;
    }
    
#ifdef TRACESMITH_PLATFORM_LINUX
    size_t offset = 0;
    
    while (offset < data.size()) {
        // For partial writes, we need to read-modify-write
        long word;
        
        size_t remaining = data.size() - offset;
        if (remaining < sizeof(long)) {
            errno = 0;
            word = ptrace(PTRACE_PEEKDATA, current_thread_,
                         reinterpret_cast<void*>(addr + offset), nullptr);
            if (errno != 0) {
                return false;
            }
        }
        
        memcpy(&word, data.data() + offset, std::min(sizeof(long), remaining));
        
        if (ptrace(PTRACE_POKEDATA, current_thread_,
                   reinterpret_cast<void*>(addr + offset),
                   reinterpret_cast<void*>(word)) < 0) {
            return false;
        }
        
        offset += sizeof(long);
    }
    
    return true;
#else
    (void)addr;
    (void)data;
    return false;
#endif
}

// ============================================================
// Breakpoints
// ============================================================

int ProcessController::setBreakpoint(uint64_t addr) {
    if (!isAttached()) {
        return -1;
    }
    
    // Check if breakpoint already exists at this address
    auto it = addr_to_bp_.find(addr);
    if (it != addr_to_bp_.end()) {
        return it->second;  // Return existing breakpoint ID
    }
    
    Breakpoint bp;
    bp.id = next_bp_id_++;
    bp.address = addr;
    bp.enabled = true;
    bp.hit_count = 0;
    
    if (!insertBreakpointInstruction(addr, bp.original_byte)) {
        return -1;
    }
    
    breakpoints_[bp.id] = bp;
    addr_to_bp_[addr] = bp.id;
    
    return bp.id;
}

bool ProcessController::removeBreakpoint(int bp_id) {
    auto it = breakpoints_.find(bp_id);
    if (it == breakpoints_.end()) {
        return false;
    }
    
    const Breakpoint& bp = it->second;
    
    if (bp.enabled) {
        removeBreakpointInstruction(bp.address, bp.original_byte);
    }
    
    addr_to_bp_.erase(bp.address);
    breakpoints_.erase(it);
    
    return true;
}

bool ProcessController::removeBreakpointAt(uint64_t addr) {
    auto it = addr_to_bp_.find(addr);
    if (it == addr_to_bp_.end()) {
        return false;
    }
    
    return removeBreakpoint(it->second);
}

bool ProcessController::enableBreakpoint(int bp_id, bool enable) {
    auto it = breakpoints_.find(bp_id);
    if (it == breakpoints_.end()) {
        return false;
    }
    
    Breakpoint& bp = it->second;
    
    if (bp.enabled == enable) {
        return true;  // Already in desired state
    }
    
    if (enable) {
        if (!insertBreakpointInstruction(bp.address, bp.original_byte)) {
            return false;
        }
    } else {
        if (!removeBreakpointInstruction(bp.address, bp.original_byte)) {
            return false;
        }
    }
    
    bp.enabled = enable;
    return true;
}

const Breakpoint* ProcessController::getBreakpoint(int bp_id) const {
    auto it = breakpoints_.find(bp_id);
    if (it == breakpoints_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<Breakpoint> ProcessController::listBreakpoints() const {
    std::vector<Breakpoint> result;
    result.reserve(breakpoints_.size());
    
    for (const auto& [id, bp] : breakpoints_) {
        result.push_back(bp);
    }
    
    return result;
}

bool ProcessController::hasBreakpointAt(uint64_t addr) const {
    auto it = addr_to_bp_.find(addr);
    if (it == addr_to_bp_.end()) {
        return false;
    }
    
    auto bp_it = breakpoints_.find(it->second);
    return bp_it != breakpoints_.end() && bp_it->second.enabled;
}

bool ProcessController::insertBreakpointInstruction(uint64_t addr, uint8_t& original) {
#ifdef TRACESMITH_PLATFORM_LINUX
    // Read original byte
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, current_thread_,
                      reinterpret_cast<void*>(addr), nullptr);
    if (errno != 0) {
        return false;
    }
    
    original = static_cast<uint8_t>(word & 0xFF);
    
    // Replace with int3 (0xCC)
    word = (word & ~0xFFL) | 0xCC;
    
    if (ptrace(PTRACE_POKEDATA, current_thread_,
               reinterpret_cast<void*>(addr),
               reinterpret_cast<void*>(word)) < 0) {
        return false;
    }
    
    return true;
#else
    (void)addr;
    (void)original;
    return false;
#endif
}

bool ProcessController::removeBreakpointInstruction(uint64_t addr, uint8_t original) {
#ifdef TRACESMITH_PLATFORM_LINUX
    errno = 0;
    long word = ptrace(PTRACE_PEEKDATA, current_thread_,
                      reinterpret_cast<void*>(addr), nullptr);
    if (errno != 0) {
        return false;
    }
    
    // Restore original byte
    word = (word & ~0xFFL) | original;
    
    if (ptrace(PTRACE_POKEDATA, current_thread_,
               reinterpret_cast<void*>(addr),
               reinterpret_cast<void*>(word)) < 0) {
        return false;
    }
    
    return true;
#else
    (void)addr;
    (void)original;
    return false;
#endif
}

void ProcessController::handleBreakpointHit(pid_t tid) {
    // Update hit count
    RegisterSet regs = readRegisters();
    uint64_t bp_addr = regs.rip;
    
    auto it = addr_to_bp_.find(bp_addr);
    if (it != addr_to_bp_.end()) {
        auto bp_it = breakpoints_.find(it->second);
        if (bp_it != breakpoints_.end()) {
            bp_it->second.hit_count++;
        }
    }
    
    (void)tid;
}

void ProcessController::setGPUEventCallback(GPUEventCallback callback) {
    gpu_callback_ = callback;
}

} // namespace gdb
} // namespace tracesmith
