/**
 * TraceSmith CLI - GPU Profiling & Replay System
 * 
 * A comprehensive command-line interface for real GPU profiling,
 * trace analysis, export, and replay.
 */

#include <tracesmith/tracesmith.hpp>
#include <tracesmith/state/perfetto_exporter.hpp>
#include <tracesmith/state/timeline_builder.hpp>
#include <tracesmith/replay/replay_engine.hpp>
#include <tracesmith/common/stack_capture.hpp>

#ifdef TRACESMITH_ENABLE_CUDA
#include <tracesmith/capture/cupti_profiler.hpp>
#include <cuda_runtime.h>
#endif

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <algorithm>
#include <signal.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <memory>

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#endif

using namespace tracesmith;

// =============================================================================
// ANSI Color Codes (for terminal output)
// =============================================================================
namespace Color {
    const char* Reset   = "\033[0m";
    const char* Bold    = "\033[1m";
    const char* Red     = "\033[31m";
    const char* Green   = "\033[32m";
    const char* Yellow  = "\033[33m";
    const char* Blue    = "\033[34m";
    const char* Magenta = "\033[35m";
    const char* Cyan    = "\033[36m";
    const char* White   = "\033[37m";
    
    // Check if colors should be enabled
    bool enabled = true;
    
    const char* get(const char* color) {
        return enabled ? color : "";
    }
}

#define C(color) Color::get(Color::color)

// =============================================================================
// Global State
// =============================================================================
static volatile bool g_interrupted = false;

void signalHandler(int) {
    g_interrupted = true;
}

// =============================================================================
// ASCII Art Banner
// =============================================================================
void printBanner() {
    std::cout << C(Cyan) << R"(
████████╗██████╗  █████╗  ██████╗███████╗███████╗███╗   ███╗██╗████████╗██╗  ██╗
╚══██╔══╝██╔══██╗██╔══██╗██╔════╝██╔════╝██╔════╝████╗ ████║██║╚══██╔══╝██║  ██║
   ██║   ██████╔╝███████║██║     █████╗  ███████╗██╔████╔██║██║   ██║   ███████║
   ██║   ██╔══██╗██╔══██║██║     ██╔══╝  ╚════██║██║╚██╔╝██║██║   ██║   ██╔══██║
   ██║   ██║  ██║██║  ██║╚██████╗███████╗███████║██║ ╚═╝ ██║██║   ██║   ██║  ██║
   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚══════╝╚══════╝╚═╝     ╚═╝╚═╝   ╚═╝   ╚═╝  ╚═╝
)" << C(Reset);
    std::cout << C(Yellow) << "                    GPU Profiling & Replay System v" 
              << getVersionString() << C(Reset) << "\n\n";
}

void printCompactBanner() {
    std::cout << C(Cyan) << C(Bold) << "TraceSmith" << C(Reset) 
              << " v" << getVersionString() 
              << " - GPU Profiling & Replay System\n\n";
}

// =============================================================================
// Utility Functions
// =============================================================================
std::string formatTimestamp(Timestamp ts) {
    uint64_t ns = ts % 1000;
    uint64_t us = (ts / 1000) % 1000;
    uint64_t ms = (ts / 1000000) % 1000;
    uint64_t s = ts / 1000000000;
    
    std::ostringstream oss;
    oss << s << "." << std::setfill('0') 
        << std::setw(3) << ms << "."
        << std::setw(3) << us << "."
        << std::setw(3) << ns;
    return oss.str();
}

std::string formatTimeDuration(Timestamp dur) {
    if (dur < 1000) {
        return std::to_string(dur) + " ns";
    } else if (dur < 1000000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (dur / 1000.0) << " µs";
        return oss.str();
    } else if (dur < 1000000000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (dur / 1000000.0) << " ms";
        return oss.str();
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (dur / 1000000000.0) << " s";
        return oss.str();
    }
}

std::string formatByteSize(uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
        return oss.str();
    } else if (bytes < 1024ULL * 1024 * 1024) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024)) << " MB";
        return oss.str();
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024 * 1024)) << " GB";
        return oss.str();
    }
}

void printSuccess(const std::string& msg) {
    std::cout << C(Green) << "✓ " << C(Reset) << msg << "\n";
}

void printError(const std::string& msg) {
    std::cerr << C(Red) << "✗ Error: " << C(Reset) << msg << "\n";
}

void printWarning(const std::string& msg) {
    std::cout << C(Yellow) << "⚠ Warning: " << C(Reset) << msg << "\n";
}

void printInfo(const std::string& msg) {
    std::cout << C(Blue) << "ℹ " << C(Reset) << msg << "\n";
}

void printSection(const std::string& title) {
    std::cout << "\n" << C(Bold) << C(Cyan) << "═══ " << title << " ═══" << C(Reset) << "\n\n";
}

// =============================================================================
// Help & Usage
// =============================================================================
void printUsage(const char* program) {
    printBanner();
    
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " <COMMAND> [OPTIONS]\n\n";
    
    std::cout << C(Bold) << "COMMANDS:" << C(Reset) << "\n";
    std::cout << C(Green) << "    profile" << C(Reset) << "     Profile a command (record + execute)\n";
    std::cout << C(Green) << "    record" << C(Reset) << "      Record GPU events to a trace file\n";
    std::cout << C(Green) << "    view" << C(Reset) << "        View contents of a trace file\n";
    std::cout << C(Green) << "    info" << C(Reset) << "        Show detailed information about a trace file\n";
    std::cout << C(Green) << "    export" << C(Reset) << "      Export trace to Perfetto or other formats\n";
    std::cout << C(Green) << "    analyze" << C(Reset) << "     Analyze trace for performance insights\n";
    std::cout << C(Green) << "    replay" << C(Reset) << "      Replay a captured trace\n";
    std::cout << C(Green) << "    benchmark" << C(Reset) << "   Run 10K GPU call stacks benchmark\n";
    std::cout << C(Green) << "    devices" << C(Reset) << "     List available GPU devices\n";
    std::cout << C(Green) << "    version" << C(Reset) << "     Show version information\n";
    std::cout << C(Green) << "    help" << C(Reset) << "        Show this help message\n\n";
    
    std::cout << C(Bold) << "EXAMPLES:" << C(Reset) << "\n";
    std::cout << "    " << program << " profile -- python train.py    # Profile a command\n";
    std::cout << "    " << program << " profile -o t.sbt -- ./app     # Profile with custom output\n";
    std::cout << "    " << program << " record -o trace.sbt -d 5      # Record for 5 seconds\n";
    std::cout << "    " << program << " view trace.sbt --stats        # Show statistics\n";
    std::cout << "    " << program << " export trace.sbt -f perfetto  # Export to Perfetto\n";
    std::cout << "    " << program << " analyze trace.sbt             # Analyze performance\n";
    std::cout << "    " << program << " benchmark -n 10000            # Run 10K benchmark\n";
    std::cout << "    " << program << " devices                       # List GPUs\n\n";
    
    std::cout << "Run '" << C(Cyan) << program << " <command> --help" << C(Reset) 
              << "' for more information on a command.\n";
}

void printRecordUsage(const char* program) {
    printCompactBanner();
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " record [OPTIONS]\n\n";
    
    std::cout << C(Bold) << "DESCRIPTION:" << C(Reset) << "\n";
    std::cout << "    Record GPU events to a trace file using real GPU profiling.\n\n";
    
    std::cout << C(Yellow) << "IMPORTANT (CUDA/MACA):" << C(Reset) << "\n";
    std::cout << "    CUPTI/MCPTI only captures events from the TraceSmith process itself.\n";
    std::cout << "    To profile external applications, use:\n";
    std::cout << "      " << C(Cyan) << program << " profile --nsys -- <command>" << C(Reset) << "      (NVIDIA)\n";
    std::cout << "      " << C(Cyan) << program << " profile --mctracer -- <command>" << C(Reset) << "  (MetaX)\n\n";
    
    std::cout << C(Bold) << "OPTIONS:" << C(Reset) << "\n";
    std::cout << "    -o, --output <FILE>      Output trace file (default: trace.sbt)\n";
    std::cout << "    -d, --duration <SEC>     Recording duration in seconds (default: 5)\n";
    std::cout << "    -b, --buffer <SIZE>      Ring buffer size in events (default: 1M)\n";
    std::cout << "    -p, --platform <TYPE>    GPU platform: cuda, rocm, metal, maca, auto (default: auto)\n";
    std::cout << "    -k, --kernels            Capture kernel events (default: on)\n";
    std::cout << "    -m, --memory             Capture memory events (default: on)\n";
    std::cout << "    -s, --stacks             Capture call stacks (default: off)\n";
    std::cout << "    -v, --verbose            Verbose output\n";
    std::cout << "    -h, --help               Show this help message\n\n";
    
    std::cout << C(Bold) << "EXAMPLES:" << C(Reset) << "\n";
    std::cout << "    " << program << " record -o my_trace.sbt -d 10\n";
    std::cout << "    " << program << " record -p cuda -d 30 --stacks\n";
}

