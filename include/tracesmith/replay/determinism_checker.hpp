#pragma once

#include "tracesmith/replay/replay_config.hpp"
#include "tracesmith/common/types.hpp"
#include <vector>
#include <string>

namespace tracesmith {

/**
 * Determinism Checker
 * 
 * Validates that replay execution matches the original captured trace.
 */
class DeterminismChecker {
public:
    DeterminismChecker() = default;
    
    /**
     * Record original operation for comparison
     */
    void recordOriginal(const StreamOperation& op);
    
    /**
     * Record replayed operation
     */
    void recordReplayed(const StreamOperation& op);
    
    /**
     * Validate operation order matches
     */
    bool validateOrder();
    
    /**
     * Validate dependencies were satisfied
     */
    bool validateDependencies();
    
    /**
     * Get validation report
     */
    std::string getReport() const;
    
    /**
     * Get violations
     */
    struct Violations {
        std::vector<std::string> order_violations;
        std::vector<std::string> dependency_violations;
        std::vector<std::string> timing_violations;
    };
    
    Violations getViolations() const { return violations_; }
    
    /**
     * Reset checker state
     */
    void reset();

private:
    std::vector<StreamOperation> original_ops_;
    std::vector<StreamOperation> replayed_ops_;
    Violations violations_;
    
    bool checkOperationMatch(const StreamOperation& orig, const StreamOperation& replay);
};

} // namespace tracesmith
