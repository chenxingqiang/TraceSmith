/**
 * @file tracesmith-gdbserver.cpp
 * @brief TraceSmith GDB Server - GPU-aware GDB stub
 * @version 0.10.0
 * 
 * Usage:
 *   tracesmith-gdbserver [options] -- <program> [args...]
 *   tracesmith-gdbserver [options] --attach <pid>
 * 
 * Options:
 *   --port <N>      Listen on TCP port N (default: 1234)
 *   --socket <path> Use Unix socket instead of TCP
 *   --attach <pid>  Attach to existing process
 *   --verbose       Enable verbose output
 *   --help          Show this help
 */

#include "tracesmith/gdb/rsp_handler.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <signal.h>

using namespace tracesmith::gdb;

// Global handler for signal handling
RSPHandler* g_handler = nullptr;

void signalHandler(int sig) {
    (void)sig;
    if (g_handler) {
        g_handler->stop();
    }
}

void printUsage(const char* prog) {
    std::cout << "TraceSmith GDB Server v0.10.0\n";
    std::cout << "GPU-aware debugging with CUDA/MACA/Metal support\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << prog << " [options] -- <program> [args...]\n";
    std::cout << "  " << prog << " [options] --attach <pid>\n\n";
    std::cout << "Options:\n";
    std::cout << "  --port <N>        Listen on TCP port N (default: 1234)\n";
    std::cout << "  --socket <path>   Use Unix socket instead of TCP\n";
    std::cout << "  --attach <pid>    Attach to existing process\n";
    std::cout << "  --verbose, -v     Enable verbose output\n";
    std::cout << "  --help, -h        Show this help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << prog << " --port 1234 -- ./my_cuda_app\n";
    std::cout << "  " << prog << " --attach 12345\n\n";
    std::cout << "GDB Connection:\n";
    std::cout << "  (gdb) target remote :1234\n\n";
    std::cout << "TraceSmith GPU Extensions:\n";
    std::cout << "  (gdb) monitor ts help\n";
}

int main(int argc, char* argv[]) {
    RSPConfig config;
    pid_t attach_pid = 0;
    std::vector<std::string> program_args;
    bool found_separator = false;
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (found_separator) {
            program_args.push_back(arg);
            continue;
        }
        
        if (arg == "--") {
            found_separator = true;
            continue;
        }
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        
        if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
            continue;
        }
        
        if (arg == "--port" && i + 1 < argc) {
            config.port = std::atoi(argv[++i]);
            continue;
        }
        
        if (arg == "--socket" && i + 1 < argc) {
            config.unix_socket = argv[++i];
            continue;
        }
        
        if (arg == "--attach" && i + 1 < argc) {
            attach_pid = std::atoi(argv[++i]);
            continue;
        }
        
        // Unknown argument - might be start of program
        if (arg[0] != '-') {
            program_args.push_back(arg);
            found_separator = true;
            continue;
        }
        
        std::cerr << "Unknown option: " << arg << "\n";
        std::cerr << "Use --help for usage\n";
        return 1;
    }
    
    // Validate arguments
    if (attach_pid == 0 && program_args.empty()) {
        std::cerr << "Error: specify a program to run or --attach <pid>\n";
        std::cerr << "Use --help for usage\n";
        return 1;
    }
    
    // Set up signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create handler
    RSPHandler handler(config);
    g_handler = &handler;
    
    // Initialize
    bool init_ok;
    if (attach_pid > 0) {
        std::cout << "Attaching to process " << attach_pid << "...\n";
        init_ok = handler.initialize(attach_pid);
    } else {
        std::cout << "Starting: ";
        for (const auto& arg : program_args) {
            std::cout << arg << " ";
        }
        std::cout << "\n";
        init_ok = handler.initialize(program_args);
    }
    
    if (!init_ok) {
        std::cerr << "Failed to initialize target\n";
        return 1;
    }
    
    // Start listening
    if (!handler.listen()) {
        std::cerr << "Failed to start listening on port " << config.port << "\n";
        return 1;
    }
    
    std::cout << "TraceSmith GDB Server listening on ";
    if (config.unix_socket.empty()) {
        std::cout << "port " << config.port;
    } else {
        std::cout << "socket " << config.unix_socket;
    }
    std::cout << "\n";
    std::cout << "Connect with: (gdb) target remote :" << config.port << "\n";
    std::cout << "GPU extensions: (gdb) monitor ts help\n";
    
    // Run main loop
    handler.run();
    
    g_handler = nullptr;
    
    std::cout << "Server terminated\n";
    return 0;
}
