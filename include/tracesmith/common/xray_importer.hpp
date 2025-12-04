#pragma once

/**
 * LLVM XRay Integration (v0.4.0)
 * 
 * XRay is LLVM's compiler-level instrumentation system that provides
 * low-overhead (<5%) function entry/exit tracing.
 * 
 * This module provides:
 * - XRay log file parsing
 * - Conversion to TraceSmith events
 * - Call graph reconstruction
 * 
 * XRay Format:
 * - Binary log files (.xray)
 * - FDR (Flight Data Recorder) format
 * - Basic mode entries
 * 
 * Usage:
 *   // Build target with -fxray-instrument
 *   // Run: XRAY_OPTIONS="patch_premain=true xray_mode=xray-basic" ./target
 *   // Import trace:
 *   XRayImporter importer;
 *   auto events = importer.importFile("xray-log.target.xray");
 */

#include "tracesmith/common/types.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace tracesmith {

/// XRay entry types
enum class XRayEntryType : uint8_t {
    FunctionEnter = 0,
    FunctionExit = 1,
    TailExit = 2,
    CustomEvent = 3,
    TypedEvent = 4
};

/// XRay function record
struct XRayFunctionRecord {
    uint32_t function_id;       // Function ID from instrumentation
    uint64_t timestamp;         // TSC timestamp
    XRayEntryType type;         // Entry type
    uint32_t thread_id;         // Thread ID
    uint8_t cpu_id;             // CPU core ID
    
    // Resolved information (after symbol lookup)
    std::string function_name;
    std::string file_name;
    uint32_t line_number;
    
    XRayFunctionRecord()
        : function_id(0)
        , timestamp(0)
        , type(XRayEntryType::FunctionEnter)
        , thread_id(0)
        , cpu_id(0)
        , line_number(0) {}
};

/// XRay custom event record
struct XRayCustomEvent {
    uint64_t timestamp;
    uint32_t thread_id;
    std::vector<uint8_t> data;
    
    XRayCustomEvent() : timestamp(0), thread_id(0) {}
};

/// XRay file header (basic mode)
struct XRayFileHeader {
    uint16_t version;
    uint16_t type;              // 0 = basic, 1 = FDR
    uint32_t cycle_frequency;   // TSC cycles per second
    uint64_t num_records;
    
    XRayFileHeader() : version(0), type(0), cycle_frequency(0), num_records(0) {}
};

/// XRay log importer
class XRayImporter {
public:
    /// Import configuration
    struct Config {
        bool resolve_symbols = true;        // Resolve function names
        bool include_custom_events = true;  // Include custom events
        bool filter_short_calls = false;    // Filter calls < min_duration_ns
        uint64_t min_duration_ns = 0;       // Minimum duration filter
        std::string symbol_file;            // Path to debug symbols
        
        Config() = default;
    };
    
    /// Import statistics
    struct Statistics {
        uint64_t records_read = 0;
        uint64_t records_converted = 0;
        uint64_t records_filtered = 0;
        uint64_t custom_events = 0;
        uint64_t functions_identified = 0;
        double total_duration_ms = 0;
        
        Statistics() = default;
    };
    
    XRayImporter() = default;
    explicit XRayImporter(const Config& config) : config_(config) {}
    
    /// Import XRay log file
    /// @param filename Path to .xray file
    /// @return Vector of TraceSmith events
    std::vector<TraceEvent> importFile(const std::string& filename);
    
    /// Import from memory buffer
    /// @param data Raw XRay data
    /// @param size Data size
    /// @return Vector of TraceSmith events
    std::vector<TraceEvent> importBuffer(const uint8_t* data, size_t size);
    
    /// Get raw XRay records (before conversion)
    const std::vector<XRayFunctionRecord>& getRawRecords() const { 
        return raw_records_; 
    }
    
    /// Get import statistics
    const Statistics& getStatistics() const { return stats_; }
    
    /// Get file header
    const XRayFileHeader& getHeader() const { return header_; }
    
    /// Set symbol file for name resolution
    void setSymbolFile(const std::string& path) { config_.symbol_file = path; }
    
    /// Set configuration
    void setConfig(const Config& config) { config_ = config; }
    
    /// Check if XRay support is available
    /// (Always true - XRay parsing doesn't require external dependencies)
    static bool isAvailable() { return true; }

private:
    Config config_;
    Statistics stats_;
    XRayFileHeader header_;
    std::vector<XRayFunctionRecord> raw_records_;
    
    // Function ID -> name mapping
    std::unordered_map<uint32_t, std::string> function_map_;
    
    // Parse header
    bool parseHeader(const uint8_t* data, size_t size);
    
    // Parse basic mode records
    bool parseBasicMode(const uint8_t* data, size_t size);
    
    // Parse FDR (Flight Data Recorder) mode records
    bool parseFDRMode(const uint8_t* data, size_t size);
    
    // Convert XRay records to TraceEvents
    std::vector<TraceEvent> convertToEvents();
    
    // Resolve function symbols
    void resolveSymbols();
    
    // Match function enter/exit pairs
    void matchPairs(std::vector<TraceEvent>& events);
    
    // Convert TSC to nanoseconds
    uint64_t tscToNanoseconds(uint64_t tsc) const;
};

/// XRay instrumentation helper
/// Use these macros in code compiled with -fxray-instrument
#define TRACESMITH_XRAY_ALWAYS_INSTRUMENT \
    __attribute__((xray_always_instrument))

#define TRACESMITH_XRAY_NEVER_INSTRUMENT \
    __attribute__((xray_never_instrument))

/// Custom XRay event emitter (if XRay runtime is available)
#ifdef __XRAY_RUNTIME__
#include <xray/xray_interface.h>

inline void xrayEmitCustomEvent(const char* data, size_t size) {
    __xray_customevent(data, size);
}

inline void xrayEmitTypedEvent(uint16_t type, const void* data, size_t size) {
    __xray_typedevent(type, data, size);
}
#else
inline void xrayEmitCustomEvent(const char*, size_t) {}
inline void xrayEmitTypedEvent(uint16_t, const void*, size_t) {}
#endif

} // namespace tracesmith

