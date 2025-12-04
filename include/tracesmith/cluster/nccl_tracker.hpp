#pragma once

#include "tracesmith/common/types.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace tracesmith::cluster {

/// NCCL operation types
enum class NCCLOpType {
    Unknown = 0,
    AllReduce,
    AllGather,
    ReduceScatter,
    Broadcast,
    Reduce,
    AllToAll,
    Send,
    Recv,
    GroupStart,
    GroupEnd
};

/// NCCL reduction operations
enum class NCCLRedOp {
    Sum = 0,
    Prod,
    Max,
    Min,
    Avg
};

/// NCCL data types
enum class NCCLDataType {
    Int8 = 0,
    Uint8,
    Int32,
    Uint32,
    Int64,
    Uint64,
    Float16,
    Float32,
    Float64,
    BFloat16
};

/// NCCL operation record
struct NCCLOperation {
    uint64_t op_id = 0;             // Unique operation ID
    NCCLOpType op_type = NCCLOpType::Unknown;
    NCCLRedOp red_op = NCCLRedOp::Sum;   // For reduction ops
    NCCLDataType data_type = NCCLDataType::Float32;
    
    uint64_t comm_id = 0;           // Communicator ID
    uint32_t rank = 0;              // Local rank
    uint32_t world_size = 0;        // Total ranks
    
    size_t count = 0;               // Element count
    size_t data_size = 0;           // Total bytes
    
    Timestamp start_time = 0;
    Timestamp end_time = 0;
    uint64_t duration_ns = 0;
    
    // For P2P operations
    int32_t peer_rank = -1;
    
    // Associated CUDA stream
    uint64_t cuda_stream = 0;
    
    // Correlation with GPU events
    uint64_t correlation_id = 0;
    
    // Status
    bool completed = false;
};

/// NCCL tracker configuration
struct NCCLTrackerConfig {
    bool hook_enabled = true;
    bool track_all_comms = true;
    std::vector<uint64_t> comm_filter;  // Empty = all
    bool capture_call_stack = false;
    size_t max_operations = 100000;
};

/// NCCL operation tracker
class NCCLTracker {
public:
    explicit NCCLTracker(const NCCLTrackerConfig& config = {});
    ~NCCLTracker();
    
    // Hook management
    bool installHooks();
    void removeHooks();
    bool isHooked() const { return hooked_; }
    
    // Capture control
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return capturing_.load(); }
    void clear();
    
    // Manual operation recording (when hooks are not available)
    uint64_t recordOperationStart(NCCLOpType type, size_t count, 
                                   NCCLDataType dtype, uint32_t rank,
                                   uint64_t stream = 0);
    void recordOperationEnd(uint64_t op_id);
    
    // Get captured operations
    std::vector<NCCLOperation> getOperations() const;
    std::vector<NCCLOperation> getOperationsByType(NCCLOpType type) const;
    std::vector<NCCLOperation> getOperationsByComm(uint64_t comm_id) const;
    NCCLOperation getOperation(uint64_t op_id) const;
    
    // Convert to TraceEvents
    std::vector<TraceEvent> toTraceEvents() const;
    
    // Correlation with GPU events
    void correlateWithGPUEvents(std::vector<TraceEvent>& gpu_events);
    
    // Statistics
    struct Statistics {
        uint64_t total_operations = 0;
        uint64_t total_bytes_transferred = 0;
        uint64_t total_duration_ns = 0;
        std::map<NCCLOpType, uint64_t> ops_by_type;
        std::map<NCCLOpType, uint64_t> bytes_by_type;
        std::map<NCCLOpType, uint64_t> duration_by_type;
    };
    Statistics getStatistics() const;
    
    // Callback for real-time notification
    using OperationCallback = std::function<void(const NCCLOperation&)>;
    void setOperationCallback(OperationCallback callback);
    
private:
    NCCLTrackerConfig config_;
    std::vector<NCCLOperation> operations_;
    std::map<uint64_t, NCCLOperation> pending_ops_;
    OperationCallback callback_;
    
    std::atomic<bool> capturing_{false};
    std::atomic<uint64_t> op_counter_{0};
    mutable std::mutex mutex_;
    bool hooked_ = false;
    
    // Singleton for hook callbacks
    static NCCLTracker* instance_;
};

/// Communication pattern analysis
class CommAnalysis {
public:
    CommAnalysis();
    
    // Build from NCCL operations
    void addOperations(const std::vector<NCCLOperation>& ops);
    void addOperation(const NCCLOperation& op);
    void clear();
    
    // Communication matrix (rank x rank)
    struct CommMatrix {
        std::vector<std::vector<uint64_t>> bytes;       // Bytes transferred
        std::vector<std::vector<uint64_t>> count;       // Operation count
        std::vector<std::vector<double>> avg_latency;   // Average latency (ns)
        uint32_t world_size = 0;
    };
    CommMatrix getCommMatrix() const;
    
    // Pattern detection
    enum class CommPattern {
        Unknown = 0,
        AllToAll,
        Ring,
        Tree,
        Butterfly,
        PointToPoint,
        Broadcast,
        Custom
    };
    CommPattern detectPattern() const;
    static const char* patternToString(CommPattern pattern);
    
    // Bottleneck analysis
    struct Bottleneck {
        uint32_t rank_a = 0;
        uint32_t rank_b = 0;
        double utilization = 0.0;
        std::string reason;
    };
    std::vector<Bottleneck> findBottlenecks() const;
    
    // Load imbalance
    struct LoadImbalance {
        uint32_t rank = 0;
        double deviation = 0.0;     // From average
        uint64_t total_bytes = 0;
        uint64_t total_time_ns = 0;
    };
    std::vector<LoadImbalance> analyzeLoadBalance() const;
    
    // Visualization
    std::string matrixToASCII() const;
    std::string matrixToHeatmapJSON() const;
    
    // Statistics
    uint64_t getTotalBytes() const;
    uint64_t getTotalOperations() const;
    uint32_t getWorldSize() const { return world_size_; }
    
private:
    std::vector<NCCLOperation> operations_;
    uint32_t world_size_ = 0;
    mutable std::mutex mutex_;
};

// Utility functions
const char* ncclOpTypeToString(NCCLOpType type);
const char* ncclRedOpToString(NCCLRedOp op);
const char* ncclDataTypeToString(NCCLDataType dtype);
size_t ncclDataTypeSize(NCCLDataType dtype);

} // namespace tracesmith::cluster