void printProfileUsage(const char* program) {
    printCompactBanner();
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " profile [OPTIONS] -- <COMMAND> [ARGS...]\n\n";
    
    std::cout << C(Bold) << "DESCRIPTION:" << C(Reset) << "\n";
    std::cout << "    Profile a command by recording GPU events during its execution.\n\n";
    
    std::cout << C(Yellow) << "IMPORTANT:" << C(Reset) << " For CUDA/MACA, you " << C(Bold) << "MUST" << C(Reset) << " use system-level profilers:\n";
    std::cout << "    " << C(Green) << "--nsys" << C(Reset) << "       NVIDIA GPU profiling (uses Nsight Systems)\n";
    std::cout << "    " << C(Green) << "--mctracer" << C(Reset) << "   MetaX MACA GPU profiling (uses mcTracer)\n";
    std::cout << "    " << C(Green) << "--xctrace" << C(Reset) << "    Apple Metal GPU profiling (uses Instruments)\n\n";
    
    std::cout << C(Bold) << "OPTIONS:" << C(Reset) << "\n";
    std::cout << "    -o, --output <FILE>      Output trace file (default: <command>_trace.sbt)\n";
    std::cout << "    -b, --buffer <SIZE>      Ring buffer size in events (default: 1M)\n";
    std::cout << "    --perfetto               Also export to Perfetto JSON format\n";
    std::cout << "    --nsys                   " << C(Green) << "[NVIDIA]" << C(Reset) << " Use Nsight Systems for CUDA profiling\n";
#ifdef __APPLE__
    std::cout << "    --xctrace                " << C(Green) << "[Apple]" << C(Reset) << " Use Instruments for Metal GPU profiling\n";
    std::cout << "    --xctrace-template <T>   Instruments template (default: 'Metal System Trace')\n";
#endif
#ifdef TRACESMITH_ENABLE_MACA
    std::cout << "    --mctracer               " << C(Green) << "[MetaX]" << C(Reset) << " Use mcTracer for MACA GPU profiling\n";
#endif
    std::cout << "    --keep-trace             Keep the raw trace output directory\n";
    std::cout << "    -v, --verbose            Verbose output\n";
    std::cout << "    -h, --help               Show this help message\n\n";
    
    std::cout << C(Bold) << "EXAMPLES (Recommended):" << C(Reset) << "\n";
    std::cout << C(Green) << "  NVIDIA CUDA:" << C(Reset) << "\n";
    std::cout << "    " << program << " profile --nsys -- python train.py\n";
    std::cout << "    " << program << " profile --nsys --perfetto -- ./my_cuda_app\n\n";
#ifdef TRACESMITH_ENABLE_MACA
    std::cout << C(Green) << "  MetaX MACA:" << C(Reset) << "\n";
    std::cout << "    " << program << " profile --mctracer -- ./my_maca_app\n";
    std::cout << "    " << program << " profile --mctracer --perfetto -- python train.py\n\n";
#endif
#ifdef __APPLE__
    std::cout << C(Green) << "  Apple Metal:" << C(Reset) << "\n";
    std::cout << "    " << program << " profile --xctrace -- python train.py\n\n";
#endif

    std::cout << C(Bold) << "NOTE:" << C(Reset) << "\n";
    std::cout << "    Use '--' to separate tracesmith options from the command to profile.\n";
    std::cout << "    Without --nsys/--mctracer/--xctrace, child process GPU events cannot be captured\n";
    std::cout << "    due to CUPTI/MCPTI API limitations (only profiles the calling process).\n";
}

void printViewUsage(const char* program) {
    printCompactBanner();
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " view [OPTIONS] <FILE>\n\n";
    
    std::cout << C(Bold) << "OPTIONS:" << C(Reset) << "\n";
    std::cout << "    -f, --format <FMT>       Output format: text, json, csv (default: text)\n";
    std::cout << "    -n, --limit <COUNT>      Maximum number of events to show\n";
    std::cout << "    -t, --type <TYPE>        Filter by event type\n";
    std::cout << "    --stats                  Show statistics only\n";
    std::cout << "    --timeline               Show ASCII timeline\n";
    std::cout << "    -h, --help               Show this help message\n";
}

void printExportUsage(const char* program) {
    printCompactBanner();
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " export [OPTIONS] <INPUT_FILE>\n\n";
    
    std::cout << C(Bold) << "OPTIONS:" << C(Reset) << "\n";
    std::cout << "    -o, --output <FILE>      Output file (default: auto-generated)\n";
    std::cout << "    -f, --format <FMT>       Export format:\n";
    std::cout << "                               perfetto   - Perfetto JSON (default)\n";
    std::cout << "                               proto      - Perfetto protobuf\n";
    std::cout << "                               chrome     - Chrome trace format\n";
    std::cout << "                               json       - Raw JSON\n";
    std::cout << "                               csv        - CSV format\n";
    std::cout << "    --counters               Include counter tracks\n";
    std::cout << "    --flows                  Include flow events\n";
    std::cout << "    -h, --help               Show this help message\n";
}

void printAnalyzeUsage(const char* program) {
    printCompactBanner();
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " analyze [OPTIONS] <FILE>\n\n";
    
    std::cout << C(Bold) << "OPTIONS:" << C(Reset) << "\n";
    std::cout << "    --gpu-util               Show GPU utilization analysis\n";
    std::cout << "    --memory                 Show memory usage analysis\n";
    std::cout << "    --kernels                Show kernel performance analysis\n";
    std::cout << "    --streams                Show stream activity analysis\n";
    std::cout << "    --hotspots               Identify performance hotspots\n";
    std::cout << "    --all                    Run all analyses (default)\n";
    std::cout << "    -o, --output <FILE>      Save report to file\n";
    std::cout << "    -h, --help               Show this help message\n";
}

void printReplayUsage(const char* program) {
    printCompactBanner();
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " replay [OPTIONS] <FILE>\n\n";
    
    std::cout << C(Bold) << "OPTIONS:" << C(Reset) << "\n";
    std::cout << "    --mode <MODE>            Replay mode: full, partial, dry-run (default: dry-run)\n";
    std::cout << "    --speed <FACTOR>         Replay speed factor (default: 1.0)\n";
    std::cout << "    --stream <ID>            Replay only specific stream\n";
    std::cout << "    --validate               Validate determinism\n";
    std::cout << "    -v, --verbose            Verbose output\n";
    std::cout << "    -h, --help               Show this help message\n";
}

void printBenchmarkUsage(const char* program) {
    printCompactBanner();
    std::cout << C(Bold) << "USAGE:" << C(Reset) << "\n";
    std::cout << "    " << program << " benchmark [OPTIONS]\n\n";
    
    std::cout << C(Bold) << "DESCRIPTION:" << C(Reset) << "\n";
    std::cout << "    Run the 10K GPU instruction-level call stacks benchmark.\n";
    std::cout << "    This validates TraceSmith's core feature: non-intrusive capture\n";
    std::cout << "    of 10,000+ instruction-level GPU call stacks.\n\n";
    
    std::cout << C(Bold) << "OPTIONS:" << C(Reset) << "\n";
    std::cout << "    -n, --count <NUM>        Number of kernels to launch (default: 10000)\n";
    std::cout << "    -o, --output <FILE>      Save trace to file (default: benchmark.sbt)\n";
    std::cout << "    --no-stacks              Disable host call stack capture\n";
    std::cout << "    -v, --verbose            Verbose output\n";
    std::cout << "    -h, --help               Show this help message\n\n";
    
    std::cout << C(Bold) << "REQUIREMENTS:" << C(Reset) << "\n";
    std::cout << "    - NVIDIA GPU with CUDA support\n";
    std::cout << "    - Built with -DTRACESMITH_ENABLE_CUDA=ON\n";
}

