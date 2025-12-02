#include <tracesmith/tracesmith.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <signal.h>
#include <cstring>

using namespace tracesmith;

// Global flag for interrupt handling
static volatile bool g_interrupted = false;

void signalHandler(int) {
    g_interrupted = true;
}

void printUsage(const char* program) {
    std::cout << "TraceSmith v" << getVersionString() << " - GPU Profiling & Replay System\n\n";
    std::cout << "Usage: " << program << " <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  record     Record GPU events to a trace file\n";
    std::cout << "  view       View contents of a trace file\n";
    std::cout << "  info       Show information about a trace file\n";
    std::cout << "  version    Show version information\n";
    std::cout << "  help       Show this help message\n";
    std::cout << "\nRun '" << program << " <command> --help' for command-specific options.\n";
}

void printRecordUsage(const char* program) {
    std::cout << "Usage: " << program << " record [options]\n\n";
    std::cout << "Record GPU events to a trace file.\n\n";
    std::cout << "Options:\n";
    std::cout << "  -o, --output <file>     Output trace file (default: trace.sbt)\n";
    std::cout << "  -d, --duration <sec>    Recording duration in seconds (default: 5)\n";
    std::cout << "  -b, --buffer <size>     Ring buffer size in events (default: 1M)\n";
    std::cout << "  -r, --rate <rate>       Event generation rate for simulation (default: 1000)\n";
    std::cout << "  -s, --simulate          Use simulation profiler (for testing)\n";
    std::cout << "  -h, --help              Show this help message\n";
}

void printViewUsage(const char* program) {
    std::cout << "Usage: " << program << " view [options] <file>\n\n";
    std::cout << "View contents of a trace file.\n\n";
    std::cout << "Options:\n";
    std::cout << "  -f, --format <fmt>      Output format: text, json (default: text)\n";
    std::cout << "  -n, --limit <count>     Maximum number of events to show\n";
    std::cout << "  -t, --type <type>       Filter by event type\n";
    std::cout << "  --stats                 Show statistics only\n";
    std::cout << "  -h, --help              Show this help message\n";
}

std::string formatTimestamp(Timestamp ts) {
    // Convert nanoseconds to readable format
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

std::string formatDuration(Timestamp dur) {
    if (dur < 1000) {
        return std::to_string(dur) + " ns";
    } else if (dur < 1000000) {
        return std::to_string(dur / 1000) + "." + std::to_string((dur % 1000) / 100) + " µs";
    } else if (dur < 1000000000) {
        return std::to_string(dur / 1000000) + "." + std::to_string((dur % 1000000) / 100000) + " ms";
    } else {
        return std::to_string(dur / 1000000000) + "." + std::to_string((dur % 1000000000) / 100000000) + " s";
    }
}

std::string formatBytes(uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return std::to_string(bytes / 1024) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    } else {
        return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
    }
}

