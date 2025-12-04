#pragma once

#include "tracesmith/replay/replay_config.hpp"
#include "tracesmith/common/types.hpp"
#include <map>
#include <queue>
#include <vector>
#include <memory>

namespace tracesmith {

/**
 * Stream Scheduler
 * 
 * Manages execution order of operations across multiple GPU streams,
 * respecting dependencies and synchronization points.
 */
class StreamScheduler {
public:
    explicit StreamScheduler(SchedulingPolicy policy = SchedulingPolicy::RoundRobin);
    
    /**
     * Add operation to schedule
     */
    void addOperation(const StreamOperation& op);
    
    /**
     * Add multiple operations
     */
    void addOperations(const std::vector<StreamOperation>& ops);
    
    /**
     * Get next operation ready for execution
     * Returns nullptr if no operations are ready
     */
    StreamOperation* getNextOperation();
    
    /**
     * Mark operation as completed
     * Updates dependencies for other operations
     */
    void markCompleted(size_t operation_id);
    
    /**
     * Check if all operations are complete
     */
    bool allCompleted() const;
    
    /**
     * Get number of pending operations
     */
    size_t pendingCount() const;
    
    /**
     * Get number of ready operations (dependencies satisfied)
     */
    size_t readyCount() const;
    
    /**
     * Reset scheduler state
     */
    void reset();
    
    /**
     * Get scheduling statistics
     */
    struct Statistics {
        size_t total_operations = 0;
        size_t completed_operations = 0;
        size_t ready_operations = 0;
        size_t blocked_operations = 0;
        std::map<uint32_t, size_t> operations_per_stream;
    };
    
    Statistics getStatistics() const;

private:
    SchedulingPolicy policy_;
    
    // All operations
    std::map<size_t, StreamOperation> operations_;
    
    // Operations ready for execution (dependencies satisfied)
    std::vector<size_t> ready_queue_;
    
    // Stream round-robin state
    std::map<uint32_t, std::queue<size_t>> stream_queues_;
    size_t round_robin_index_ = 0;
    
    // Dependency tracking
    std::map<size_t, std::vector<size_t>> dependents_;  // op_id -> [dependent_op_ids]
    
    // Helper methods
    void updateDependencies(size_t completed_id);
    void addToReadyQueue(size_t op_id);
    size_t selectNextRoundRobin();
    size_t selectNextPriority();
    size_t selectNextOriginalTiming();
};

} // namespace tracesmith