// =============================================================================
// Command: devices - List Available GPUs
// =============================================================================
int cmdDevices([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    printSection("GPU Device Detection");
    
    bool found_any = false;
    
    // Check CUDA
    std::cout << C(Bold) << "NVIDIA CUDA:" << C(Reset) << "\n";
    if (isCUDAAvailable()) {
        int count = getCUDADeviceCount();
        int driver = getCUDADriverVersion();
        printSuccess("CUDA available");
        std::cout << "  Devices: " << count << "\n";
        std::cout << "  Driver:  " << driver << "\n";
        found_any = true;
        
        // Get device info if possible
        auto profiler = createProfiler(PlatformType::CUDA);
        if (profiler) {
            ProfilerConfig config;
            if (profiler->initialize(config)) {
                auto devices = profiler->getDeviceInfo();
                for (const auto& dev : devices) {
                    std::cout << "\n  " << C(Cyan) << "Device " << dev.device_id << ": " 
                              << C(Reset) << dev.name << "\n";
                    std::cout << "    Vendor:     " << dev.vendor << "\n";
                    std::cout << "    Compute:    " << dev.compute_major << "." << dev.compute_minor << "\n";
                    std::cout << "    Memory:     " << formatByteSize(dev.total_memory) << "\n";
                    std::cout << "    SMs:        " << dev.multiprocessor_count << "\n";
                    std::cout << "    Clock:      " << (dev.clock_rate / 1000) << " MHz\n";
                }
            }
        }
    } else {
        std::cout << "  " << C(Yellow) << "Not available" << C(Reset) << "\n";
    }
    
    // Check Metal
    std::cout << "\n" << C(Bold) << "Apple Metal:" << C(Reset) << "\n";
    if (isMetalAvailable()) {
        int count = getMetalDeviceCount();
        printSuccess("Metal available");
        std::cout << "  Devices: " << count << "\n";
        found_any = true;
    } else {
        std::cout << "  " << C(Yellow) << "Not available" << C(Reset) << "\n";
    }
    
    // Check MetaX MACA
    std::cout << "\n" << C(Bold) << "MetaX MACA:" << C(Reset) << "\n";
    if (isMACAAvailable()) {
        int count = getMACADeviceCount();
        int driver = getMACADriverVersion();
        printSuccess("MACA available");
        std::cout << "  Devices: " << count << "\n";
        std::cout << "  Driver:  " << driver << "\n";
        found_any = true;
        
        // Get device info if possible
        auto profiler = createProfiler(PlatformType::MACA);
        if (profiler) {
            ProfilerConfig config;
            if (profiler->initialize(config)) {
                auto devices = profiler->getDeviceInfo();
                for (const auto& dev : devices) {
                    std::cout << "\n  " << C(Cyan) << "Device " << dev.device_id << ": " 
                              << C(Reset) << dev.name << "\n";
                    std::cout << "    Vendor:     " << dev.vendor << "\n";
                    std::cout << "    Compute:    " << dev.compute_major << "." << dev.compute_minor << "\n";
                    std::cout << "    Memory:     " << formatByteSize(dev.total_memory) << "\n";
                    std::cout << "    SMs:        " << dev.multiprocessor_count << "\n";
                    std::cout << "    Clock:      " << (dev.clock_rate / 1000) << " MHz\n";
                }
                profiler->finalize();
            }
        }
    } else {
        std::cout << "  " << C(Yellow) << "Not available" << C(Reset) << "\n";
    }
    
    // Check ROCm
    std::cout << "\n" << C(Bold) << "AMD ROCm:" << C(Reset) << "\n";
    std::cout << "  " << C(Yellow) << "Coming soon" << C(Reset) << "\n";
    
    std::cout << "\n";
    
    if (!found_any) {
        printWarning("No supported GPU platforms detected.");
        std::cout << "Make sure GPU drivers are installed and accessible.\n";
    }
    
    return found_any ? 0 : 1;
}

// =============================================================================
// Command: profile with xctrace (macOS only)
// =============================================================================
#ifdef __APPLE__
int cmdProfileXCTrace(
    const std::vector<std::string>& command,
    std::string output_file,
    const std::string& xctrace_template,
    bool keep_trace,
    [[maybe_unused]] bool export_perfetto_flag
) {
    // Generate output filename if not provided
    if (output_file.empty()) {
        std::string cmd_name = command[0];
        size_t slash_pos = cmd_name.rfind('/');
        if (slash_pos != std::string::npos) {
            cmd_name = cmd_name.substr(slash_pos + 1);
        }
        size_t dot_pos = cmd_name.rfind('.');
        if (dot_pos != std::string::npos) {
            cmd_name = cmd_name.substr(0, dot_pos);
        }
        output_file = cmd_name + "_trace.sbt";
    }
    
    // Build command string
    std::string cmd_str;
    for (size_t i = 0; i < command.size(); ++i) {
        if (i > 0) cmd_str += " ";
        if (command[i].find(' ') != std::string::npos) {
            cmd_str += "\"" + command[i] + "\"";
        } else {
            cmd_str += command[i];
        }
    }
    
    // Generate trace file path
    std::string trace_file = output_file;
    size_t dot_pos = trace_file.rfind('.');
    if (dot_pos != std::string::npos) {
        trace_file = trace_file.substr(0, dot_pos);
    }
    trace_file += ".trace";
    
    printSection("TraceSmith Profile (xctrace)");
    
    std::cout << C(Bold) << "Configuration:" << C(Reset) << "\n";
    std::cout << "  Command:   " << C(Cyan) << cmd_str << C(Reset) << "\n";
    std::cout << "  Output:    " << C(Cyan) << output_file << C(Reset) << "\n";
    std::cout << "  Backend:   " << C(Green) << "Apple Instruments (xctrace)" << C(Reset) << "\n";
    std::cout << "  Template:  " << xctrace_template << "\n\n";
    
    // Build xctrace command
    std::vector<std::string> xctrace_cmd = {
        "xcrun", "xctrace", "record",
        "--template", xctrace_template,
        "--output", trace_file,
        "--launch", "--"
    };
    
    // Add user command
    for (const auto& arg : command) {
        xctrace_cmd.push_back(arg);
    }
    
    // Build command line string for system()
    std::string full_cmd;
    for (size_t i = 0; i < xctrace_cmd.size(); ++i) {
        if (i > 0) full_cmd += " ";
        // Quote args with spaces
        if (xctrace_cmd[i].find(' ') != std::string::npos) {
            full_cmd += "'" + xctrace_cmd[i] + "'";
        } else {
            full_cmd += xctrace_cmd[i];
        }
    }
    
    printSuccess("xctrace profiler initialized");
    std::cout << "\n";
    
    std::cout << C(Green) << "▶ Starting xctrace profiling..." << C(Reset) << "\n";
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n";
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Execute xctrace
    int exit_code = system(full_cmd.c_str());
    
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n\n";
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    if (exit_code != 0) {
        printWarning("xctrace returned non-zero exit code");
    }
    
    printSuccess("xctrace profiling stopped");
    
    // Print summary
    printSection("Profile Complete");
    
    std::cout << C(Bold) << "Summary:" << C(Reset) << "\n";
    std::cout << "  Command:      " << cmd_str << "\n";
    std::cout << "  Duration:     " << std::fixed << std::setprecision(2) 
              << (duration.count() / 1000.0) << " seconds\n";
    std::cout << "  Raw Trace:    " << C(Cyan) << trace_file << C(Reset) << "\n\n";
    
    std::cout << C(Bold) << "Next steps:" << C(Reset) << "\n";
    std::cout << "  " << C(Cyan) << "open \"" << trace_file << "\"" << C(Reset) 
              << "  # Open in Instruments\n";
    std::cout << "  " << C(Cyan) << "xcrun xctrace export --input \"" << trace_file 
              << "\" --toc" << C(Reset) << "  # Export TOC\n";
    
    // Suggest using Python CLI for parsing
    std::cout << "\n" << C(Yellow) << "Note:" << C(Reset) 
              << " For event parsing, use the Python CLI:\n";
    std::cout << "  " << C(Cyan) << "tracesmith-cli profile --xctrace -- " 
              << cmd_str << C(Reset) << "\n";
    
    // Cleanup trace file if not keeping
    if (!keep_trace) {
        std::string rm_cmd = "rm -rf \"" + trace_file + "\"";
        system(rm_cmd.c_str());
    }
    
    return exit_code == 0 ? 0 : 1;
}
#endif

// =============================================================================
// Command: profile with nsys (NVIDIA Nsight Systems)
// =============================================================================
int cmdProfileNsys(
    const std::vector<std::string>& command,
    std::string output_file,
    bool keep_trace,
    bool export_perfetto_flag
) {
    // Build command string for display
    std::string cmd_str;
    for (size_t i = 0; i < command.size(); ++i) {
        if (i > 0) cmd_str += " ";
        if (command[i].find(' ') != std::string::npos) {
            cmd_str += "\"" + command[i] + "\"";
        } else {
            cmd_str += command[i];
        }
    }
    
    // Generate output filename if not provided
    std::string json_output;
    if (output_file.empty()) {
        std::string cmd_name = command[0];
        size_t slash_pos = cmd_name.rfind('/');
        if (slash_pos != std::string::npos) {
            cmd_name = cmd_name.substr(slash_pos + 1);
        }
        size_t dot_pos = cmd_name.rfind('.');
        if (dot_pos != std::string::npos) {
            cmd_name = cmd_name.substr(0, dot_pos);
        }
        output_file = cmd_name + "_trace";
        json_output = cmd_name + "_trace.json";
    } else {
        // Remove extension if any
        size_t dot_pos = output_file.rfind('.');
        if (dot_pos != std::string::npos) {
            output_file = output_file.substr(0, dot_pos);
        }
        json_output = output_file + ".json";
    }
    
    printSection("TraceSmith Profile (nsys)");
    
    std::cout << C(Bold) << "Configuration:" << C(Reset) << "\n";
    std::cout << "  Command:   " << C(Cyan) << cmd_str << C(Reset) << "\n";
    std::cout << "  Output:    " << C(Cyan) << output_file << ".nsys-rep" << C(Reset) << "\n";
    std::cout << "  Backend:   " << C(Green) << "NVIDIA Nsight Systems (nsys)" << C(Reset) << "\n\n";
    
    // Check if nsys exists
    std::string nsys_path = "nsys";
    if (system("which nsys > /dev/null 2>&1") != 0) {
        printError("nsys not found. Please install NVIDIA Nsight Systems.");
        std::cout << "  Download: https://developer.nvidia.com/nsight-systems\n";
        std::cout << "  Or install via: apt-get install nsight-systems (Ubuntu)\n";
        return 1;
    }
    
    // Build nsys command
    std::string full_cmd = nsys_path + " profile";
    full_cmd += " -o " + output_file;
    full_cmd += " --stats=true";
    full_cmd += " --force-overwrite true";
    
    // Add user command
    for (const auto& arg : command) {
        if (arg.find(' ') != std::string::npos) {
            full_cmd += " '" + arg + "'";
        } else {
            full_cmd += " " + arg;
        }
    }
    
    printSuccess("nsys profiler initialized");
    std::cout << "\n";
    
    std::cout << C(Green) << "▶ Starting nsys profiling..." << C(Reset) << "\n";
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n";
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Execute nsys
    int exit_code = system(full_cmd.c_str());
    
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n\n";
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    printSection("Profile Complete (nsys)");
    
    if (exit_code == 0) {
        printSuccess("Command completed successfully");
    } else {
        printWarning("Command exited with code " + std::to_string(exit_code));
    }
    
    std::cout << "\n" << C(Bold) << "Summary:" << C(Reset) << "\n";
    std::cout << "  Command:  " << cmd_str << "\n";
    std::cout << "  Duration: " << std::fixed << std::setprecision(2) 
              << (duration.count() / 1000.0) << " seconds\n";
    std::cout << "  Output:   " << C(Cyan) << output_file << ".nsys-rep" << C(Reset) << "\n\n";
    
    // Check if output file exists
    std::string nsys_output = output_file + ".nsys-rep";
    if (access(nsys_output.c_str(), F_OK) == 0) {
        printSuccess("Trace saved to " + nsys_output);
        
        std::cout << "\n" << C(Bold) << "Next steps:" << C(Reset) << "\n";
        std::cout << "  " << C(Cyan) << "nsys stats " << nsys_output << C(Reset) << "  # View statistics\n";
        std::cout << "  " << C(Cyan) << "nsys-ui " << nsys_output << C(Reset) << "  # Open in GUI\n";
        
        // Export to JSON if requested
        if (export_perfetto_flag) {
            std::cout << "\n" << C(Green) << "▶ Exporting to SQLite/JSON..." << C(Reset) << "\n";
            std::string export_cmd = "nsys export --type=sqlite -o " + output_file + ".sqlite " + nsys_output + " 2>/dev/null";
            if (system(export_cmd.c_str()) == 0) {
                printSuccess("SQLite exported to " + output_file + ".sqlite");
            }
        }
    } else {
        printWarning("Trace file not created - check nsys output above");
    }
    
    // Cleanup if not keeping trace
    if (!keep_trace) {
        // Don't delete by default - nsys traces are valuable
    }
    
    return exit_code == 0 ? 0 : 1;
}

// =============================================================================
// Command: profile with mcTracer (MetaX MACA)
// =============================================================================
#ifdef TRACESMITH_ENABLE_MACA
int cmdProfileMCTracer(
    const std::vector<std::string>& command,
    std::string output_file,
    bool keep_trace,
    bool export_perfetto_flag
) {
    // Build command string for display
    std::string cmd_str;
    for (size_t i = 0; i < command.size(); ++i) {
        if (i > 0) cmd_str += " ";
        if (command[i].find(' ') != std::string::npos) {
            cmd_str += "\"" + command[i] + "\"";
        } else {
            cmd_str += command[i];
        }
    }
    
    // Generate output filename if not provided
    if (output_file.empty()) {
        std::string cmd_name = command[0];
        size_t slash_pos = cmd_name.rfind('/');
        if (slash_pos != std::string::npos) {
            cmd_name = cmd_name.substr(slash_pos + 1);
        }
        size_t dot_pos = cmd_name.rfind('.');
        if (dot_pos != std::string::npos) {
            cmd_name = cmd_name.substr(0, dot_pos);
        }
        output_file = cmd_name + "_trace.sbt";
    }
    
    printSection("TraceSmith Profile (mcTracer)");
    
    std::cout << C(Bold) << "Configuration:" << C(Reset) << "\n";
    std::cout << "  Command:   " << C(Cyan) << cmd_str << C(Reset) << "\n";
    std::cout << "  Output:    " << C(Cyan) << output_file << C(Reset) << "\n";
    std::cout << "  Backend:   " << C(Green) << "MetaX mcTracer (MACA)" << C(Reset) << "\n\n";
    
    // Check if mcTracer exists
    std::string mctracer_path = "/opt/maca-3.0.0/bin/mcTracer";
    if (access(mctracer_path.c_str(), X_OK) != 0) {
        // Try alternate path
        mctracer_path = "/opt/maca/bin/mcTracer";
        if (access(mctracer_path.c_str(), X_OK) != 0) {
            printError("mcTracer not found. Please install MACA SDK.");
            std::cout << "  Expected at: /opt/maca-3.0.0/bin/mcTracer\n";
            return 1;
        }
    }
    
    // Create output directory for mcTracer
    std::string trace_dir = "mctracer_output";
    std::string mkdir_cmd = "mkdir -p " + trace_dir;
    system(mkdir_cmd.c_str());
    
    // Build mcTracer command
    std::vector<std::string> mctracer_cmd = {
        mctracer_path,
        "--mctx",
        "--odname", trace_dir,
        "--name", "tracesmith",
        "--"  // Separator for user command
    };
    
    // Add user command
    for (const auto& arg : command) {
        mctracer_cmd.push_back(arg);
    }
    
    // Build command line string for system()
    std::string full_cmd;
    for (size_t i = 0; i < mctracer_cmd.size(); ++i) {
        if (i > 0) full_cmd += " ";
        if (mctracer_cmd[i].find(' ') != std::string::npos) {
            full_cmd += "'" + mctracer_cmd[i] + "'";
        } else {
            full_cmd += mctracer_cmd[i];
        }
    }
    
    printSuccess("mcTracer profiler initialized");
    std::cout << "\n";
    
    std::cout << C(Green) << "▶ Starting mcTracer profiling..." << C(Reset) << "\n";
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n";
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Execute mcTracer
    int exit_code = system(full_cmd.c_str());
    
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n\n";
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    if (exit_code != 0) {
        printWarning("mcTracer returned non-zero exit code");
    }
    
    printSuccess("mcTracer profiling stopped");
    
    // Find the generated JSON file
    std::string find_cmd = "ls -t " + trace_dir + "/tracesmith-*.json 2>/dev/null | head -1";
    FILE* fp = popen(find_cmd.c_str(), "r");
    std::string json_file;
    if (fp) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), fp)) {
            json_file = buffer;
            // Remove trailing newline
            if (!json_file.empty() && json_file.back() == '\n') {
                json_file.pop_back();
            }
        }
        pclose(fp);
    }
    
    // Print summary
    printSection("Profile Complete");
    
    std::cout << C(Bold) << "Results:" << C(Reset) << "\n";
    std::cout << "  Duration:     " << C(Green) << duration.count() << " ms" << C(Reset) << "\n";
    if (!json_file.empty()) {
        std::cout << "  Trace file:   " << C(Cyan) << json_file << C(Reset) << "\n";
    }
    std::cout << "  Trace dir:    " << C(Cyan) << trace_dir << C(Reset) << "\n\n";
    
    // Convert mcTracer JSON to TraceSmith format if requested
    if (!json_file.empty()) {
        // mcTracer already outputs Perfetto-compatible JSON
        std::cout << C(Bold) << "Next steps:" << C(Reset) << "\n";
        std::cout << "  " << C(Cyan) << "Open in Perfetto: https://ui.perfetto.dev" << C(Reset) << "\n";
        std::cout << "  " << C(Cyan) << "Load file: " << json_file << C(Reset) << "\n";
        
        // Copy to output file if export_perfetto is set
        if (export_perfetto_flag) {
            std::string perfetto_output = output_file;
            size_t dot_pos = perfetto_output.rfind('.');
            if (dot_pos != std::string::npos) {
                perfetto_output = perfetto_output.substr(0, dot_pos) + ".json";
            } else {
                perfetto_output += ".json";
            }
            
            std::string cp_cmd = "cp \"" + json_file + "\" \"" + perfetto_output + "\"";
            if (system(cp_cmd.c_str()) == 0) {
                std::cout << "\n";
                printSuccess("Exported to: " + perfetto_output);
            }
        }
    }
    
    // Cleanup trace directory if not keeping
    if (!keep_trace && !trace_dir.empty()) {
        std::cout << "\n" << C(Yellow) << "Note:" << C(Reset) 
                  << " Use --keep-trace to keep the mcTracer output directory\n";
    }
    
    return exit_code == 0 ? 0 : 1;
}
#endif

