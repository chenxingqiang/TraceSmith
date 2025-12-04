#include "tracesmith/state/instruction_stream.hpp"
#include <algorithm>
#include <sstream>

namespace tracesmith {

void InstructionStreamBuilder::addEvent(const TraceEvent& event) {
    if (event.correlation_id == 0) {
        return;  // Invalid event
    }
    
    // Create node
    InstructionNode node(event);
    nodes_[event.correlation_id] = node;
    
    // Track per-stream
    streams_[event.stream_id].push_back(event.correlation_id);
}

void InstructionStreamBuilder::addEvents(const std::vector<TraceEvent>& events) {
    for (const auto& event : events) {
        addEvent(event);
    }
}

void InstructionStreamBuilder::analyze() {
    // Clear previous analysis
    dependencies_.clear();
    for (auto& pair : nodes_) {
        pair.second.dependencies.clear();
        pair.second.dependents.clear();
    }
    
    // Detect sequential dependencies within each stream
    detectSequentialDependencies();
    
    // Detect synchronization dependencies across streams
    detectSynchronizationDependencies();
    
    // Update node dependency lists
    for (const auto& dep : dependencies_) {
        auto from_it = nodes_.find(dep.from_correlation_id);
        auto to_it = nodes_.find(dep.to_correlation_id);
        
        if (from_it != nodes_.end() && to_it != nodes_.end()) {
            from_it->second.dependents.push_back(dep.to_correlation_id);
            to_it->second.dependencies.push_back(dep.from_correlation_id);
        }
    }
}

void InstructionStreamBuilder::detectSequentialDependencies() {
    // Operations in the same stream have sequential dependencies
    for (const auto& stream_pair : streams_) {
        const auto& ops = stream_pair.second;
        
        for (size_t i = 1; i < ops.size(); ++i) {
            uint64_t prev = ops[i - 1];
            uint64_t curr = ops[i];
            
            addDependency(prev, curr, DependencyType::Sequential, 
                          "Sequential in stream " + std::to_string(stream_pair.first));
        }
    }
}

void InstructionStreamBuilder::detectSynchronizationDependencies() {
    // Detect synchronization events and create cross-stream dependencies
    for (const auto& node_pair : nodes_) {
        const auto& event = node_pair.second.event;
        
        switch (event.type) {
            case EventType::StreamSync:
            case EventType::DeviceSync:
                // Create dependencies from all prior operations to this sync
                createSyncDependencies(event.correlation_id, event.stream_id);
                break;
                
            default:
                break;
        }
    }
}

void InstructionStreamBuilder::createSyncDependencies(uint64_t sync_corr_id, uint32_t sync_stream) {
    // Find all operations before this sync point
    for (const auto& node_pair : nodes_) {
        const auto& node = node_pair.second;
        
        // Skip same stream (handled by sequential deps) and future events
        if (node.event.stream_id == sync_stream) {
            continue;
        }
        
        if (node.event.timestamp < nodes_[sync_corr_id].event.timestamp) {
            addDependency(node.event.correlation_id, sync_corr_id, 
                          DependencyType::Synchronization,
                          "Stream sync barrier");
        }
    }
}

void InstructionStreamBuilder::addDependency(uint64_t from, uint64_t to, 
                                              DependencyType type, const std::string& desc) {
    // Check if dependency already exists
    for (const auto& dep : dependencies_) {
        if (dep.from_correlation_id == from && dep.to_correlation_id == to) {
            return;  // Already exists
        }
    }
    
    dependencies_.emplace_back(from, to, type, desc);
}

std::vector<InstructionNode> InstructionStreamBuilder::getExecutionOrder() const {
    // Get all nodes sorted by timestamp
    std::vector<InstructionNode> result;
    result.reserve(nodes_.size());
    
    for (const auto& pair : nodes_) {
        result.push_back(pair.second);
    }
    
    std::sort(result.begin(), result.end(), 
              [](const InstructionNode& a, const InstructionNode& b) {
                  return a.event.timestamp < b.event.timestamp;
              });
    
    return result;
}

std::vector<OperationDependency> InstructionStreamBuilder::getDependencies() const {
    return dependencies_;
}

std::vector<InstructionNode> InstructionStreamBuilder::getStreamOperations(uint32_t stream_id) const {
    std::vector<InstructionNode> result;
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return result;
    }
    
    for (uint64_t corr_id : it->second) {
        auto node_it = nodes_.find(corr_id);
        if (node_it != nodes_.end()) {
            result.push_back(node_it->second);
        }
    }
    
    return result;
}

bool InstructionStreamBuilder::hasDependency(uint64_t from, uint64_t to) const {
    for (const auto& dep : dependencies_) {
        if (dep.from_correlation_id == from && dep.to_correlation_id == to) {
            return true;
        }
    }
    return false;
}

InstructionStreamBuilder::Statistics InstructionStreamBuilder::getStatistics() const {
    Statistics stats;
    stats.total_operations = nodes_.size();
    stats.total_dependencies = dependencies_.size();
    
    for (const auto& pair : nodes_) {
        const auto& event = pair.second.event;
        
        switch (event.type) {
            case EventType::KernelLaunch:
                stats.kernel_launches++;
                break;
            case EventType::MemcpyH2D:
            case EventType::MemcpyD2H:
            case EventType::MemcpyD2D:
                stats.memory_operations++;
                break;
            case EventType::StreamSync:
            case EventType::DeviceSync:
                stats.synchronizations++;
                break;
            default:
                break;
        }
        
        stats.operations_per_stream[event.stream_id]++;
    }
    
    return stats;
}

std::string InstructionStreamBuilder::exportToDot() const {
    std::ostringstream dot;
    
    dot << "digraph InstructionStream {\n";
    dot << "  rankdir=LR;\n";
    dot << "  node [shape=box];\n\n";
    
    // Nodes
    for (const auto& pair : nodes_) {
        const auto& node = pair.second;
        dot << "  n" << node.event.correlation_id 
            << " [label=\"" << node.event.name 
            << "\\nStream " << node.event.stream_id << "\"];\n";
    }
    
    dot << "\n";
    
    // Edges (dependencies)
    for (const auto& dep : dependencies_) {
        std::string color;
        switch (dep.type) {
            case DependencyType::Sequential:
                color = "black";
                break;
            case DependencyType::Synchronization:
                color = "red";
                break;
            default:
                color = "gray";
                break;
        }
        
        dot << "  n" << dep.from_correlation_id 
            << " -> n" << dep.to_correlation_id
            << " [color=" << color << "];\n";
    }
    
    dot << "}\n";
    
    return dot.str();
}

void InstructionStreamBuilder::clear() {
    nodes_.clear();
    streams_.clear();
    dependencies_.clear();
    stream_last_op_.clear();
    pending_syncs_.clear();
}

} // namespace tracesmith
