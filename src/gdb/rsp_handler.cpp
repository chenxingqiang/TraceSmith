/**
 * @file rsp_handler.cpp
 * @brief Implementation of GDB RSP Handler
 */

#include "tracesmith/gdb/rsp_handler.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace tracesmith {
namespace gdb {

// ============================================================
// Construction/Destruction
// ============================================================

RSPHandler::RSPHandler(const RSPConfig& config)
    : config_(config)
    , process_(std::make_unique<ProcessController>())
    , gpu_engine_(std::make_unique<GPUDebugEngine>())
{
}

RSPHandler::~RSPHandler() {
    stop();
    
    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }
    
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
}

// ============================================================
// Initialization
// ============================================================

bool RSPHandler::initialize(pid_t pid) {
    if (!process_->attach(pid)) {
        return false;
    }
    
    // Initialize GPU engine
    gpu_engine_->initialize(pid);
    
    return true;
}

bool RSPHandler::initialize(const std::vector<std::string>& args) {
    if (!process_->spawn(args)) {
        return false;
    }
    
    // Initialize GPU engine
    gpu_engine_->initialize(process_->pid());
    
    return true;
}

bool RSPHandler::listen() {
    if (!config_.unix_socket.empty()) {
        // Unix socket
        server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            return false;
        }
        
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, config_.unix_socket.c_str(), sizeof(addr.sun_path) - 1);
        
        unlink(config_.unix_socket.c_str());
        
        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }
    } else {
        // TCP socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            return false;
        }
        
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(config_.port);
        
        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }
    }
    
    if (::listen(server_fd_, 1) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    log("Listening on port " + std::to_string(config_.port));
    return true;
}

void RSPHandler::run() {
    if (server_fd_ < 0) {
        return;
    }
    
    running_ = true;
    
    // Accept connection
    log("Waiting for GDB connection...");
    client_fd_ = accept(server_fd_, nullptr, nullptr);
    
    if (client_fd_ < 0) {
        running_ = false;
        return;
    }
    
    log("GDB connected");
    
    // Main loop
    while (running_) {
        std::string packet = receivePacket();
        
        if (packet.empty()) {
            // Connection closed or error
            break;
        }
        
        // Handle the packet
        std::string response = handlePacket(packet);
        
        if (!response.empty()) {
            sendPacket(response);
        }
    }
    
    running_ = false;
    
    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }
    
    log("GDB disconnected");
}

void RSPHandler::stop() {
    running_ = false;
}

// ============================================================
// Packet I/O
// ============================================================

std::string RSPHandler::receivePacket() {
    std::string buffer;
    char c;
    
    // Skip leading junk, wait for '$'
    while (true) {
        ssize_t n = read(client_fd_, &c, 1);
        if (n <= 0) {
            return "";
        }
        
        // Handle interrupt
        if (c == '\x03') {
            process_->interrupt();
            continue;
        }
        
        // Handle ack/nack
        if (c == '+' || c == '-') {
            continue;
        }
        
        if (c == '$') {
            break;
        }
    }
    
    buffer = "$";
    
    // Read until '#'
    while (true) {
        ssize_t n = read(client_fd_, &c, 1);
        if (n <= 0) {
            return "";
        }
        buffer += c;
        if (c == '#') {
            break;
        }
    }
    
    // Read 2 checksum characters
    char cs[2];
    if (read(client_fd_, cs, 2) != 2) {
        return "";
    }
    buffer += cs[0];
    buffer += cs[1];
    
    // Decode and verify
    auto decoded = RSPPacket::decode(buffer);
    
    if (!decoded) {
        // Send NACK
        sendRaw("-");
        return "";
    }
    
    // Send ACK (unless in no-ack mode)
    if (!no_ack_mode_) {
        sendRaw("+");
    }
    
    if (config_.verbose) {
        log("RX: " + *decoded);
    }
    
    return *decoded;
}

bool RSPHandler::sendPacket(const std::string& data) {
    std::string packet = RSPPacket::encode(data);
    
    if (config_.verbose) {
        log("TX: " + data);
    }
    
    if (!sendRaw(packet)) {
        return false;
    }
    
    // Wait for ACK (unless in no-ack mode)
    if (!no_ack_mode_) {
        return waitForAck();
    }
    
    return true;
}