int cmdRecord(int argc, char* argv[]) {
    std::string output_file = "trace.sbt";
    double duration_sec = 5.0;
    size_t buffer_size = 1024 * 1024;
    double event_rate = 1000.0;
    bool use_simulation = true;  // Default to simulation for now
    
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
        } else if ((arg == "-r" || arg == "--rate") && i + 1 < argc) {
            event_rate = std::stod(argv[++i]);
        } else if (arg == "-s" || arg == "--simulate") {
            use_simulation = true;
        }
    }
    
    std::cout << "TraceSmith Record\n";
    std::cout << "  Output:   " << output_file << "\n";
    std::cout << "  Duration: " << duration_sec << " seconds\n";
    std::cout << "  Buffer:   " << buffer_size << " events\n";
    std::cout << "  Platform: " << (use_simulation ? "Simulation" : "Auto-detect") << "\n\n";
    
    // Create profiler
    auto profiler = createProfiler(use_simulation ? PlatformType::Simulation : PlatformType::Unknown);
    
    if (!profiler) {
        std::cerr << "Error: Failed to create profiler\n";
        return 1;
    }
    
    // Configure
    ProfilerConfig config;
    config.buffer_size = buffer_size;
    
    if (!profiler->initialize(config)) {
        std::cerr << "Error: Failed to initialize profiler\n";
        return 1;
    }
    
    // Set event rate for simulation
    if (auto* sim = dynamic_cast<SimulationProfiler*>(profiler.get())) {
        sim->setEventRate(event_rate);
    }
    
    // Set up signal handler
    signal(SIGINT, signalHandler);
    
    // Create writer
    SBTWriter writer(output_file);
    if (!writer.isOpen()) {
        std::cerr << "Error: Failed to open output file: " << output_file << "\n";
        return 1;
    }
    
    // Write metadata
    TraceMetadata metadata;
    metadata.application_name = "tracesmith";
    metadata.command_line = "record";
    metadata.start_time = getCurrentTimestamp();
    
    // Get device info
    auto devices = profiler->getDeviceInfo();
    metadata.devices = devices;
    
    writer.writeMetadata(metadata);
    writer.writeDeviceInfo(devices);
    
    // Start capture
    std::cout << "Recording... (Press Ctrl+C to stop early)\n";
    
    profiler->startCapture();
    
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::milliseconds(static_cast<int64_t>(duration_sec * 1000));
    
    uint64_t total_events = 0;
    
    while (!g_interrupted && std::chrono::steady_clock::now() < end_time) {
        // Drain events from buffer
        std::vector<TraceEvent> events;
        size_t count = profiler->getEvents(events, 10000);
        
        if (count > 0) {
            writer.writeEvents(events);
            total_events += count;
            
            // Progress update
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            double elapsed_sec = std::chrono::duration<double>(elapsed).count();
            std::cout << "\r  Events: " << total_events 
                      << " | Rate: " << static_cast<int>(total_events / elapsed_sec) << "/s"
                      << " | Dropped: " << profiler->eventsDropped()
                      << "       " << std::flush;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Stop capture
    profiler->stopCapture();
    
    // Drain remaining events
    std::vector<TraceEvent> remaining;
    profiler->getEvents(remaining);
    if (!remaining.empty()) {
        writer.writeEvents(remaining);
        total_events += remaining.size();
    }
    
    // Finalize
    writer.finalize();
    
    std::cout << "\n\nRecording complete!\n";
    std::cout << "  Total events: " << total_events << "\n";
    std::cout << "  Dropped:      " << profiler->eventsDropped() << "\n";
    std::cout << "  File size:    " << formatBytes(writer.fileSize()) << "\n";
    std::cout << "  Output:       " << output_file << "\n";
    
    return 0;
}

int cmdView(int argc, char* argv[]) {
    std::string input_file;
    std::string format = "text";
    size_t limit = 0;
    bool stats_only = false;
    
    // Parse arguments
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
        } else if (arg[0] != '-') {
            input_file = arg;
        }
    }
    
    if (input_file.empty()) {
        std::cerr << "Error: No input file specified\n";
        printViewUsage(argv[0]);
        return 1;
    }
    
    // Open trace file
    SBTReader reader(input_file);
    
    if (!reader.isOpen()) {
        std::cerr << "Error: Failed to open file: " << input_file << "\n";
        return 1;
    }
    
    if (!reader.isValid()) {
        std::cerr << "Error: Invalid SBT file format\n";
        return 1;
    }
    
    // Read trace
    TraceRecord record;
    auto result = reader.readAll(record);
    
    if (!result) {
        std::cerr << "Error: Failed to read trace: " << result.error_message << "\n";
        return 1;
    }
    
    // Print header info
    std::cout << "TraceSmith Trace File: " << input_file << "\n";
    std::cout << "  Version:      " << reader.header().version_major << "." 
              << reader.header().version_minor << "\n";
    std::cout << "  Event count:  " << record.size() << "\n";
    
    if (!record.metadata().application_name.empty()) {
        std::cout << "  Application:  " << record.metadata().application_name << "\n";
    }
    
    std::cout << "\n";
    
    // Calculate statistics
    std::map<EventType, size_t> type_counts;
    std::map<uint32_t, size_t> stream_counts;
    Timestamp total_duration = 0;
    Timestamp min_ts = UINT64_MAX, max_ts = 0;
    
    for (const auto& event : record.events()) {
        type_counts[event.type]++;
        stream_counts[event.stream_id]++;
        total_duration += event.duration;
        if (event.timestamp < min_ts) min_ts = event.timestamp;
        if (event.timestamp > max_ts) max_ts = event.timestamp;
    }
    
    if (stats_only) {
        std::cout << "Statistics:\n";
        std::cout << "  Time range:      " << formatDuration(max_ts - min_ts) << "\n";
        std::cout << "  Total duration:  " << formatDuration(total_duration) << "\n\n";
        
        std::cout << "Events by type:\n";
        for (const auto& [type, count] : type_counts) {
            std::cout << "  " << std::setw(20) << std::left << eventTypeToString(type) 
                      << ": " << count << "\n";
        }
        
        std::cout << "\nEvents by stream:\n";
        for (const auto& [stream, count] : stream_counts) {
            std::cout << "  Stream " << stream << ": " << count << "\n";
        }
        
        return 0;
    }
    
    // Print events
    size_t count = 0;
    size_t max_count = limit > 0 ? limit : record.size();
    
    if (format == "json") {
        std::cout << "[\n";
    }
    
    for (const auto& event : record.events()) {
        if (count >= max_count) break;
        
        if (format == "json") {
            if (count > 0) std::cout << ",\n";
            std::cout << "  {";
            std::cout << "\"type\":\"" << eventTypeToString(event.type) << "\"";
            std::cout << ",\"timestamp\":" << event.timestamp;
            std::cout << ",\"duration\":" << event.duration;
            std::cout << ",\"stream\":" << event.stream_id;
            std::cout << ",\"device\":" << event.device_id;
            std::cout << ",\"name\":\"" << event.name << "\"";
            std::cout << "}";
        } else {
            std::cout << "[" << std::setw(8) << count << "] ";
            std::cout << std::setw(16) << std::left << eventTypeToString(event.type);
            std::cout << " | Stream " << event.stream_id;
            std::cout << " | " << std::setw(12) << formatDuration(event.duration);
            std::cout << " | " << event.name;
            
            if (event.kernel_params) {
                const auto& kp = event.kernel_params.value();
                std::cout << " <<<" << kp.grid_x << "," << kp.grid_y << "," << kp.grid_z
                          << ">>>, <<<" << kp.block_x << "," << kp.block_y << "," << kp.block_z << ">>>";
            }
            
            if (event.memory_params) {
                std::cout << " (" << formatBytes(event.memory_params->size_bytes) << ")";
            }
            
            std::cout << "\n";
        }
        
        count++;
    }
    
    if (format == "json") {
        std::cout << "\n]\n";
    }
    
    if (limit > 0 && record.size() > limit) {
        std::cout << "\n... and " << (record.size() - limit) << " more events\n";
    }
    
    return 0;
}