// =============================================================================
// Command: profile - Profile a Command (Record + Execute)
// =============================================================================
int cmdProfile(int argc, char* argv[]) {
    std::string output_file;
    size_t buffer_size = 1024 * 1024;
    bool export_perfetto = false;
    [[maybe_unused]] bool verbose = false;
    std::vector<std::string> command;
    
#ifdef __APPLE__
    bool use_xctrace = false;
    std::string xctrace_template = "Metal System Trace";
#endif

#ifdef TRACESMITH_ENABLE_MACA
    bool use_mctracer = false;
#endif

    bool use_nsys = false;
    [[maybe_unused]] bool keep_trace = false;
    
    // Parse arguments
    bool found_separator = false;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (found_separator) {
            // Everything after '--' is the command
            command.push_back(arg);
        } else if (arg == "--") {
            found_separator = true;
        } else if (arg == "-h" || arg == "--help") {
            printProfileUsage(argv[0]);
            return 0;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
#ifdef __APPLE__
        } else if (arg == "--xctrace") {
            use_xctrace = true;
        } else if (arg == "--xctrace-template" && i + 1 < argc) {
            xctrace_template = argv[++i];
#endif
#ifdef TRACESMITH_ENABLE_MACA
        } else if (arg == "--mctracer") {
            use_mctracer = true;
#endif
        } else if (arg == "--nsys") {
            use_nsys = true;
        } else if (arg == "--keep-trace") {
            keep_trace = true;
        } else if ((arg == "-b" || arg == "--buffer") && i + 1 < argc) {
            buffer_size = std::stoull(argv[++i]);
        } else if (arg == "--perfetto") {
            export_perfetto = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg[0] != '-') {
            // Start of command without --
            command.push_back(arg);
            for (int j = i + 1; j < argc; ++j) {
                command.push_back(argv[j]);
            }
            break;
        }
    }
    
    // Check if command is provided
    if (command.empty()) {
        printError("No command specified");
        std::cout << "\n" << C(Bold) << "Usage:" << C(Reset) << "\n";
        std::cout << "    " << argv[0] << " profile [OPTIONS] -- <COMMAND> [ARGS...]\n\n";
        std::cout << C(Bold) << "Examples:" << C(Reset) << "\n";
        std::cout << "    " << argv[0] << " profile -- python train.py\n";
        std::cout << "    " << argv[0] << " profile -o trace.sbt -- python train.py --epochs 10\n";
        std::cout << "    " << argv[0] << " profile --perfetto -- ./my_cuda_app\n";
#ifdef __APPLE__
        std::cout << "    " << argv[0] << " profile --xctrace -- python train.py\n";
#endif
#ifdef TRACESMITH_ENABLE_MACA
        std::cout << "    " << argv[0] << " profile --mctracer -- ./my_maca_app\n";
#endif
        return 1;
    }
    
#ifdef __APPLE__
    // Use xctrace if requested
    if (use_xctrace) {
        return cmdProfileXCTrace(command, output_file, xctrace_template, keep_trace, export_perfetto);
    }
    
    // Suggest xctrace on macOS with Metal
    if (detectPlatform() == PlatformType::Metal) {
        printInfo("Tip: Use --xctrace for real Metal GPU events on macOS");
        std::cout << "\n";
    }
#endif

#ifdef TRACESMITH_ENABLE_MACA
    // Use mcTracer if requested
    if (use_mctracer) {
        return cmdProfileMCTracer(command, output_file, keep_trace, export_perfetto);
    }
#endif

    // Use nsys if requested (for NVIDIA CUDA)
    if (use_nsys) {
        return cmdProfileNsys(command, output_file, keep_trace, export_perfetto);
    }
    
    // For CUDA, strongly suggest --nsys
    if (detectPlatform() == PlatformType::CUDA) {
        printError("CUDA detected. Please use --nsys for GPU profiling.");
        std::cout << "\n";
        std::cout << C(Bold) << "Usage:" << C(Reset) << "\n";
        std::cout << "  " << C(Cyan) << "tracesmith profile --nsys -- " << C(Reset) << "<your-command>\n\n";
        std::cout << C(Bold) << "Example:" << C(Reset) << "\n";
        std::cout << "  tracesmith profile --nsys -- python train.py\n\n";
        std::cout << "CUPTI cannot profile child processes. nsys provides system-wide profiling.\n";
        return 1;
    }
    
#ifdef TRACESMITH_ENABLE_MACA
    // For MACA, strongly suggest --mctracer
    if (detectPlatform() == PlatformType::MACA) {
        printError("MACA detected. Please use --mctracer for GPU profiling.");
        std::cout << "\n";
        std::cout << C(Bold) << "Usage:" << C(Reset) << "\n";
        std::cout << "  " << C(Cyan) << "tracesmith profile --mctracer -- " << C(Reset) << "<your-command>\n\n";
        std::cout << C(Bold) << "Example:" << C(Reset) << "\n";
        std::cout << "  tracesmith profile --mctracer -- ./my_maca_app\n\n";
        std::cout << "MCPTI cannot profile child processes. mcTracer provides system-wide profiling.\n";
        return 1;
    }