bool RSPHandler::sendRaw(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = write(client_fd_, data.c_str() + sent, data.size() - sent);
        if (n <= 0) {
            return false;
        }
        sent += n;
    }
    return true;
}

bool RSPHandler::waitForAck() {
    char c;
    ssize_t n = read(client_fd_, &c, 1);
    if (n <= 0) {
        return false;
    }
    return c == '+';
}

// ============================================================
// Command Dispatch
// ============================================================

std::string RSPHandler::handlePacket(const std::string& packet) {
    if (packet.empty()) {
        return "";
    }
    
    RSPPacketType type = RSPPacket::parseType(packet);
    
    switch (type) {
        case RSPPacketType::ReadRegisters:
            return handleReadRegisters();
            
        case RSPPacketType::WriteRegisters:
            return handleWriteRegisters(packet.substr(1));
            
        case RSPPacketType::ReadMemory: {
            // Format: m<addr>,<len>
            size_t comma = packet.find(',');
            if (comma == std::string::npos) {
                return "E01";
            }
            uint64_t addr = RSPPacket::hexToUint64(packet.substr(1, comma - 1));
            size_t len = RSPPacket::hexToUint64(packet.substr(comma + 1));
            return handleReadMemory(addr, len);
        }
            
        case RSPPacketType::WriteMemory: {
            // Format: M<addr>,<len>:<data>
            size_t comma = packet.find(',');
            size_t colon = packet.find(':');
            if (comma == std::string::npos || colon == std::string::npos) {
                return "E01";
            }
            uint64_t addr = RSPPacket::hexToUint64(packet.substr(1, comma - 1));
            return handleWriteMemory(addr, packet.substr(colon + 1));
        }
            
        case RSPPacketType::Continue:
            return handleContinue();
            
        case RSPPacketType::ContinueSignal: {
            int sig = RSPPacket::hexToUint64(packet.substr(1, 2));
            return handleContinue(sig);
        }
            
        case RSPPacketType::Step:
            return handleStep();
            
        case RSPPacketType::StepSignal: {
            int sig = RSPPacket::hexToUint64(packet.substr(1, 2));
            return handleStep(sig);
        }
            
        case RSPPacketType::Kill:
            process_->kill();
            return "OK";
            
        case RSPPacketType::Detach:
            process_->detach();
            return "OK";
            
        case RSPPacketType::InsertBreakpoint:
        case RSPPacketType::RemoveBreakpoint: {
            // Format: Z<type>,<addr>,<kind> or z<type>,<addr>,<kind>
            if (packet.size() < 5) {
                return "E01";
            }
            char op = packet[0];
            int bp_type = packet[1] - '0';
            
            size_t comma1 = packet.find(',');
            size_t comma2 = packet.find(',', comma1 + 1);
            if (comma1 == std::string::npos || comma2 == std::string::npos) {
                return "E01";
            }
            
            uint64_t addr = RSPPacket::hexToUint64(packet.substr(comma1 + 1, comma2 - comma1 - 1));
            int kind = RSPPacket::hexToUint64(packet.substr(comma2 + 1));
            
            return handleBreakpoint(op, bp_type, addr, kind);
        }
            
        case RSPPacketType::Query:
            return handleQuery(packet.substr(1));
            
        case RSPPacketType::QuerySet:
            return handleQuerySet(packet.substr(1));
            
        case RSPPacketType::VCommand:
            return handleVCommand(packet.substr(1));
            
        case RSPPacketType::RestartReason:
            return handleStopReason();
            
        case RSPPacketType::SetThread:
            return handleThreadOps(packet[1], packet.substr(2));
            
        case RSPPacketType::ThreadAlive: {
            pid_t tid = RSPPacket::hexToUint64(packet.substr(1));
            return handleThreadAlive(tid);
        }
            
        case RSPPacketType::ExtendedMode:
            return "OK";
            
        default:
            return "";  // Unsupported command
    }
}

// ============================================================
// Standard GDB Commands
// ============================================================

std::string RSPHandler::handleReadRegisters() {
    RegisterSet regs = process_->readRegisters();
    return regs.toHex();
}