int cmdInfo(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " info <file>\n";
        return 1;
    }
    
    std::string input_file = argv[2];
    
    SBTReader reader(input_file);
    
    if (!reader.isOpen()) {
        std::cerr << "Error: Failed to open file: " << input_file << "\n";
        return 1;
    }
    
    const auto& header = reader.header();
    
    std::cout << "File: " << input_file << "\n\n";
    
    if (!header.isValid()) {
        std::cout << "Status: Invalid SBT file\n";
        return 1;
    }
    
    std::cout << "Format Information:\n";
    std::cout << "  Magic:          SBT\n";
    std::cout << "  Version:        " << header.version_major << "." << header.version_minor << "\n";
    std::cout << "  Header size:    " << header.header_size << " bytes\n";
    std::cout << "  Event count:    " << header.event_count << "\n";
    std::cout << "  Flags:          0x" << std::hex << header.flags << std::dec << "\n";
    
    std::cout << "\nSection Offsets:\n";
    std::cout << "  Metadata:       " << header.metadata_offset << "\n";
    std::cout << "  String table:   " << header.string_table_offset << "\n";
    std::cout << "  Device info:    " << header.device_info_offset << "\n";
    std::cout << "  Events:         " << header.events_offset << "\n";
    
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "record") {
        return cmdRecord(argc, argv);
    } else if (command == "view") {
        return cmdView(argc, argv);
    } else if (command == "info") {
        return cmdInfo(argc, argv);
    } else if (command == "version") {
        std::cout << "TraceSmith v" << getVersionString() << "\n";
        return 0;
    } else if (command == "help" || command == "-h" || command == "--help") {
        printUsage(argv[0]);
        return 0;
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage(argv[0]);
        return 1;
    }
}