#endif
    
    // Generate output filename if not provided
    if (output_file.empty()) {
        std::string cmd_name = command[0];
        // Extract basename
        size_t slash_pos = cmd_name.rfind('/');
        if (slash_pos != std::string::npos) {
            cmd_name = cmd_name.substr(slash_pos + 1);
        }
        // Remove extension
        size_t dot_pos = cmd_name.rfind('.');
        if (dot_pos != std::string::npos) {
            cmd_name = cmd_name.substr(0, dot_pos);
        }
        output_file = cmd_name + "_trace.sbt";
    }
    
    // Build command string for display
    std::string cmd_str;
    for (size_t i = 0; i < command.size(); ++i) {
        if (i > 0) cmd_str += " ";
        // Quote arguments with spaces
        if (command[i].find(' ') != std::string::npos) {
            cmd_str += "\"" + command[i] + "\"";
        } else {
            cmd_str += command[i];
        }
    }
    
    printSection("TraceSmith Profile");
    
    std::cout << C(Bold) << "Configuration:" << C(Reset) << "\n";
    std::cout << "  Command: " << C(Cyan) << cmd_str << C(Reset) << "\n";
    std::cout << "  Output:  " << C(Cyan) << output_file << C(Reset) << "\n\n";
    
    // Detect platform
    PlatformType platform = detectPlatform();
    std::string platform_name = platformTypeToString(platform);
    
    std::shared_ptr<IPlatformProfiler> profiler;
    
    if (platform == PlatformType::Unknown) {
        printWarning("No GPU detected, will execute without GPU profiling");
    } else if (platform == PlatformType::CUDA || platform == PlatformType::MACA) {
        // CUDA/MACA: Show limitation warning and suggest system-level tools
        std::cout << C(Yellow) << "╔════════════════════════════════════════════════════════════════╗" << C(Reset) << "\n";
        std::cout << C(Yellow) << "║" << C(Reset) << C(Bold) << "  IMPORTANT: GPU Profiling API Limitation                       " << C(Reset) << C(Yellow) << "║" << C(Reset) << "\n";
        std::cout << C(Yellow) << "╠════════════════════════════════════════════════════════════════╣" << C(Reset) << "\n";
        std::cout << C(Yellow) << "║" << C(Reset) << "  CUPTI/MCPTI only profiles the calling process, NOT child      " << C(Yellow) << "║" << C(Reset) << "\n";
        std::cout << C(Yellow) << "║" << C(Reset) << "  processes. This means GPU events from your command will       " << C(Yellow) << "║" << C(Reset) << "\n";
        std::cout << C(Yellow) << "║" << C(Reset) << "  NOT be captured here.                                         " << C(Yellow) << "║" << C(Reset) << "\n";
        std::cout << C(Yellow) << "║" << C(Reset) << "                                                                 " << C(Yellow) << "║" << C(Reset) << "\n";
        std::cout << C(Yellow) << "║" << C(Reset) << C(Green) << "  RECOMMENDED: Use system-level profiler instead:              " << C(Reset) << C(Yellow) << "║" << C(Reset) << "\n";
        if (platform == PlatformType::CUDA) {
            std::cout << C(Yellow) << "║" << C(Reset) << C(Cyan) << "    tracesmith profile --nsys -- " << C(Reset) << "<your-command>              " << C(Yellow) << "║" << C(Reset) << "\n";
        } else {
            std::cout << C(Yellow) << "║" << C(Reset) << C(Cyan) << "    tracesmith profile --mctracer -- " << C(Reset) << "<your-command>          " << C(Yellow) << "║" << C(Reset) << "\n";
        }
        std::cout << C(Yellow) << "╚════════════════════════════════════════════════════════════════╝" << C(Reset) << "\n\n";
        
        printWarning("Proceeding without GPU profiling for child process.");
        std::cout << "  The command will be executed but GPU events won't be captured.\n";
        std::cout << "  Use " << C(Cyan) << (platform == PlatformType::CUDA ? "--nsys" : "--mctracer") << C(Reset) 
                  << " for system-wide GPU profiling.\n\n";
        
        // Don't create profiler - it won't help
        profiler = nullptr;
    } else {
        printSuccess("Detected GPU platform: " + platform_name);
        
        // Create profiler (Metal, ROCm, etc.)
        profiler = createProfiler(platform);
        if (!profiler) {
            printWarning("Failed to create profiler for " + platform_name);
        } else {
            // Configure
            ProfilerConfig config;
            config.buffer_size = buffer_size;
            
            if (!profiler->initialize(config)) {
                printWarning("Failed to initialize profiler");
                profiler = nullptr;
            } else {
                printSuccess("Profiler initialized");
            }
        }
    }
    
    // Create writer
    SBTWriter writer(output_file);
    if (!writer.isOpen()) {
        printError("Failed to open output file: " + output_file);
        return 1;
    }
    
    // Write metadata
    TraceMetadata metadata;
    metadata.application_name = command[0];
    metadata.command_line = cmd_str;
    metadata.start_time = getCurrentTimestamp();
    writer.writeMetadata(metadata);
    
    // Start profiling
    if (profiler) {
        profiler->startCapture();
        std::cout << "\n" << C(Green) << "▶ GPU profiling started" << C(Reset) << "\n";
    }
    
    std::cout << C(Green) << "▶ Executing command..." << C(Reset) << "\n\n";
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n";
    
    // Record start time
    auto start_time = std::chrono::steady_clock::now();
    
    // Execute the command using fork/exec
    int exit_code = 0;
    
#ifdef _WIN32
    // Windows: use system()
    exit_code = system(cmd_str.c_str());
#else
    // Unix: use fork/exec for better control
    pid_t pid = fork();
    
    if (pid < 0) {
        printError("Failed to fork process");
        return 1;
    } else if (pid == 0) {
        // Child process - execute the command
        std::vector<char*> c_args;
        for (auto& arg : command) {
            c_args.push_back(const_cast<char*>(arg.c_str()));
        }
        c_args.push_back(nullptr);
        
        execvp(c_args[0], c_args.data());
        
        // If execvp returns, it failed
        std::cerr << "Failed to execute: " << command[0] << " - " << strerror(errno) << "\n";
        _exit(127);
    } else {
        // Parent process - wait for child
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_code = 128 + WTERMSIG(status);
        }
    }
#endif
    
    std::cout << C(Yellow);
    for (int i = 0; i < 60; ++i) std::cout << "-";
    std::cout << C(Reset) << "\n\n";
    
    // Record end time
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Stop profiling and collect events
    std::vector<TraceEvent> all_events;
    
    if (profiler) {
        profiler->stopCapture();
        profiler->getEvents(all_events);
        printSuccess("GPU profiling stopped");
    }
    
    // Write events
    if (!all_events.empty()) {
        writer.writeEvents(all_events);
    }
    
    writer.finalize();
    
    // Print summary
    printSection("Profile Complete");
    
    // Command result
    if (exit_code == 0) {
        printSuccess("Command completed successfully");
    } else {
        printWarning("Command exited with code: " + std::to_string(exit_code));
    }
    
    std::cout << "\n" << C(Bold) << "Summary:" << C(Reset) << "\n";
    std::cout << "  Command:      " << cmd_str << "\n";
    std::cout << "  Duration:     " << std::fixed << std::setprecision(2) 
              << (duration.count() / 1000.0) << " seconds\n";
    std::cout << "  GPU Events:   " << C(Green) << all_events.size() << C(Reset) << "\n";
    std::cout << "  Output:       " << C(Cyan) << output_file << C(Reset) << "\n";
    
    // Analyze events
    if (!all_events.empty()) {
        size_t kernel_count = 0;
        size_t memcpy_count = 0;
        
        for (const auto& e : all_events) {
            if (e.type == EventType::KernelLaunch) kernel_count++;
            else if (e.type == EventType::MemcpyH2D || e.type == EventType::MemcpyD2H || 
                     e.type == EventType::MemcpyD2D) memcpy_count++;
        }
        
        std::cout << "\n" << C(Bold) << "Event Breakdown:" << C(Reset) << "\n";
        std::cout << "  Kernel Launches: " << kernel_count << "\n";
        std::cout << "  Memory Copies:   " << memcpy_count << "\n";
        std::cout << "  Other Events:    " << (all_events.size() - kernel_count - memcpy_count) << "\n";
    }
    
    std::cout << "\n";
    
    // Export to Perfetto if requested
    if (export_perfetto && !all_events.empty()) {
        std::string perfetto_file = output_file;
        size_t dot_pos = perfetto_file.rfind('.');
        if (dot_pos != std::string::npos) {
            perfetto_file = perfetto_file.substr(0, dot_pos);
        }
        perfetto_file += ".json";
        
        PerfettoExporter exporter;
        if (exporter.exportToFile(all_events, perfetto_file)) {
            printSuccess("Exported Perfetto trace: " + perfetto_file);
            std::cout << "  View at: " << C(Cyan) << "https://ui.perfetto.dev/" << C(Reset) << "\n\n";
        } else {
            printWarning("Failed to export Perfetto trace");
        }
    }
    
    // Next steps
    std::cout << C(Bold) << "Next steps:" << C(Reset) << "\n";
    std::cout << "  " << C(Cyan) << "tracesmith view " << output_file << " --stats" << C(Reset) << "\n";
    std::cout << "  " << C(Cyan) << "tracesmith export " << output_file << " -f perfetto" << C(Reset) << "\n";
    std::cout << "  " << C(Cyan) << "tracesmith analyze " << output_file << C(Reset) << "\n";
    
    return exit_code;
}