std::string RSPHandler::handleWriteRegisters(const std::string& data) {
    RegisterSet regs = RegisterSet::fromHex(data);
    if (process_->writeRegisters(regs)) {
        return "OK";
    }
    return "E01";
}

std::string RSPHandler::handleReadMemory(uint64_t addr, size_t len) {
    auto data = process_->readMemory(addr, len);
    if (data.empty()) {
        return "E01";
    }
    return RSPPacket::toHex(data);
}

std::string RSPHandler::handleWriteMemory(uint64_t addr, const std::string& data) {
    auto bytes = RSPPacket::fromHex(data);
    if (process_->writeMemory(addr, bytes)) {
        return "OK";
    }
    return "E01";
}

std::string RSPHandler::handleBinaryWrite(uint64_t addr, size_t len, const std::string& data) {
    auto bytes = RSPPacket::unescapeBinary(data);
    if (bytes.size() != len) {
        return "E01";
    }
    if (process_->writeMemory(addr, bytes)) {
        return "OK";
    }
    return "E01";
}

std::string RSPHandler::handleContinue(int signal) {
    gpu_engine_->onProcessResume();
    process_->continueExecution(signal);
    StopEvent event = waitForStop();
    gpu_engine_->onProcessStop();
    return formatStopReply(event);
}

std::string RSPHandler::handleStep(int signal) {
    gpu_engine_->onProcessResume();
    process_->singleStep(signal);
    StopEvent event = waitForStop();
    gpu_engine_->onProcessStop();
    return formatStopReply(event);
}

std::string RSPHandler::handleBreakpoint(char op, int type, uint64_t addr, int kind) {
    (void)kind;  // Not used for software breakpoints
    
    if (type != 0) {
        return "";  // Only software breakpoints supported for now
    }
    
    if (op == 'Z') {
        int bp_id = process_->setBreakpoint(addr);
        if (bp_id >= 0) {
            return "OK";
        }
        return "E01";
    } else {
        if (process_->removeBreakpointAt(addr)) {
            return "OK";
        }
        return "E01";
    }
}

std::string RSPHandler::handleStopReason() {
    // Return SIGTRAP as initial stop reason
    return "S05";
}

std::string RSPHandler::handleThreadOps(char op, const std::string& args) {
    (void)op;
    
    if (args.empty() || args == "-1" || args == "0") {
        return "OK";  // Any thread / all threads
    }
    
    pid_t tid = RSPPacket::hexToUint64(args);
    if (process_->selectThread(tid)) {
        return "OK";
    }
    return "E01";
}

std::string RSPHandler::handleThreadAlive(pid_t tid) {
    if (process_->isThreadAlive(tid)) {
        return "OK";
    }
    return "E01";
}

// ============================================================
// Query Handlers
// ============================================================

std::string RSPHandler::handleQuery(const std::string& query) {
    RSPQuery q = RSPQuery::parse(query);
    
    if (q.name == "Supported") {
        std::ostringstream oss;
        oss << "PacketSize=" << std::hex << config_.max_packet_size;
        oss << ";qXfer:features:read+";
        oss << ";QStartNoAckMode+";
        oss << ";multiprocess+";
        return oss.str();
    }
    
    if (q.name == "Attached") {
        return "1";  // Attached to existing process
    }
    
    if (q.name == "fThreadInfo") {
        auto threads = process_->getThreads();
        if (threads.empty()) {
            return "l";
        }
        
        std::ostringstream oss;
        oss << "m";
        for (size_t i = 0; i < threads.size(); ++i) {
            if (i > 0) oss << ",";
            oss << std::hex << threads[i];
        }
        return oss.str();
    }
    
    if (q.name == "sThreadInfo") {
        return "l";  // End of thread list
    }
    
    if (q.name == "C") {
        // Current thread
        std::ostringstream oss;
        oss << "QC" << std::hex << process_->currentThread();
        return oss.str();
    }
    
    if (q.name == "Rcmd") {
        // Monitor command (hex encoded)
        if (!q.args.empty()) {
            std::string cmd;
            auto bytes = RSPPacket::fromHex(q.args[0]);
            cmd = std::string(bytes.begin(), bytes.end());
            return handleMonitor(cmd);
        }
        return "E01";
    }
    
    return "";  // Unsupported query
}

