#include "tracesmith/replay/stream_scheduler.hpp"
#include <algorithm>

namespace tracesmith {

StreamScheduler::StreamScheduler(SchedulingPolicy policy) : policy_(policy) {}

void StreamScheduler::addOperation(const StreamOperation& op) {
    operations_[op.operation_id] = op;
    
    // Track dependencies
    for (size_t dep_id : op.depends_on) {
        dependents_[dep_id].push_back(op.operation_id);
    }
    
    // If no dependencies, add to ready queue
    if (op.depends_on.empty()) {
        addToReadyQueue(op.operation_id);
    }
}

void StreamScheduler::addOperations(const std::vector<StreamOperation>& ops) {
    for (const auto& op : ops) {
        addOperation(op);
    }
}

StreamOperation* StreamScheduler::getNextOperation() {
    if (ready_queue_.empty()) {
        return nullptr;
    }
    
    // Use priority selection (timestamp order)
    size_t next_id = selectNextPriority();
    
    // Check if operation exists (next_id can be 0 which is valid)
    auto it = operations_.find(next_id);
    if (it == operations_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

void StreamScheduler::markCompleted(size_t operation_id) {
    auto it = operations_.find(operation_id);
    if (it == operations_.end()) {
        return;
    }
    
    it->second.executed = true;
    it->second.execution_time = getCurrentTimestamp();
    
    // Remove from ready queue
    ready_queue_.erase(
        std::remove(ready_queue_.begin(), ready_queue_.end(), operation_id),
        ready_queue_.end()
    );
    
    // Update dependent operations
    updateDependencies(operation_id);
}

bool StreamScheduler::allCompleted() const {
    return std::all_of(operations_.begin(), operations_.end(),
        [](const auto& pair) { return pair.second.executed || pair.second.skipped; });
}

size_t StreamScheduler::pendingCount() const {
    return std::count_if(operations_.begin(), operations_.end(),
        [](const auto& pair) { return !pair.second.executed && !pair.second.skipped; });
}

size_t StreamScheduler::readyCount() const {
    return ready_queue_.size();
}

void StreamScheduler::reset() {
    operations_.clear();
    ready_queue_.clear();
    stream_queues_.clear();
    dependents_.clear();
    round_robin_index_ = 0;
}

StreamScheduler::Statistics StreamScheduler::getStatistics() const {
    Statistics stats;
    stats.total_operations = operations_.size();
    stats.ready_operations = ready_queue_.size();
    
    for (const auto& [id, op] : operations_) {
        if (op.executed) {
            stats.completed_operations++;
        } else if (!op.dependencies_satisfied) {
            stats.blocked_operations++;
        }
        
        stats.operations_per_stream[op.stream_id]++;
    }
    
    return stats;
}

void StreamScheduler::updateDependencies(size_t completed_id) {
    auto it = dependents_.find(completed_id);
    if (it == dependents_.end()) {
        return;
    }
    
    // Check all operations that depend on this one
    for (size_t dependent_id : it->second) {
        auto& dep_op = operations_[dependent_id];
        
        // Check if all dependencies are now satisfied
        bool all_satisfied = true;
        for (size_t dep_id : dep_op.depends_on) {
            if (!operations_[dep_id].executed) {
                all_satisfied = false;
                break;
            }
        }
        
        if (all_satisfied && !dep_op.dependencies_satisfied) {
            dep_op.dependencies_satisfied = true;
            addToReadyQueue(dependent_id);
        }
    }
}

void StreamScheduler::addToReadyQueue(size_t op_id) {
    auto& op = operations_[op_id];
    op.dependencies_satisfied = true;
    
    ready_queue_.push_back(op_id);
    
    // Add to stream-specific queue for round-robin
    stream_queues_[op.stream_id].push(op_id);
}

size_t StreamScheduler::selectNextRoundRobin() {
    // Fall back to priority selection if ready queue is available
    if (ready_queue_.empty()) {
        return 0;
    }
    
    // Use priority selection (timestamp order) for simplicity and correctness
    return selectNextPriority();
}

size_t StreamScheduler::selectNextPriority() {
    // For now, prioritize by timestamp (earliest first)
    if (ready_queue_.empty()) {
        return 0;
    }
    
    size_t earliest_id = ready_queue_[0];
    Timestamp earliest_time = operations_[earliest_id].event.timestamp;
    
    for (size_t op_id : ready_queue_) {
        Timestamp t = operations_[op_id].event.timestamp;
        if (t < earliest_time) {
            earliest_time = t;
            earliest_id = op_id;
        }
    }
    
    return earliest_id;
}

size_t StreamScheduler::selectNextOriginalTiming() {
    // Same as priority for now - execute in original timestamp order
    return selectNextPriority();
}

} // namespace tracesmith