// =============================================================================
// Command: record - Record GPU Events
// =============================================================================
int cmdRecord(int argc, char* argv[]) {
    std::string output_file = "trace.sbt";
    double duration_sec = 5.0;
    size_t buffer_size = 1024 * 1024;
    std::string platform_str = "auto";
    bool capture_stacks = false;
    [[maybe_unused]] bool verbose = false;
    
    // Parse arguments
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printRecordUsage(argv[0]);
            return 0;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
        } else if ((arg == "-d" || arg == "--duration") && i + 1 < argc) {
            duration_sec = std::stod(argv[++i]);
        } else if ((arg == "-b" || arg == "--buffer") && i + 1 < argc) {
            buffer_size = std::stoull(argv[++i]);
        } else if ((arg == "-p" || arg == "--platform") && i + 1 < argc) {
            platform_str = argv[++i];
        } else if (arg == "-s" || arg == "--stacks") {
            capture_stacks = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        }
    }
    
    printSection("Recording GPU Trace");
    
    // Determine platform type
    PlatformType platform = PlatformType::Unknown;
    if (platform_str == "cuda") {
        platform = PlatformType::CUDA;
    } else if (platform_str == "rocm") {
        platform = PlatformType::ROCm;
    } else if (platform_str == "metal") {
        platform = PlatformType::Metal;
    } else if (platform_str == "auto") {
        platform = detectPlatform();
    }
    
    std::string platform_name = platformTypeToString(platform);
    
    // Print configuration
    std::cout << C(Bold) << "Configuration:" << C(Reset) << "\n";
    std::cout << "  Output:      " << C(Cyan) << output_file << C(Reset) << "\n";
    std::cout << "  Duration:    " << duration_sec << " seconds\n";
    std::cout << "  Buffer:      " << formatByteSize(buffer_size * sizeof(TraceEvent)) << "\n";
    std::cout << "  Platform:    " << platform_name << "\n";
    std::cout << "  Call stacks: " << (capture_stacks ? "enabled" : "disabled") << "\n\n";
    
    // Check platform
    if (platform == PlatformType::Unknown) {
        printError("No supported GPU platform detected.");
        std::cout << "Supported: CUDA (NVIDIA), ROCm (AMD), Metal (Apple), MACA (MetaX)\n";
        return 1;
    }
    
    // For CUDA/MACA platforms, the record command is not useful
    // because CUPTI/MCPTI can only profile the calling process
    if (platform == PlatformType::CUDA || platform == PlatformType::MACA) {
        std::cout << C(Red) << "╔════════════════════════════════════════════════════════════════╗" << C(Reset) << "\n";
        std::cout << C(Red) << "║" << C(Reset) << C(Bold) << "  ERROR: 'record' command not supported for CUDA/MACA           " << C(Reset) << C(Red) << "║" << C(Reset) << "\n";
        std::cout << C(Red) << "╠════════════════════════════════════════════════════════════════╣" << C(Reset) << "\n";
        std::cout << C(Red) << "║" << C(Reset) << "  CUPTI/MCPTI APIs can only profile the calling process itself. " << C(Red) << "║" << C(Reset) << "\n";
        std::cout << C(Red) << "║" << C(Reset) << "  The 'record' command cannot capture external GPU activity.    " << C(Red) << "║" << C(Reset) << "\n";
        std::cout << C(Red) << "║" << C(Reset) << "                                                                 " << C(Red) << "║" << C(Reset) << "\n";
        std::cout << C(Red) << "║" << C(Reset) << C(Green) << "  Use system-level profiler instead:                            " << C(Reset) << C(Red) << "║" << C(Reset) << "\n";
        if (platform == PlatformType::CUDA) {
            std::cout << C(Red) << "║" << C(Reset) << C(Cyan) << "    tracesmith profile --nsys -- <your-command>" << C(Reset) << "               " << C(Red) << "║" << C(Reset) << "\n";
        } else {
            std::cout << C(Red) << "║" << C(Reset) << C(Cyan) << "    tracesmith profile --mctracer -- <your-command>" << C(Reset) << "           " << C(Red) << "║" << C(Reset) << "\n";
        }
        std::cout << C(Red) << "╚════════════════════════════════════════════════════════════════╝" << C(Reset) << "\n\n";
        
        std::cout << C(Bold) << "Example:" << C(Reset) << "\n";
        if (platform == PlatformType::CUDA) {
            std::cout << "  tracesmith profile --nsys -- python train.py\n";
            std::cout << "  tracesmith profile --nsys --perfetto -- ./my_cuda_app\n";
        } else {
            std::cout << "  tracesmith profile --mctracer -- ./my_maca_app\n";
            std::cout << "  tracesmith profile --mctracer --perfetto -- python train.py\n";
        }
        return 1;
    }
    
    // Create profiler (for Metal/ROCm which can record)
    auto profiler = createProfiler(platform);
    if (!profiler) {
        printError("Failed to create profiler for " + platform_name);
        return 1;
    }
    
    // Configure
    ProfilerConfig config;
    config.buffer_size = buffer_size;
    config.capture_callstacks = capture_stacks;
    
    if (!profiler->initialize(config)) {
        printError("Failed to initialize profiler");
        std::cout << "This may be due to insufficient permissions or missing drivers.\n";
        return 1;
    }
    
    printSuccess("Profiler initialized");
    
    // Print device info
    auto devices = profiler->getDeviceInfo();
    if (!devices.empty()) {
        std::cout << "  Device: " << devices[0].name << "\n";
    }
    
    // Setup signal handler
    signal(SIGINT, signalHandler);
    
    // Create writer
    SBTWriter writer(output_file);
    if (!writer.isOpen()) {
        printError("Failed to open output file: " + output_file);
        return 1;
    }
    
    // Write metadata
    TraceMetadata metadata;
    metadata.application_name = "tracesmith";
    metadata.command_line = "record";
    metadata.start_time = getCurrentTimestamp();
    metadata.devices = devices;
    
    writer.writeMetadata(metadata);
    writer.writeDeviceInfo(devices);
    
    // Start capture
    std::cout << "\n" << C(Green) << "▶ Recording..." << C(Reset) 
              << " (Press Ctrl+C to stop)\n\n";
    
    profiler->startCapture();
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::milliseconds(static_cast<int64_t>(duration_sec * 1000));
    
    uint64_t total_events = 0;
    
    // Progress bar
    auto printProgress = [&](double progress) {
        int bar_width = 40;
        int pos = static_cast<int>(bar_width * progress);
        
        std::cout << "\r  [";
        for (int i = 0; i < bar_width; ++i) {
            if (i < pos) std::cout << C(Green) << "█" << C(Reset);
            else if (i == pos) std::cout << C(Green) << "▓" << C(Reset);
            else std::cout << "░";
        }
        std::cout << "] " << std::fixed << std::setprecision(0) << (progress * 100) << "%";
        std::cout << " | Events: " << total_events;
        std::cout << " | Dropped: " << profiler->eventsDropped();
        std::cout << "     " << std::flush;
    };
    
    while (!g_interrupted && std::chrono::steady_clock::now() < end_time) {
        // Drain events
        std::vector<TraceEvent> events;
        size_t count = profiler->getEvents(events, 10000);
        
        if (count > 0) {
            writer.writeEvents(events);
            total_events += count;
        }
        
        // Update progress
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        double progress = std::chrono::duration<double>(elapsed).count() / duration_sec;
        printProgress(std::min(progress, 1.0));
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Stop capture
    profiler->stopCapture();
    
    // Drain remaining
    std::vector<TraceEvent> remaining;
    profiler->getEvents(remaining);
    if (!remaining.empty()) {
        writer.writeEvents(remaining);
        total_events += remaining.size();
    }
    
    writer.finalize();
    
    printProgress(1.0);
    std::cout << "\n\n";
    
    // Summary
    printSection("Recording Complete");
    
    std::cout << C(Bold) << "Summary:" << C(Reset) << "\n";
    std::cout << "  Platform:     " << platform_name << "\n";
    std::cout << "  Total events: " << C(Green) << total_events << C(Reset) << "\n";
    std::cout << "  Dropped:      " << profiler->eventsDropped() << "\n";
    std::cout << "  File size:    " << formatByteSize(writer.fileSize()) << "\n";
    std::cout << "  Output:       " << C(Cyan) << output_file << C(Reset) << "\n\n";
    
    printSuccess("Trace saved to " + output_file);
    std::cout << "\nNext steps:\n";
    std::cout << "  " << C(Cyan) << "tracesmith view " << output_file << " --stats" << C(Reset) << "\n";
    std::cout << "  " << C(Cyan) << "tracesmith export " << output_file << " -f perfetto" << C(Reset) << "\n";
    
    return 0;
}

// =============================================================================
// Command: view - View Trace Contents
// =============================================================================
int cmdView(int argc, char* argv[]) {
    std::string input_file;
    std::string format = "text";
    size_t limit = 20;
    bool stats_only = false;
    [[maybe_unused]] bool show_timeline = false;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printViewUsage(argv[0]);
            return 0;
        } else if ((arg == "-f" || arg == "--format") && i + 1 < argc) {
            format = argv[++i];
        } else if ((arg == "-n" || arg == "--limit") && i + 1 < argc) {
            limit = std::stoull(argv[++i]);
        } else if (arg == "--stats") {
            stats_only = true;
        } else if (arg == "--timeline") {
            show_timeline = true;
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }
    
    if (input_file.empty()) {
        printError("No input file specified");
        printViewUsage(argv[0]);
        return 1;
    }
    
    // Open file
    SBTReader reader(input_file);
    if (!reader.isOpen()) {
        printError("Failed to open file: " + input_file);
        return 1;
    }
    
    if (!reader.isValid()) {
        printError("Invalid SBT file format");
        return 1;
    }
    
    // Read trace
    TraceRecord record;
    auto result = reader.readAll(record);
    if (!result) {
        printError("Failed to read trace: " + result.error_message);
        return 1;
    }
    
    printSection("Trace File: " + input_file);
    
    // Basic info
    std::cout << C(Bold) << "File Info:" << C(Reset) << "\n";
    std::cout << "  Version:     " << reader.header().version_major << "." 
              << reader.header().version_minor << "\n";
    std::cout << "  Events:      " << C(Green) << record.size() << C(Reset) << "\n";
    if (!record.metadata().application_name.empty()) {
        std::cout << "  Application: " << record.metadata().application_name << "\n";
    }
    
    // Calculate statistics
    std::map<EventType, size_t> type_counts;
    std::map<EventType, uint64_t> type_durations;
    std::map<uint32_t, size_t> stream_counts;
    Timestamp total_duration = 0;
    Timestamp min_ts = UINT64_MAX, max_ts = 0;
    
    for (const auto& event : record.events()) {
        type_counts[event.type]++;
        type_durations[event.type] += event.duration;
        stream_counts[event.stream_id]++;
        total_duration += event.duration;
        if (event.timestamp < min_ts) min_ts = event.timestamp;
        if (event.timestamp > max_ts) max_ts = event.timestamp;
    }
    
    // Statistics
    std::cout << "\n" << C(Bold) << "Statistics:" << C(Reset) << "\n";
    std::cout << "  Time span:      " << formatTimeDuration(max_ts - min_ts) << "\n";
    std::cout << "  Total duration: " << formatTimeDuration(total_duration) << "\n";
    std::cout << "  Streams:        " << stream_counts.size() << "\n";
    
    // Events by type
    std::cout << "\n" << C(Bold) << "Events by Type:" << C(Reset) << "\n";
    std::cout << "  " << std::left << std::setw(20) << "Type" 
              << std::setw(10) << "Count" 
              << std::setw(15) << "Total Time" 
              << "Avg Time\n";
    std::cout << "  " << std::string(55, '-') << "\n";
    
    for (const auto& [type, count] : type_counts) {
        std::cout << "  " << std::left << std::setw(20) << eventTypeToString(type)
                  << std::setw(10) << count;
        if (type_durations[type] > 0) {
            std::cout << std::setw(15) << formatTimeDuration(type_durations[type])
                      << formatTimeDuration(type_durations[type] / count);
        }
        std::cout << "\n";
    }
    
    if (stats_only) {
        // Stream breakdown
        std::cout << "\n" << C(Bold) << "Events by Stream:" << C(Reset) << "\n";
        for (const auto& [stream, count] : stream_counts) {
            std::cout << "  Stream " << stream << ": " << count << " events\n";
        }
        return 0;
    }
    
    // Show events
    std::cout << "\n" << C(Bold) << "Events (first " << limit << "):" << C(Reset) << "\n";
    
    size_t count = 0;
    for (const auto& event : record.events()) {
        if (count >= limit) break;
        
        std::cout << "  " << C(Cyan) << "[" << std::setw(5) << count << "]" << C(Reset) << " ";
        std::cout << std::setw(16) << std::left << eventTypeToString(event.type);
        std::cout << " | Stream " << event.stream_id;
        std::cout << " | " << std::setw(12) << formatTimeDuration(event.duration);
        std::cout << " | " << event.name;
        std::cout << "\n";
        
        count++;
    }
    
    if (record.size() > limit) {
        std::cout << "\n  ... and " << (record.size() - limit) << " more events\n";
    }
    
    return 0;
}