std::string RSPHandler::handleQuerySet(const std::string& query) {
    RSPQuery q = RSPQuery::parse(query);
    
    if (q.name == "StartNoAckMode") {
        no_ack_mode_ = true;
        return "OK";
    }
    
    return "";  // Unsupported
}

std::string RSPHandler::handleVCommand(const std::string& cmd) {
    if (cmd.compare(0, 5, "Cont;") == 0) {
        // vCont command
        std::string action = cmd.substr(5);
        
        if (action[0] == 'c') {
            return handleContinue();
        } else if (action[0] == 's') {
            return handleStep();
        }
    }
    
    if (cmd.compare(0, 5, "Cont?") == 0) {
        // Query supported vCont actions
        return "vCont;c;C;s;S";
    }
    
    return "";
}

// ============================================================
// Monitor (TraceSmith Extensions)
// ============================================================

std::string RSPHandler::handleMonitor(const std::string& cmd) {
    // Parse command
    std::istringstream iss(cmd);
    std::string word;
    std::vector<std::string> words;
    
    while (iss >> word) {
        words.push_back(word);
    }
    
    if (words.empty()) {
        return RSPPacket::toHex("Error: empty command\n");
    }
    
    // Check for "ts" prefix
    if (words[0] != "ts") {
        return RSPPacket::toHex("Error: unknown command. Use 'monitor ts help'\n");
    }
    
    if (words.size() < 2) {
        return handleTSHelp();
    }
    
    std::string subcmd = words[1];
    std::string args;
    
    // Collect remaining args
    for (size_t i = 2; i < words.size(); ++i) {
        if (!args.empty()) args += " ";
        args += words[i];
    }
    
    if (subcmd == "help") return handleTSHelp();
    if (subcmd == "status") return handleTSStatus();
    if (subcmd == "devices") return handleTSDevices();
    if (subcmd == "memory") return handleTSMemory(args);
    if (subcmd == "kernels") return handleTSKernels(args);
    if (subcmd == "kernel-search") return handleTSKernelSearch(args);
    if (subcmd == "streams") return handleTSStreams();
    if (subcmd == "break") return handleTSBreakpoint(args);
    if (subcmd == "gpu") return handleTSGPUMemory(args);
    if (subcmd == "allocs") return handleTSAllocations(args);
    if (subcmd == "trace") {
        if (args == "start") return handleTSTraceStart();
        if (args == "stop") return handleTSTraceStop();
        if (args.compare(0, 5, "save ") == 0) return handleTSTraceSave(args.substr(5));
        if (args.compare(0, 5, "load ") == 0) return handleTSTraceLoad(args.substr(5));
    }
    if (subcmd == "replay") return handleTSReplay(args);
    
    return RSPPacket::toHex("Error: unknown command '" + subcmd + "'\n");
}

// ============================================================
// TraceSmith Extension Commands
// ============================================================

