#pragma once

#include "tracesmith/common/types.hpp"
#include <vector>
#include <map>
#include <set>
#include <memory>

namespace tracesmith {

/// GPU operation dependency type
enum class DependencyType {
    None,
    Sequential,      // Operations in same stream
    Synchronization, // Explicit sync (cudaStreamWaitEvent, etc.)
    HostBarrier,     // Host-side barrier
    Memory           // Memory dependency (WAR, WAW, RAW)
};

/// Represents a dependency between two operations
struct OperationDependency {
    uint64_t from_correlation_id;
    uint64_t to_correlation_id;
    DependencyType type;
    std::string description;
    
    OperationDependency(uint64_t from, uint64_t to, DependencyType t, const std::string& desc = "")
        : from_correlation_id(from)
        , to_correlation_id(to)
        , type(t)
        , description(desc) {}
};

/// Instruction stream node
struct InstructionNode {
    TraceEvent event;
    std::vector<uint64_t> dependencies;  // Correlation IDs this node depends on
    std::vector<uint64_t> dependents;    // Correlation IDs that depend on this node
    
    InstructionNode() = default;
    explicit InstructionNode(const TraceEvent& e) : event(e) {}
};

/**
 * Instruction Stream Builder
 * 
 * Builds an ordered sequence of GPU operations with dependency tracking.
 * Analyzes event streams to construct execution order and detect synchronization.
 */
class InstructionStreamBuilder {
public:
    InstructionStreamBuilder() = default;
    
    /**
     * Add an event to the instruction stream
     */
    void addEvent(const TraceEvent& event);
    
    /**
     * Add multiple events
     */
    void addEvents(const std::vector<TraceEvent>& events);
    
    /**
     * Analyze and build the dependency graph
     */
    void analyze();
    
    /**
     * Get the instruction stream in execution order
     */
    std::vector<InstructionNode> getExecutionOrder() const;
    
    /**
     * Get dependencies between operations
     */
    std::vector<OperationDependency> getDependencies() const;
    
    /**
     * Get all operations on a specific stream
     */
    std::vector<InstructionNode> getStreamOperations(uint32_t stream_id) const;
    
    /**
     * Check if an operation depends on another
     */
    bool hasDependency(uint64_t from, uint64_t to) const;
    
    /**
     * Get statistics about the instruction stream
     */
    struct Statistics {
        size_t total_operations = 0;
        size_t kernel_launches = 0;
        size_t memory_operations = 0;
        size_t synchronizations = 0;
        size_t total_dependencies = 0;
        std::map<uint32_t, size_t> operations_per_stream;
    };
    
    Statistics getStatistics() const;
    
    /**
     * Export to DOT format for visualization
     */
    std::string exportToDot() const;
    
    /**
     * Clear all data
     */
    void clear();

private:
    std::map<uint64_t, InstructionNode> nodes_;           // Correlation ID -> Node
    std::map<uint32_t, std::vector<uint64_t>> streams_;   // Stream ID -> Operations
    std::vector<OperationDependency> dependencies_;
    
    // Last operation on each stream (for sequential dependencies)
    std::map<uint32_t, uint64_t> stream_last_op_;
    
    // Pending synchronizations
    struct SyncPoint {
        uint64_t event_correlation_id;
        uint32_t stream_id;
    };
    std::vector<SyncPoint> pending_syncs_;
    
    void detectSequentialDependencies();
    void detectSynchronizationDependencies();
    void createSyncDependencies(uint64_t sync_corr_id, uint32_t sync_stream);
    void addDependency(uint64_t from, uint64_t to, DependencyType type, const std::string& desc = "");
};

} // namespace tracesmith