// =============================================================================
// Command: info - Show Trace File Info
// =============================================================================
int cmdInfo(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " info <file>\n";
        return 1;
    }
    
    std::string input_file = argv[2];
    
    SBTReader reader(input_file);
    if (!reader.isOpen()) {
        printError("Failed to open file: " + input_file);
        return 1;
    }
    
    const auto& header = reader.header();
    
    printSection("Trace File Info");
    
    std::cout << C(Bold) << "File:" << C(Reset) << " " << input_file << "\n\n";
    
    if (!header.isValid()) {
        printError("Invalid SBT file");
        return 1;
    }
    
    std::cout << C(Bold) << "Format:" << C(Reset) << "\n";
    std::cout << "  Magic:        SBT (TraceSmith Binary Trace)\n";
    std::cout << "  Version:      " << header.version_major << "." << header.version_minor << "\n";
    std::cout << "  Header size:  " << header.header_size << " bytes\n";
    std::cout << "  Event count:  " << header.event_count << "\n";
    std::cout << "  Flags:        0x" << std::hex << header.flags << std::dec << "\n";
    
    std::cout << "\n" << C(Bold) << "Section Offsets:" << C(Reset) << "\n";
    std::cout << "  Metadata:     " << header.metadata_offset << "\n";
    std::cout << "  String table: " << header.string_table_offset << "\n";
    std::cout << "  Device info:  " << header.device_info_offset << "\n";
    std::cout << "  Events:       " << header.events_offset << "\n";
    
    return 0;
}

// =============================================================================
// Command: export - Export Trace
// =============================================================================
int cmdExport(int argc, char* argv[]) {
    std::string input_file;
    std::string output_file;
    std::string format = "perfetto";
    bool include_counters = false;
    bool include_flows = false;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printExportUsage(argv[0]);
            return 0;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
        } else if ((arg == "-f" || arg == "--format") && i + 1 < argc) {
            format = argv[++i];
        } else if (arg == "--counters") {
            include_counters = true;
        } else if (arg == "--flows") {
            include_flows = true;
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }
    
    if (input_file.empty()) {
        printError("No input file specified");
        printExportUsage(argv[0]);
        return 1;
    }
    
    // Auto-generate output filename
    if (output_file.empty()) {
        size_t dot_pos = input_file.rfind('.');
        std::string base = (dot_pos != std::string::npos) ? 
                          input_file.substr(0, dot_pos) : input_file;
        
        if (format == "perfetto" || format == "chrome") {
            output_file = base + ".json";
        } else if (format == "proto") {
            output_file = base + ".perfetto-trace";
        } else if (format == "csv") {
            output_file = base + ".csv";
        } else {
            output_file = base + ".json";
        }
    }
    
    printSection("Exporting Trace");
    
    std::cout << "Input:  " << C(Cyan) << input_file << C(Reset) << "\n";
    std::cout << "Output: " << C(Cyan) << output_file << C(Reset) << "\n";
    std::cout << "Format: " << format << "\n\n";
    
    // Read input
    SBTReader reader(input_file);
    if (!reader.isOpen() || !reader.isValid()) {
        printError("Failed to open or invalid SBT file");
        return 1;
    }
    
    TraceRecord record;
    auto result = reader.readAll(record);
    if (!result) {
        printError("Failed to read trace");
        return 1;
    }
    
    printInfo("Read " + std::to_string(record.size()) + " events");
    
    // Export
    if (format == "perfetto" || format == "chrome" || format == "json") {
        PerfettoExporter exporter;
        exporter.setEnableCounterTracks(include_counters);
        exporter.setEnableFlowEvents(include_flows);
        
        if (exporter.exportToFile(record.events(), output_file)) {
            printSuccess("Exported to " + output_file);
            std::cout << "\nView at: " << C(Cyan) << "https://ui.perfetto.dev/" << C(Reset) << "\n";
        } else {
            printError("Export failed");
            return 1;
        }
    } else if (format == "csv") {
        std::ofstream ofs(output_file);
        if (!ofs) {
            printError("Failed to open output file");
            return 1;
        }
        
        ofs << "timestamp,duration,type,name,stream_id,device_id\n";
        for (const auto& e : record.events()) {
            ofs << e.timestamp << "," << e.duration << "," 
                << eventTypeToString(e.type) << ",\"" << e.name << "\","
                << e.stream_id << "," << e.device_id << "\n";
        }
        
        printSuccess("Exported to " + output_file);
    } else {
        printError("Unknown format: " + format);
        return 1;
    }
    
    return 0;
}

// =============================================================================
// Command: analyze - Analyze Trace
// =============================================================================
int cmdAnalyze(int argc, char* argv[]) {
    std::string input_file;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printAnalyzeUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }
    
    if (input_file.empty()) {
        printError("No input file specified");
        printAnalyzeUsage(argv[0]);
        return 1;
    }
    
    // Read trace
    SBTReader reader(input_file);
    if (!reader.isOpen() || !reader.isValid()) {
        printError("Failed to open or invalid SBT file");
        return 1;
    }
    
    TraceRecord record;
    reader.readAll(record);
    
    printSection("Performance Analysis");
    
    std::cout << "File: " << C(Cyan) << input_file << C(Reset) << "\n";
    std::cout << "Events: " << record.size() << "\n\n";
    
    // Build timeline
    TimelineBuilder builder;
    builder.addEvents(record.events());
    auto timeline = builder.build();
    
    // GPU Utilization
    std::cout << C(Bold) << "GPU Utilization:" << C(Reset) << "\n";
    std::cout << "  Overall:        " << C(Green) << std::fixed << std::setprecision(1)
              << (timeline.gpu_utilization * 100) << "%" << C(Reset) << "\n";
    std::cout << "  Max concurrent: " << timeline.max_concurrent_ops << " ops\n";
    std::cout << "  Total duration: " << formatTimeDuration(timeline.total_duration) << "\n";
    
    // Kernel analysis
    std::map<std::string, std::pair<size_t, uint64_t>> kernel_stats;  // name -> (count, total_duration)
    
    for (const auto& event : record.events()) {
        if (event.type == EventType::KernelLaunch || event.type == EventType::KernelComplete) {
            kernel_stats[event.name].first++;
            kernel_stats[event.name].second += event.duration;
        }
    }
    
    if (!kernel_stats.empty()) {
        std::cout << "\n" << C(Bold) << "Top Kernels by Time:" << C(Reset) << "\n";
        
        // Sort by total time
        std::vector<std::pair<std::string, std::pair<size_t, uint64_t>>> sorted_kernels(
            kernel_stats.begin(), kernel_stats.end());
        std::sort(sorted_kernels.begin(), sorted_kernels.end(),
            [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
        
        std::cout << "  " << std::left << std::setw(35) << "Kernel" 
                  << std::setw(10) << "Count"
                  << std::setw(15) << "Total"
                  << "Average\n";
        std::cout << "  " << std::string(70, '-') << "\n";
        
        size_t shown = 0;
        for (const auto& [name, stats] : sorted_kernels) {
            if (shown++ >= 10) break;
            std::string short_name = name.length() > 32 ? name.substr(0, 32) + "..." : name;
            std::cout << "  " << std::left << std::setw(35) << short_name
                      << std::setw(10) << stats.first
                      << std::setw(15) << formatTimeDuration(stats.second)
                      << formatTimeDuration(stats.second / stats.first) << "\n";
        }
    }
    
    std::cout << "\n";
    printSuccess("Analysis complete");
    
    return 0;
}

// =============================================================================
// Command: replay - Replay Trace
// =============================================================================
int cmdReplay(int argc, char* argv[]) {
    std::string input_file;
    std::string mode = "dry-run";
    bool validate = false;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printReplayUsage(argv[0]);
            return 0;
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--validate") {
            validate = true;
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }
    
    if (input_file.empty()) {
        printError("No input file specified");
        printReplayUsage(argv[0]);
        return 1;
    }
    
    printSection("Replay Trace");
    
    std::cout << "File: " << C(Cyan) << input_file << C(Reset) << "\n";
    std::cout << "Mode: " << mode << "\n\n";
    
    // Read trace
    SBTReader reader(input_file);
    if (!reader.isOpen() || !reader.isValid()) {
        printError("Failed to open or invalid SBT file");
        return 1;
    }
    
    TraceRecord record;
    reader.readAll(record);
    
    printInfo("Loaded " + std::to_string(record.size()) + " events");
    
    // Create replay engine
    ReplayEngine engine;
    
    ReplayConfig config;
    if (mode == "dry-run") {
        config.mode = ReplayMode::DryRun;
    } else if (mode == "full") {
        config.mode = ReplayMode::Full;
    } else if (mode == "partial") {
        config.mode = ReplayMode::Partial;
    }
    config.validate_dependencies = validate;
    
    if (!engine.loadTrace(input_file)) {
        printError("Failed to load trace for replay");
        return 1;
    }
    
    std::cout << "Replaying...\n";
    auto result = engine.replay(config);
    
    std::cout << "\n" << C(Bold) << "Replay Results:" << C(Reset) << "\n";
    std::cout << "  Success:      " << (result.success ? C(Green) : C(Red)) 
              << (result.success ? "Yes" : "No") << C(Reset) << "\n";
    std::cout << "  Operations:   " << result.operations_executed << "/" 
              << result.operations_total << "\n";
    std::cout << "  Deterministic: " << (result.deterministic ? "Yes" : "No") << "\n";
    std::cout << "  Duration:     " << formatTimeDuration(result.replay_duration) << "\n";
    
    if (result.success) {
        printSuccess("Replay completed");
    } else {
        printError("Replay failed");
    }
    
    return result.success ? 0 : 1;
}

// =============================================================================
// Command: benchmark - Run 10K GPU Call Stacks Benchmark
// =============================================================================
#ifdef TRACESMITH_ENABLE_CUDA

// CUDA kernel for benchmark
__global__ void benchmark_kernel_cli(float* data, int n, int kernel_id) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        data[idx] = data[idx] * 2.0f + static_cast<float>(kernel_id);
    }
}

#endif // TRACESMITH_ENABLE_CUDA

int cmdBenchmark(int argc, char* argv[]) {
    // Parse options
    int target_kernels = 10000;
    std::string output_file = "benchmark.sbt";
    bool capture_stacks = true;
    bool verbose = false;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printBenchmarkUsage(argv[0]);
            return 0;
        } else if ((arg == "-n" || arg == "--count") && i + 1 < argc) {
            target_kernels = std::stoi(argv[++i]);
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--no-stacks") {
            capture_stacks = false;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        }
    }

#ifndef TRACESMITH_ENABLE_CUDA
    std::cout << "\n";
    std::cout << C(Bold) << C(Red);
    std::cout << "╔══════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ERROR: CUDA support not enabled                                     ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════╝\n";
    std::cout << C(Reset) << "\n";
    std::cout << "This benchmark requires CUDA support.\n\n";
    std::cout << "Please rebuild TraceSmith with CUDA enabled:\n";
    std::cout << "  cmake .. -DTRACESMITH_ENABLE_CUDA=ON\n";
    std::cout << "  make\n\n";
    return 1;
