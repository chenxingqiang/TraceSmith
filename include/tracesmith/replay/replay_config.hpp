#pragma once

#include "tracesmith/common/types.hpp"
#include <string>
#include <vector>
#include <optional>

namespace tracesmith {

/// Replay mode
enum class ReplayMode {
    Full,           // Replay entire trace
    Partial,        // Replay specific time/operation range
    DryRun,         // Simulate without execution
    StreamSpecific  // Replay specific stream(s)
};

/// Scheduling policy for stream execution
enum class SchedulingPolicy {
    RoundRobin,     // Fair scheduling across streams
    Priority,       // Priority-based scheduling
    OriginalTiming  // Match original captured timing
};

/// Replay configuration
struct ReplayConfig {
    ReplayMode mode = ReplayMode::Full;
    SchedulingPolicy scheduling = SchedulingPolicy::RoundRobin;
    
    // Partial replay options
    std::optional<Timestamp> start_time;
    std::optional<Timestamp> end_time;
    std::optional<size_t> start_operation_id;
    std::optional<size_t> end_operation_id;
    std::optional<uint32_t> stream_id;  // For StreamSpecific mode
    
    // Validation options
    bool validate_order = true;          // Check operation order
    bool validate_dependencies = true;    // Check dependency satisfaction
    bool validate_timing = false;         // Check timing constraints
    bool compute_checksums = false;       // Compute memory checksums
    
    // Execution options
    bool verbose = false;                 // Verbose output
    bool pause_on_error = false;          // Pause when validation fails
    double time_scale = 1.0;              // Time scaling factor (1.0 = realtime)
    
    ReplayConfig() = default;
};

/// Replay result
struct ReplayResult {
    bool success = false;
    bool deterministic = true;
    
    size_t operations_total = 0;
    size_t operations_executed = 0;
    size_t operations_skipped = 0;
    size_t operations_failed = 0;
    
    Timestamp replay_duration = 0;
    Timestamp original_duration = 0;
    
    // Validation results
    size_t order_violations = 0;
    size_t dependency_violations = 0;
    size_t timing_violations = 0;
    
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    std::string summary() const {
        std::string s;
        s += "Replay Result:\n";
        s += "  Success: " + std::string(success ? "Yes" : "No") + "\n";
        s += "  Deterministic: " + std::string(deterministic ? "Yes" : "No") + "\n";
        s += "  Operations: " + std::to_string(operations_executed) + "/" + 
             std::to_string(operations_total) + "\n";
        s += "  Failed: " + std::to_string(operations_failed) + "\n";
        if (order_violations > 0) {
            s += "  Order violations: " + std::to_string(order_violations) + "\n";
        }
        if (dependency_violations > 0) {
            s += "  Dependency violations: " + std::to_string(dependency_violations) + "\n";
        }
        return s;
    }
};

/// Stream operation for scheduling
struct StreamOperation {
    TraceEvent event;
    size_t operation_id;
    uint32_t device_id;
    uint32_t stream_id;
    
    // Dependencies
    std::vector<size_t> depends_on;  // Operation IDs this depends on
    bool dependencies_satisfied = false;
    
    // Execution state
    bool executed = false;
    bool skipped = false;
    Timestamp execution_time = 0;
    
    StreamOperation() : operation_id(0), device_id(0), stream_id(0) {}
    explicit StreamOperation(const TraceEvent& ev, size_t id) 
        : event(ev), operation_id(id), 
          device_id(ev.device_id), stream_id(ev.stream_id) {}
};

} // namespace tracesmith
