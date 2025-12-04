#pragma once

#include "tracesmith/replay/replay_config.hpp"
#include "tracesmith/replay/stream_scheduler.hpp"
#include "tracesmith/replay/operation_executor.hpp"
#include "tracesmith/replay/determinism_checker.hpp"
#include "tracesmith/format/sbt_format.hpp"
#include <string>
#include <memory>

namespace tracesmith {

/**
 * Replay Engine
 * 
 * Main orchestrator for replaying captured GPU traces.
 * Coordinates scheduling, execution, and validation.
 */
class ReplayEngine {
public:
    ReplayEngine();
    ~ReplayEngine();
    
    /**
     * Load trace from SBT file
     */
    bool loadTrace(const std::string& filename);
    
    /**
     * Load trace from events
     */
    void loadEvents(const std::vector<TraceEvent>& events);
    
    /**
     * Execute replay with given configuration
     */
    ReplayResult replay(const ReplayConfig& config);
    
    /**
     * Get determinism checker for validation
     */
    const DeterminismChecker& getChecker() const { return *checker_; }

private:
    std::unique_ptr<StreamScheduler> scheduler_;
    std::unique_ptr<OperationExecutor> executor_;
    std::unique_ptr<DeterminismChecker> checker_;
    
    std::vector<TraceEvent> events_;
    std::vector<StreamOperation> operations_;
    TraceMetadata metadata_;
    
    // Prepare operations from events
    void prepareOperations(const ReplayConfig& config);
    
    // Build dependency graph
    void buildDependencies();
    
    // Execute replay loop
    ReplayResult executeReplay(const ReplayConfig& config);
    
    // Filter operations based on config
    bool shouldIncludeOperation(const StreamOperation& op, const ReplayConfig& config);
};

} // namespace tracesmith