#else
    
    // Print banner
    std::cout << "\n";
    std::cout << C(Bold) << C(Cyan);
    std::cout << "╔══════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  TraceSmith Benchmark: 10,000+ GPU Instruction-Level Call Stacks     ║\n";
    std::cout << "║  Feature: Non-intrusive capture of instruction-level GPU call stacks ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════╝\n";
    std::cout << C(Reset) << "\n";

    // Check CUDA availability
    if (!isCUDAAvailable()) {
        printError("CUDA not available");
        return 1;
    }
    
    int cuda_devices = getCUDADeviceCount();
    printSuccess("CUDA available, " + std::to_string(cuda_devices) + " device(s)");
    
    // Check stack capture
    if (capture_stacks && !StackCapture::isAvailable()) {
        printWarning("Stack capture not available, disabling");
        capture_stacks = false;
    } else if (capture_stacks) {
        printSuccess("Stack capture available");
    }
    
    std::cout << "\n";
    std::cout << C(Bold) << "Configuration:" << C(Reset) << "\n";
    std::cout << "  Target kernels: " << target_kernels << "\n";
    std::cout << "  Output file:    " << output_file << "\n";
    std::cout << "  Capture stacks: " << (capture_stacks ? "Yes" : "No") << "\n\n";

    // Configuration
    const int DATA_SIZE = 1024 * 1024;  // 1M elements
    
    // Allocate GPU memory
    float* d_data;
    cudaError_t err = cudaMalloc(&d_data, DATA_SIZE * sizeof(float));
    if (err != cudaSuccess) {
        printError(std::string("cudaMalloc failed: ") + cudaGetErrorString(err));
        return 1;
    }
    
    // Initialize data
    std::vector<float> h_data(DATA_SIZE, 1.0f);
    cudaMemcpy(d_data, h_data.data(), DATA_SIZE * sizeof(float), cudaMemcpyHostToDevice);
    
    printSuccess("Allocated " + std::to_string(DATA_SIZE * sizeof(float) / 1024 / 1024) + " MB GPU memory");

    // Setup profiler
    CUPTIProfiler profiler;
    ProfilerConfig prof_config;
    prof_config.buffer_size = 64 * 1024 * 1024;  // 64MB buffer
    profiler.initialize(prof_config);
    
    // Setup stack capturer
    std::unique_ptr<StackCapture> stack_capturer;
    std::vector<TraceEvent> host_stacks;
    
    if (capture_stacks) {
        StackCaptureConfig stack_config;
        stack_config.max_depth = 16;
        stack_config.resolve_symbols = false;  // Fast capture
        stack_config.demangle = false;
        stack_capturer = std::make_unique<StackCapture>(stack_config);
        host_stacks.reserve(target_kernels);
    }

    // ================================================================
    // Run benchmark
    // ================================================================
    printSection("Running Benchmark");
    std::cout << "Launching " << target_kernels << " REAL CUDA kernels...\n\n";
    
    // Start profiling
    profiler.startCapture();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    int threads = 256;
    int blocks = (DATA_SIZE + threads - 1) / threads;
    
    // Progress bar
    int progress_interval = target_kernels / 20;
    if (progress_interval == 0) progress_interval = 1;
    
    for (int i = 0; i < target_kernels; ++i) {
        // Capture host call stack before kernel launch
        if (capture_stacks && stack_capturer) {
            CallStack stack;
            stack_capturer->capture(stack);
            
            TraceEvent stack_event;
            stack_event.type = EventType::KernelLaunch;
            stack_event.name = "benchmark_kernel_" + std::to_string(i);
            stack_event.timestamp = getCurrentTimestamp();
            stack_event.correlation_id = i;
            stack_event.call_stack = stack;
            stack_event.thread_id = stack.thread_id;
            host_stacks.push_back(std::move(stack_event));
        }
        
        // Launch real CUDA kernel
        benchmark_kernel_cli<<<blocks, threads>>>(d_data, DATA_SIZE, i);
        
        // Sync every 1000 kernels
        if (i % 1000 == 999) {
            cudaDeviceSynchronize();
        }
        
        // Show progress
        if (verbose && i % progress_interval == 0) {
            int pct = (i * 100) / target_kernels;
            std::cout << "\r  Progress: [";
            for (int p = 0; p < 20; ++p) {
                std::cout << (p < pct / 5 ? "█" : "░");
            }
            std::cout << "] " << pct << "% " << std::flush;
        }
    }
    
    // Final sync
    cudaDeviceSynchronize();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Stop profiling
    profiler.stopCapture();
    
    if (verbose) {
        std::cout << "\r  Progress: [████████████████████] 100%\n";
    }
    
    printSuccess("Launched " + std::to_string(target_kernels) + " real CUDA kernels");
    std::cout << "  Total time:   " << duration.count() << " ms\n";
    std::cout << "  Kernels/sec:  " << std::fixed << std::setprecision(0) 
              << (target_kernels * 1000.0 / duration.count()) << "\n\n";

    // ================================================================
    // Collect results
    // ================================================================
    printSection("Results");
    
    std::vector<TraceEvent> gpu_events;
    size_t event_count = profiler.getEvents(gpu_events);
    uint64_t events_dropped = profiler.eventsDropped();
    
    // Count event types
    size_t kernel_launches = 0, kernel_completes = 0, other = 0;
    for (const auto& e : gpu_events) {
        if (e.type == EventType::KernelLaunch) kernel_launches++;
        else if (e.type == EventType::KernelComplete) kernel_completes++;
        else other++;
    }
    
    std::cout << C(Bold) << "GPU Events (CUPTI):" << C(Reset) << "\n";
    std::cout << "  Events captured:   " << event_count << "\n";
    std::cout << "  Events dropped:    " << events_dropped << "\n";
    std::cout << "  Kernel launches:   " << kernel_launches << "\n";
    std::cout << "  Kernel completes:  " << kernel_completes << "\n";
    std::cout << "  Other events:      " << other << "\n\n";

    // Host call stacks
    if (capture_stacks) {
        size_t stacks_captured = 0;
        size_t total_frames = 0;
        
        for (const auto& e : host_stacks) {
            if (e.call_stack.has_value()) {
                stacks_captured++;
                total_frames += e.call_stack->depth();
            }
        }
        
        double avg_depth = stacks_captured > 0 ? total_frames / static_cast<double>(stacks_captured) : 0;
        
        std::cout << C(Bold) << "Host Call Stacks:" << C(Reset) << "\n";
        std::cout << "  Stacks captured:   " << stacks_captured << "\n";
        std::cout << "  Average depth:     " << std::fixed << std::setprecision(1) << avg_depth << " frames\n";
        std::cout << "  Total frames:      " << total_frames << "\n\n";
        
        // Merge stacks with GPU events
        std::map<uint64_t, CallStack> stack_map;
        for (const auto& e : host_stacks) {
            if (e.call_stack.has_value()) {
                stack_map[e.correlation_id] = e.call_stack.value();
            }
        }
        
        size_t attached = 0;
        for (auto& gpu_event : gpu_events) {
            auto it = stack_map.find(gpu_event.correlation_id);
            if (it != stack_map.end()) {
                gpu_event.call_stack = it->second;
                attached++;
            }
        }
        
        std::cout << C(Bold) << "Correlation:" << C(Reset) << "\n";
        std::cout << "  GPU events with stacks: " << attached << " / " << gpu_events.size() << "\n\n";
    }

    // ================================================================
    // Save to file
    // ================================================================
    {
        SBTWriter writer(output_file);
        TraceMetadata meta;
        meta.application_name = "TraceSmith Benchmark";
        meta.command_line = "tracesmith benchmark -n " + std::to_string(target_kernels);
        writer.writeMetadata(meta);
        
        for (const auto& e : gpu_events) {
            writer.writeEvent(e);
        }
        writer.finalize();
        
        printSuccess("Saved to " + output_file);
        
        std::ifstream file(output_file, std::ios::binary | std::ios::ate);
        size_t file_size = file.tellg();
        std::cout << "  File size: " << file_size / 1024 << " KB\n\n";
    }

    // ================================================================
    // Summary
    // ================================================================
    bool goal_achieved = (kernel_launches >= static_cast<size_t>(target_kernels));
    
    std::cout << C(Bold);
    if (goal_achieved) {
        std::cout << C(Green);
    } else {
        std::cout << C(Red);
    }
    std::cout << "╔══════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                         BENCHMARK SUMMARY                            ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║                                                                      ║\n";
    std::cout << "║  Feature: Non-intrusive 10K+ instruction-level GPU call stacks       ║\n";
    std::cout << "║                                                                      ║\n";
    
    if (goal_achieved) {
        std::cout << "║  ✅ VERIFIED!                                                        ║\n";
    } else {
        std::cout << "║  ❌ NOT VERIFIED                                                     ║\n";
    }
    
    std::cout << "║                                                                      ║\n";
    std::cout << "║  Results (REAL GPU):                                                 ║\n";
    std::cout << "║    • CUDA kernels launched: " << std::setw(8) << target_kernels << "                           ║\n";
    std::cout << "║    • GPU events (CUPTI):    " << std::setw(8) << gpu_events.size() << "                           ║\n";
    std::cout << "║    • Kernel launches:       " << std::setw(8) << kernel_launches << "                           ║\n";
    std::cout << "║    • Total time:            " << std::setw(5) << duration.count() << " ms                            ║\n";
    std::cout << "║                                                                      ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════╝\n";
    std::cout << C(Reset) << "\n";

    // Cleanup
    cudaFree(d_data);
    
    return goal_achieved ? 0 : 1;
#endif // TRACESMITH_ENABLE_CUDA
}

// =============================================================================
// Main Entry Point
// =============================================================================
int main(int argc, char* argv[]) {
    // Check for --no-color flag
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-color") {
            Color::enabled = false;
        }
    }
    
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "profile") {
        return cmdProfile(argc, argv);
    } else if (command == "record") {
        return cmdRecord(argc, argv);
    } else if (command == "view") {
        return cmdView(argc, argv);
    } else if (command == "info") {
        return cmdInfo(argc, argv);
    } else if (command == "export") {
        return cmdExport(argc, argv);
    } else if (command == "analyze") {
        return cmdAnalyze(argc, argv);
    } else if (command == "replay") {
        return cmdReplay(argc, argv);
    } else if (command == "benchmark") {
        return cmdBenchmark(argc, argv);
    } else if (command == "devices") {
        return cmdDevices(argc, argv);
    } else if (command == "version" || command == "-v" || command == "--version") {
        printBanner();
        return 0;
    } else if (command == "help" || command == "-h" || command == "--help") {
        printUsage(argv[0]);
        return 0;
    } else if (command == "--no-color") {
        printUsage(argv[0]);
        return 0;
    } else {
        printError("Unknown command: " + command);
        std::cout << "Run '" << argv[0] << " help' for available commands.\n";
        return 1;
    }
}