std::string RSPHandler::handleTSHelp() {
    std::ostringstream oss;
    oss << "TraceSmith GDB Extensions v0.10.0\n";
    oss << "=================================\n\n";
    oss << "GPU Status & Info:\n";
    oss << "  monitor ts status              Show GPU status summary\n";
    oss << "  monitor ts devices             List all GPU devices\n";
    oss << "  monitor ts memory [DEV]        Show GPU memory usage\n";
    oss << "  monitor ts streams             Show stream states\n\n";
    oss << "Kernel History:\n";
    oss << "  monitor ts kernels [N]         Show last N kernel calls\n";
    oss << "  monitor ts kernel-search PAT   Search kernels by pattern\n\n";
    oss << "GPU Breakpoints:\n";
    oss << "  monitor ts break kernel NAME   Break on kernel launch\n";
    oss << "  monitor ts break memcpy [DIR]  Break on memcpy\n";
    oss << "  monitor ts break alloc         Break on allocation\n";
    oss << "  monitor ts break list          List GPU breakpoints\n";
    oss << "  monitor ts break delete N      Delete breakpoint\n\n";
    oss << "Trace:\n";
    oss << "  monitor ts trace start         Start capture\n";
    oss << "  monitor ts trace stop          Stop capture\n";
    oss << "  monitor ts trace save FILE     Save trace\n";
    oss << "  monitor ts trace load FILE     Load trace\n\n";
    oss << "Replay:\n";
    oss << "  monitor ts replay start        Start replay\n";
    oss << "  monitor ts replay step         Step event\n";
    oss << "  monitor ts replay status       Show status\n";
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSStatus() {
    std::ostringstream oss;
    
    auto state = gpu_engine_->getGPUState();
    auto devices = gpu_engine_->getDevices();
    
    oss << "GPU Status\n";
    oss << "==========\n";
    oss << "Devices: " << devices.size() << "\n";
    
    for (const auto& dev : devices) {
        oss << "  " << dev.device_id << ": " << dev.name;
        if (!dev.vendor.empty()) {
            oss << " (" << dev.vendor << ")";
        }
        oss << "\n";
    }
    
    if (!state.memory_states.empty()) {
        oss << "\nMemory:\n";
        for (const auto& mem : state.memory_states) {
            double used_mb = mem.used_memory / (1024.0 * 1024.0);
            double total_mb = mem.total_memory / (1024.0 * 1024.0);
            oss << "  Device " << mem.device_id << ": " 
                << std::fixed << std::setprecision(1)
                << used_mb << " / " << total_mb << " MB\n";
        }
    }
    
    oss << "\nCapturing: " << (gpu_engine_->isCapturing() ? "Yes" : "No") << "\n";
    
    auto replay = gpu_engine_->getReplayState();
    if (!replay.trace_file.empty()) {
        oss << "Replay loaded: " << replay.trace_file << "\n";
        oss << "  Events: " << replay.current_event_index << "/" << replay.total_events << "\n";
    }
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSDevices() {
    std::ostringstream oss;
    
    auto devices = gpu_engine_->getDevices();
    
    if (devices.empty()) {
        oss << "No GPU devices found\n";
    } else {
        oss << "GPU Devices\n";
        oss << "===========\n";
        
        for (const auto& dev : devices) {
            oss << "\nDevice " << dev.device_id << ": " << dev.name << "\n";
            oss << "  Vendor: " << dev.vendor << "\n";
            oss << "  Compute: " << dev.compute_major << "." << dev.compute_minor << "\n";
            oss << "  Memory: " << (dev.total_memory / (1024 * 1024)) << " MB\n";
            oss << "  SMs: " << dev.multiprocessor_count << "\n";
            oss << "  Clock: " << (dev.clock_rate / 1000) << " MHz\n";
        }
    }
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSMemory(const std::string& args) {
    int device_id = -1;
    if (!args.empty()) {
        try {
            device_id = std::stoi(args);
        } catch (...) {
            return RSPPacket::toHex("Error: invalid device ID\n");
        }
    }
    
    auto snapshot = gpu_engine_->getMemoryUsage(device_id);
    
    std::ostringstream oss;
    oss << "GPU Memory\n";
    oss << "==========\n";
    oss << "Current: " << (snapshot.live_bytes / (1024.0 * 1024.0)) << " MB\n";
    oss << "Allocations: " << snapshot.live_allocations << "\n";
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSKernels(const std::string& args) {
    size_t count = 10;
    if (!args.empty()) {
        try {
            count = std::stoul(args);
        } catch (...) {
            // Use default
        }
    }
    
    auto kernels = gpu_engine_->getKernelHistory(count);
    
    std::ostringstream oss;
    oss << "Kernel History (last " << kernels.size() << ")\n";
    oss << std::string(40, '=') << "\n";
    
    if (kernels.empty()) {
        oss << "No kernels recorded\n";
    } else {
        for (size_t i = 0; i < kernels.size(); ++i) {
            const auto& k = kernels[i];
            oss << "#" << (i + 1) << " " << k.kernel_name;
            
            if (k.params.grid_x > 0) {
                oss << " <<<(" << k.params.grid_x << "," << k.params.grid_y << "," << k.params.grid_z << ")";
                oss << ",(" << k.params.block_x << "," << k.params.block_y << "," << k.params.block_z << ")>>>";
            }
            
            if (k.isComplete()) {
                double us = k.duration() / 1000.0;
                oss << " " << std::fixed << std::setprecision(1) << us << "µs";
            } else {
                oss << " [running]";
            }
            oss << "\n";
        }
    }
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSKernelSearch(const std::string& pattern) {
    if (pattern.empty()) {
        return RSPPacket::toHex("Usage: monitor ts kernel-search PATTERN\n");
    }
    
    auto kernels = gpu_engine_->findKernels(pattern);
    
    std::ostringstream oss;
    oss << "Found " << kernels.size() << " kernels matching '" << pattern << "'\n";
    
    for (const auto& k : kernels) {
        oss << "  " << k.kernel_name << " (device " << k.device_id << ")\n";
    }
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSStreams() {
    auto streams = gpu_engine_->getStreamStates();
    
    std::ostringstream oss;
    oss << "Stream States\n";
    oss << "=============\n";
    
    if (streams.empty()) {
        oss << "No streams recorded\n";
    } else {
        for (const auto& s : streams) {
            oss << "Device " << s.device_id << " Stream " << s.stream_id << ": ";
            oss << gpuStateToString(s.state) << "\n";
        }
    }
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSBreakpoint(const std::string& args) {
    std::istringstream iss(args);
    std::string action;
    iss >> action;
    
    if (action == "list") {
        auto bps = gpu_engine_->listGPUBreakpoints();
        
        std::ostringstream oss;
        oss << "GPU Breakpoints\n";
        oss << "===============\n";
        
        if (bps.empty()) {
            oss << "No GPU breakpoints set\n";
        } else {
            for (const auto& bp : bps) {
                oss << "#" << bp.id << " " << gpuBreakpointTypeToString(bp.type);
                if (!bp.kernel_pattern.empty()) {
                    oss << " '" << bp.kernel_pattern << "'";
                }
                if (bp.device_id >= 0) {
                    oss << " device=" << bp.device_id;
                }
                oss << " hits=" << bp.hit_count;
                if (!bp.enabled) {
                    oss << " [disabled]";
                }
                oss << "\n";
            }
        }
        
        return RSPPacket::toHex(oss.str());
    }
    
    if (action == "delete") {
        int id;
        if (!(iss >> id)) {
            return RSPPacket::toHex("Usage: monitor ts break delete ID\n");
        }
        
        if (gpu_engine_->removeGPUBreakpoint(id)) {
            return RSPPacket::toHex("Deleted GPU breakpoint " + std::to_string(id) + "\n");
        }
        return RSPPacket::toHex("Breakpoint not found\n");
    }
    
    if (action == "enable" || action == "disable") {
        int id;
        if (!(iss >> id)) {
            return RSPPacket::toHex("Usage: monitor ts break enable/disable ID\n");
        }
        
        if (gpu_engine_->enableGPUBreakpoint(id, action == "enable")) {
            return RSPPacket::toHex("OK\n");
        }
        return RSPPacket::toHex("Breakpoint not found\n");
    }
    
    if (action == "kernel") {
        std::string pattern;
        iss >> pattern;
        
        GPUBreakpoint bp;
        bp.type = GPUBreakpointType::KernelLaunch;
        bp.kernel_pattern = pattern;
        
        int id = gpu_engine_->setGPUBreakpoint(bp);
        
        std::ostringstream oss;
        oss << "GPU breakpoint " << id << ": kernel launch";
        if (!pattern.empty()) {
            oss << " '" << pattern << "'";
        }
        oss << "\n";
        
        return RSPPacket::toHex(oss.str());
    }
    
    if (action == "memcpy") {
        std::string dir;
        iss >> dir;
        
        GPUBreakpoint bp;
        if (dir == "h2d") {
            bp.type = GPUBreakpointType::MemcpyH2D;
        } else if (dir == "d2h") {
            bp.type = GPUBreakpointType::MemcpyD2H;
        } else if (dir == "d2d") {
            bp.type = GPUBreakpointType::MemcpyD2D;
        } else {
            bp.type = GPUBreakpointType::MemcpyH2D;  // Default
        }
        
        int id = gpu_engine_->setGPUBreakpoint(bp);
        return RSPPacket::toHex("GPU breakpoint " + std::to_string(id) + ": " +
                               gpuBreakpointTypeToString(bp.type) + "\n");
    }
    
    if (action == "alloc") {
        GPUBreakpoint bp;
        bp.type = GPUBreakpointType::MemAlloc;
        int id = gpu_engine_->setGPUBreakpoint(bp);
        return RSPPacket::toHex("GPU breakpoint " + std::to_string(id) + ": memory allocation\n");
    }
    
    if (action == "free") {
        GPUBreakpoint bp;
        bp.type = GPUBreakpointType::MemFree;
        int id = gpu_engine_->setGPUBreakpoint(bp);
        return RSPPacket::toHex("GPU breakpoint " + std::to_string(id) + ": memory free\n");
    }
    
    if (action == "sync") {
        GPUBreakpoint bp;
        bp.type = GPUBreakpointType::Synchronize;
        int id = gpu_engine_->setGPUBreakpoint(bp);
        return RSPPacket::toHex("GPU breakpoint " + std::to_string(id) + ": synchronize\n");
    }
    
    return RSPPacket::toHex("Usage: monitor ts break <kernel|memcpy|alloc|free|sync|list|delete|enable|disable>\n");
}

std::string RSPHandler::handleTSGPUMemory(const std::string& args) {
    std::istringstream iss(args);
    std::string action;
    iss >> action;
    
    if (action == "read") {
        int device;
        uint64_t addr;
        size_t len;
        
        if (!(iss >> device >> std::hex >> addr >> std::dec >> len)) {
            return RSPPacket::toHex("Usage: monitor ts gpu read DEV ADDR LEN\n");
        }
        
        auto data = gpu_engine_->readGPUMemory(device, addr, len);
        if (data.empty()) {
            return RSPPacket::toHex("Failed to read GPU memory\n");
        }
        
        std::ostringstream oss;
        oss << "GPU memory at 0x" << std::hex << addr << ":\n";
        for (size_t i = 0; i < data.size(); i += 16) {
            oss << std::hex << std::setw(8) << std::setfill('0') << (addr + i) << ": ";
            for (size_t j = 0; j < 16 && i + j < data.size(); ++j) {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i + j]) << " ";
            }
            oss << "\n";
        }
        
        return RSPPacket::toHex(oss.str());
    }
    
    return RSPPacket::toHex("Usage: monitor ts gpu <read DEV ADDR LEN>\n");
}

std::string RSPHandler::handleTSAllocations(const std::string& args) {
    int device = -1;
    if (!args.empty()) {
        try {
            device = std::stoi(args);
        } catch (...) {
            // Use default
        }
    }
    
    auto allocs = gpu_engine_->getMemoryAllocations(device);
    
    std::ostringstream oss;
    oss << "GPU Memory Allocations\n";
    oss << "======================\n";
    oss << "Count: " << allocs.size() << "\n\n";
    
    for (const auto& alloc : allocs) {
        oss << "0x" << std::hex << alloc.ptr << std::dec << ": " << alloc.size << " bytes";
        if (!alloc.allocator.empty()) {
            oss << " (" << alloc.allocator << ")";
        }
        oss << "\n";
    }
    
    return RSPPacket::toHex(oss.str());
}

std::string RSPHandler::handleTSTraceStart() {
    if (gpu_engine_->startCapture()) {
        return RSPPacket::toHex("GPU trace capture started\n");
    }
    return RSPPacket::toHex("Failed to start trace capture\n");
}

std::string RSPHandler::handleTSTraceStop() {
    if (gpu_engine_->stopCapture()) {
        auto events = gpu_engine_->getCapturedEvents();
        std::ostringstream oss;
        oss << "GPU trace capture stopped\n";
        oss << "Captured " << events.size() << " events\n";
        return RSPPacket::toHex(oss.str());
    }
    return RSPPacket::toHex("No capture in progress\n");
}

std::string RSPHandler::handleTSTraceSave(const std::string& filename) {
    if (filename.empty()) {
        return RSPPacket::toHex("Usage: monitor ts trace save FILENAME\n");
    }
    
    if (gpu_engine_->saveTrace(filename)) {
        return RSPPacket::toHex("Trace saved to " + filename + "\n");
    }
    return RSPPacket::toHex("Failed to save trace\n");
}

std::string RSPHandler::handleTSTraceLoad(const std::string& filename) {
    if (filename.empty()) {
        return RSPPacket::toHex("Usage: monitor ts trace load FILENAME\n");
    }
    
    if (gpu_engine_->loadTrace(filename)) {
        auto state = gpu_engine_->getReplayState();
        std::ostringstream oss;
        oss << "Loaded trace: " << filename << "\n";
        oss << "Events: " << state.total_events << "\n";
        return RSPPacket::toHex(oss.str());
    }
    return RSPPacket::toHex("Failed to load trace\n");
}

std::string RSPHandler::handleTSReplay(const std::string& args) {
    std::istringstream iss(args);
    std::string action;
    iss >> action;
    
    if (action == "status") {
        auto state = gpu_engine_->getReplayState();
        
        std::ostringstream oss;
        oss << "Replay Status\n";
        oss << "=============\n";
        
        if (state.trace_file.empty()) {
            oss << "No trace loaded\n";
        } else {
            oss << "File: " << state.trace_file << "\n";
            oss << "Events: " << state.current_event_index << "/" << state.total_events << "\n";
            oss << "Active: " << (state.active ? "Yes" : "No") << "\n";
            oss << "Paused: " << (state.paused ? "Yes" : "No") << "\n";
        }
        
        return RSPPacket::toHex(oss.str());
    }
    
    ReplayControl ctrl;
    
    if (action == "start") {
        ctrl.command = ReplayControl::Command::Start;
    } else if (action == "stop") {
        ctrl.command = ReplayControl::Command::Stop;
    } else if (action == "pause") {
        ctrl.command = ReplayControl::Command::Pause;
    } else if (action == "resume") {
        ctrl.command = ReplayControl::Command::Resume;
    } else if (action == "step") {
        ctrl.command = ReplayControl::Command::StepEvent;
    } else if (action == "step-kernel") {
        ctrl.command = ReplayControl::Command::StepKernel;
    } else if (action == "goto") {
        uint64_t ts;
        if (!(iss >> ts)) {
            return RSPPacket::toHex("Usage: monitor ts replay goto TIMESTAMP\n");
        }
        ctrl.command = ReplayControl::Command::GotoTimestamp;
        ctrl.target_timestamp = ts;
    } else {
        return RSPPacket::toHex("Usage: monitor ts replay <start|stop|pause|resume|step|step-kernel|goto|status>\n");
    }
    
    if (gpu_engine_->controlReplay(ctrl)) {
        auto event = gpu_engine_->getCurrentReplayEvent();
        if (event) {
            std::ostringstream oss;
            oss << "Current event: " << eventTypeToString(event->type) << " " << event->name << "\n";
            return RSPPacket::toHex(oss.str());
        }
        return RSPPacket::toHex("OK\n");
    }
    return RSPPacket::toHex("Replay command failed\n");
}

// ============================================================
// Helpers
// ============================================================

StopEvent RSPHandler::waitForStop() {
    return process_->waitForStop();
}

std::string RSPHandler::formatStopReply(const StopEvent& event) {
    switch (event.reason) {
        case StopReason::Exited:
            return "W" + RSPPacket::toHex(static_cast<uint64_t>(event.exit_code), 2);
            
        case StopReason::Breakpoint:
        case StopReason::Signal:
            return "T" + RSPPacket::toHex(static_cast<uint64_t>(static_cast<int>(event.signal)), 2) +
                   "thread:" + RSPPacket::toHex(static_cast<uint64_t>(event.thread_id)) + ";";
            
        case StopReason::GPUBreakpoint:
            // Format GPU breakpoint as stop with SIGTRAP and extra info
            return "T05thread:" + RSPPacket::toHex(static_cast<uint64_t>(event.thread_id)) + ";";
            
        default:
            return "S05";  // SIGTRAP
    }
}

std::string RSPHandler::formatGPUBreakpointHit(const GPUBreakpoint& bp, const TraceEvent& event) {
    std::ostringstream oss;
    oss << "GPU breakpoint " << bp.id << " hit: " << gpuBreakpointTypeToString(bp.type);
    if (!event.name.empty()) {
        oss << " '" << event.name << "'";
    }
    return oss.str();
}

void RSPHandler::log(const std::string& msg) {
    if (config_.verbose) {
        std::cerr << "[tracesmith-gdbserver] " << msg << std::endl;
    }
}

} // namespace gdb
} // namespace tracesmith
