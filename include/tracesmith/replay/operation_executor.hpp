#pragma once

#include "tracesmith/replay/replay_config.hpp"
#include "tracesmith/common/types.hpp"

namespace tracesmith {

/**
 * Operation Executor
 * 
 * Executes individual GPU operations during replay.
 * Supports dry-run mode for validation without actual execution.
 */
class OperationExecutor {
public:
    explicit OperationExecutor(bool dry_run = false);
    
    /**
     * Execute a single operation
     * Returns true if successful
     */
    bool execute(const StreamOperation& op);
    
    /**
     * Set dry-run mode
     */
    void setDryRun(bool enabled) { dry_run_ = enabled; }
    
    /**
     * Check if in dry-run mode
     */
    bool isDryRun() const { return dry_run_; }
    
    /**
     * Get execution metrics
     */
    struct Metrics {
        size_t operations_executed = 0;
        size_t kernels_executed = 0;
        size_t memory_ops_executed = 0;
        size_t sync_ops_executed = 0;
        Timestamp total_execution_time = 0;
    };
    
    Metrics getMetrics() const { return metrics_; }
    
    /**
     * Reset metrics
     */
    void resetMetrics();

private:
    bool dry_run_;
    Metrics metrics_;
    
    // Execution handlers
    bool executeKernel(const TraceEvent& event);
    bool executeMemoryOp(const TraceEvent& event);
    bool executeSyncOp(const TraceEvent& event);
};

} // namespace tracesmith
