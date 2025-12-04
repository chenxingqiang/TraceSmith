#include "tracesmith/replay/replay_engine.hpp"
#include <iostream>
#include <algorithm>
#include <map>

namespace tracesmith {

ReplayEngine::ReplayEngine()
    : scheduler_(std::make_unique<StreamScheduler>())
    , executor_(std::make_unique<OperationExecutor>())
    , checker_(std::make_unique<DeterminismChecker>()) {
}

ReplayEngine::~ReplayEngine() = default;

bool ReplayEngine::loadTrace(const std::string& filename) {
    SBTReader reader(filename);
    if (!reader.isOpen() || !reader.isValid()) {
        return false;
    }
    
    TraceRecord record;
    auto result = reader.readAll(record);
    if (!result) {
        return false;
    }
    
    metadata_ = record.metadata();
    events_ = record.events();
    
    return true;
}

void ReplayEngine::loadEvents(const std::vector<TraceEvent>& events) {
    events_ = events;
}

ReplayResult ReplayEngine::replay(const ReplayConfig& config) {
    ReplayResult result;
    Timestamp start_time = getCurrentTimestamp();
    
    // Reset components
    scheduler_->reset();
    executor_->resetMetrics();
    checker_->reset();
    operations_.clear();
    
    // Set executor mode
    executor_->setDryRun(config.mode == ReplayMode::DryRun);
    
    // Prepare operations
    prepareOperations(config);
    result.operations_total = operations_.size();
    
    if (operations_.empty()) {
        result.errors.push_back("No operations to replay");
        return result;
    }
    
    // Build dependencies
    buildDependencies();
    
    // Add operations to scheduler
    scheduler_->addOperations(operations_);
    
    // Execute replay
    result = executeReplay(config);
    
    // Record timing
    Timestamp end_time = getCurrentTimestamp();
    result.replay_duration = end_time - start_time;
    
    // Validate if requested
    if (config.validate_order || config.validate_dependencies) {
        if (config.validate_order) {
            result.deterministic = checker_->validateOrder();
            result.order_violations = checker_->getViolations().order_violations.size();
        }
        
        if (config.validate_dependencies) {
            bool deps_valid = checker_->validateDependencies();
            result.deterministic = result.deterministic && deps_valid;
            result.dependency_violations = checker_->getViolations().dependency_violations.size();
        }
    }
    
    result.success = (result.operations_failed == 0);
    
    return result;
}

void ReplayEngine::prepareOperations(const ReplayConfig& config) {
    size_t op_id = 0;
    
    for (const auto& event : events_) {
        StreamOperation op(event, op_id++);
        
        // Filter based on config
        if (!shouldIncludeOperation(op, config)) {
            continue;
        }
        
        operations_.push_back(op);
        checker_->recordOriginal(op);
    }
}

void ReplayEngine::buildDependencies() {
    // Build simple stream-based dependencies
    // Operations on the same stream depend on previous operations
    std::map<uint32_t, size_t> last_op_per_stream;
    
    for (auto& op : operations_) {
        uint32_t stream_id = op.stream_id;
        
        // Depend on previous operation in same stream
        if (last_op_per_stream.find(stream_id) != last_op_per_stream.end()) {
            op.depends_on.push_back(last_op_per_stream[stream_id]);
        }
        
        // Sync operations create dependencies across streams
        if (op.event.type == EventType::StreamSync || 
            op.event.type == EventType::DeviceSync) {
            // Depend on all previous operations in all streams
            for (const auto& [other_stream, last_id] : last_op_per_stream) {
                if (other_stream != stream_id) {
                    op.depends_on.push_back(last_id);
                }
            }
        }
        
        last_op_per_stream[stream_id] = op.operation_id;
    }
}

ReplayResult ReplayEngine::executeReplay(const ReplayConfig& config) {
    ReplayResult result;
    
    while (!scheduler_->allCompleted()) {
        StreamOperation* op = scheduler_->getNextOperation();
        
        if (!op) {
            // No operations ready - check if we're stuck
            size_t pending = scheduler_->pendingCount();
            size_t ready = scheduler_->readyCount();
            
            if (pending > 0 && ready == 0) {
                result.errors.push_back("Deadlock detected: " + 
                    std::to_string(pending) + " operations pending but none ready");
                break;
            }
            
            // No more operations to process
            if (pending == 0 && ready == 0) {
                break;
            }
            
            continue;
        }
        
        // Execute operation
        bool success = executor_->execute(*op);
        
        if (success) {
            result.operations_executed++;
            scheduler_->markCompleted(op->operation_id);
            checker_->recordReplayed(*op);
            
            if (config.verbose) {
                std::cout << "Executed: " << op->event.name << " (stream " << op->stream_id << ")\n";
            }
        } else {
            result.operations_failed++;
            result.errors.push_back("Failed to execute operation " + 
                std::to_string(op->operation_id) + ": " + op->event.name);
            
            if (config.pause_on_error) {
                break;
            }
        }
    }
    
    // Get executor metrics
    auto metrics = executor_->getMetrics();
    result.original_duration = metrics.total_execution_time;
    
    return result;
}

bool ReplayEngine::shouldIncludeOperation(const StreamOperation& op, const ReplayConfig& config) {
    // Filter by mode
    if (config.mode == ReplayMode::StreamSpecific && config.stream_id) {
        if (op.stream_id != *config.stream_id) {
            return false;
        }
    }
    
    // Filter by time range
    if (config.start_time && op.event.timestamp < *config.start_time) {
        return false;
    }
    if (config.end_time && op.event.timestamp > *config.end_time) {
        return false;
    }
    
    // Filter by operation ID range
    if (config.start_operation_id && op.operation_id < *config.start_operation_id) {
        return false;
    }
    if (config.end_operation_id && op.operation_id > *config.end_operation_id) {
        return false;
    }
    
    return true;
}

} // namespace tracesmith
