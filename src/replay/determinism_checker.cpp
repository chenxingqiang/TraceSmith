#include "tracesmith/replay/determinism_checker.hpp"
#include <sstream>

namespace tracesmith {

void DeterminismChecker::recordOriginal(const StreamOperation& op) {
    original_ops_.push_back(op);
}

void DeterminismChecker::recordReplayed(const StreamOperation& op) {
    replayed_ops_.push_back(op);
}

bool DeterminismChecker::validateOrder() {
    if (original_ops_.size() != replayed_ops_.size()) {
        violations_.order_violations.push_back(
            "Operation count mismatch: original=" + std::to_string(original_ops_.size()) +
            ", replayed=" + std::to_string(replayed_ops_.size())
        );
        return false;
    }
    
    bool valid = true;
    for (size_t i = 0; i < original_ops_.size(); ++i) {
        if (!checkOperationMatch(original_ops_[i], replayed_ops_[i])) {
            violations_.order_violations.push_back(
                "Operation mismatch at index " + std::to_string(i) +
                ": expected " + original_ops_[i].event.name +
                ", got " + replayed_ops_[i].event.name
            );
            valid = false;
        }
    }
    
    return valid;
}

bool DeterminismChecker::validateDependencies() {
    bool valid = true;
    
    for (const auto& op : replayed_ops_) {
        // Check if all dependencies were executed before this operation
        for (size_t dep_id : op.depends_on) {
            bool dep_found = false;
            for (const auto& completed_op : replayed_ops_) {
                if (completed_op.operation_id == dep_id && 
                    completed_op.executed &&
                    completed_op.execution_time < op.execution_time) {
                    dep_found = true;
                    break;
                }
            }
            
            if (!dep_found) {
                violations_.dependency_violations.push_back(
                    "Dependency violation: operation " + std::to_string(op.operation_id) +
                    " executed before dependency " + std::to_string(dep_id)
                );
                valid = false;
            }
        }
    }
    
    return valid;
}

std::string DeterminismChecker::getReport() const {
    std::ostringstream ss;
    
    ss << "Determinism Validation Report\n";
    ss << "==============================\n";
    ss << "Original operations: " << original_ops_.size() << "\n";
    ss << "Replayed operations: " << replayed_ops_.size() << "\n\n";
    
    if (violations_.order_violations.empty() &&
        violations_.dependency_violations.empty() &&
        violations_.timing_violations.empty()) {
        ss << "✓ All validations passed!\n";
    } else {
        if (!violations_.order_violations.empty()) {
            ss << "Order Violations (" << violations_.order_violations.size() << "):\n";
            for (const auto& v : violations_.order_violations) {
                ss << "  - " << v << "\n";
            }
            ss << "\n";
        }
        
        if (!violations_.dependency_violations.empty()) {
            ss << "Dependency Violations (" << violations_.dependency_violations.size() << "):\n";
            for (const auto& v : violations_.dependency_violations) {
                ss << "  - " << v << "\n";
            }
            ss << "\n";
        }
        
        if (!violations_.timing_violations.empty()) {
            ss << "Timing Violations (" << violations_.timing_violations.size() << "):\n";
            for (const auto& v : violations_.timing_violations) {
                ss << "  - " << v << "\n";
            }
        }
    }
    
    return ss.str();
}

void DeterminismChecker::reset() {
    original_ops_.clear();
    replayed_ops_.clear();
    violations_ = Violations{};
}

bool DeterminismChecker::checkOperationMatch(const StreamOperation& orig, const StreamOperation& replay) {
    // Check if operations are the same
    return orig.event.type == replay.event.type &&
           orig.event.device_id == replay.event.device_id &&
           orig.event.stream_id == replay.event.stream_id &&
           orig.event.name == replay.event.name;
}

} // namespace tracesmith
