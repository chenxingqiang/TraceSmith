#include "tracesmith/cluster/nccl_tracker.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace tracesmith::cluster {

// Static instance for hooks
NCCLTracker* NCCLTracker::instance_ = nullptr;

// =============================================================================
// NCCLTracker Implementation
// =============================================================================

NCCLTracker::NCCLTracker(const NCCLTrackerConfig& config)
    : config_(config) {
    operations_.reserve(config.max_operations);
}

NCCLTracker::~NCCLTracker() {
    removeHooks();
}

bool NCCLTracker::installHooks() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // In production, this would use LD_PRELOAD or similar
    // to intercept NCCL function calls
    // For now, we support manual operation recording
    
    instance_ = this;
    hooked_ = true;
    
    return true;
}

void NCCLTracker::removeHooks() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    hooked_ = false;
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void NCCLTracker::startCapture() {
    capturing_.store(true);
}

void NCCLTracker::stopCapture() {
    capturing_.store(false);
}

void NCCLTracker::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    operations_.clear();
    pending_ops_.clear();
    op_counter_.store(0);
}

uint64_t NCCLTracker::recordOperationStart(NCCLOpType type, size_t count,
                                            NCCLDataType dtype, uint32_t rank,
                                            uint64_t stream) {
    if (!capturing_.load()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t op_id = ++op_counter_;
    
    NCCLOperation op;
    op.op_id = op_id;
    op.op_type = type;
    op.data_type = dtype;
    op.count = count;
    op.data_size = count * ncclDataTypeSize(dtype);
    op.rank = rank;
    op.cuda_stream = stream;
    op.start_time = getCurrentTimestamp();
    op.completed = false;
    
    pending_ops_[op_id] = op;
    
    return op_id;
}

void NCCLTracker::recordOperationEnd(uint64_t op_id) {
    if (op_id == 0) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pending_ops_.find(op_id);
    if (it == pending_ops_.end()) {
        return;
    }
    
    NCCLOperation& op = it->second;
    op.end_time = getCurrentTimestamp();
    op.duration_ns = op.end_time - op.start_time;
    op.completed = true;
    
    if (operations_.size() < config_.max_operations) {
        operations_.push_back(op);
    }
    
    // Invoke callback
    if (callback_) {
        callback_(op);
    }
    
    pending_ops_.erase(it);
}

std::vector<NCCLOperation> NCCLTracker::getOperations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return operations_;
}

std::vector<NCCLOperation> NCCLTracker::getOperationsByType(NCCLOpType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<NCCLOperation> result;
    for (const auto& op : operations_) {
        if (op.op_type == type) {
            result.push_back(op);
        }
    }
    return result;
}

std::vector<NCCLOperation> NCCLTracker::getOperationsByComm(uint64_t comm_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<NCCLOperation> result;
    for (const auto& op : operations_) {
        if (op.comm_id == comm_id) {
            result.push_back(op);
        }
    }
    return result;
}

NCCLOperation NCCLTracker::getOperation(uint64_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& op : operations_) {
        if (op.op_id == op_id) {
            return op;
        }
    }
    return NCCLOperation{};
}

std::vector<TraceEvent> NCCLTracker::toTraceEvents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TraceEvent> events;
    events.reserve(operations_.size() * 2);  // Start + End
    
    for (const auto& op : operations_) {
        // Start event
        TraceEvent start_event;
        start_event.type = EventType::NCCLStart;
        start_event.timestamp = op.start_time;
        start_event.name = std::string("NCCL_") + ncclOpTypeToString(op.op_type);
        start_event.correlation_id = op.op_id;
        start_event.stream_id = static_cast<uint32_t>(op.cuda_stream);
        start_event.metadata["rank"] = std::to_string(op.rank);
        start_event.metadata["world_size"] = std::to_string(op.world_size);
        start_event.metadata["bytes"] = std::to_string(op.data_size);
        events.push_back(start_event);
        
        // End event
        if (op.completed) {
            TraceEvent end_event;
            end_event.type = EventType::NCCLComplete;
            end_event.timestamp = op.end_time;
            end_event.duration = op.duration_ns;
            end_event.name = std::string("NCCL_") + ncclOpTypeToString(op.op_type);
            end_event.correlation_id = op.op_id;
            end_event.stream_id = static_cast<uint32_t>(op.cuda_stream);
            events.push_back(end_event);
        }
    }
    
    return events;
}

void NCCLTracker::correlateWithGPUEvents(std::vector<TraceEvent>& gpu_events) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Match NCCL operations with GPU events by timestamp and stream
    for (auto& gpu_event : gpu_events) {
        for (const auto& nccl_op : operations_) {
            // Check if GPU event falls within NCCL operation time window
            if (gpu_event.timestamp >= nccl_op.start_time &&
                gpu_event.timestamp <= nccl_op.end_time &&
                gpu_event.stream_id == static_cast<uint32_t>(nccl_op.cuda_stream)) {
                
                // Add correlation
                gpu_event.metadata["nccl_op_id"] = std::to_string(nccl_op.op_id);
                gpu_event.metadata["nccl_op_type"] = ncclOpTypeToString(nccl_op.op_type);
            }
        }
    }
}

NCCLTracker::Statistics NCCLTracker::getStatistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Statistics stats;
    
    for (const auto& op : operations_) {
        stats.total_operations++;
        stats.total_bytes_transferred += op.data_size;
        stats.total_duration_ns += op.duration_ns;
        
        stats.ops_by_type[op.op_type]++;
        stats.bytes_by_type[op.op_type] += op.data_size;
        stats.duration_by_type[op.op_type] += op.duration_ns;
    }
    
    return stats;
}

void NCCLTracker::setOperationCallback(OperationCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

// =============================================================================
// CommAnalysis Implementation
// =============================================================================

CommAnalysis::CommAnalysis() = default;

void CommAnalysis::addOperations(const std::vector<NCCLOperation>& ops) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& op : ops) {
        operations_.push_back(op);
        if (op.world_size > world_size_) {
            world_size_ = op.world_size;
        }
    }
}

void CommAnalysis::addOperation(const NCCLOperation& op) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    operations_.push_back(op);
    if (op.world_size > world_size_) {
        world_size_ = op.world_size;
    }
}

void CommAnalysis::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    operations_.clear();
    world_size_ = 0;
}

CommAnalysis::CommMatrix CommAnalysis::getCommMatrix() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CommMatrix matrix;
    matrix.world_size = world_size_;
    
    if (world_size_ == 0) {
        return matrix;
    }
    
    // Initialize matrices
    matrix.bytes.resize(world_size_, std::vector<uint64_t>(world_size_, 0));
    matrix.count.resize(world_size_, std::vector<uint64_t>(world_size_, 0));
    matrix.avg_latency.resize(world_size_, std::vector<double>(world_size_, 0.0));
    
    // Accumulate data
    std::vector<std::vector<uint64_t>> latency_sum(world_size_, 
        std::vector<uint64_t>(world_size_, 0));
    
    for (const auto& op : operations_) {
        if (op.op_type == NCCLOpType::Send || op.op_type == NCCLOpType::Recv) {
            // P2P operations
            if (op.peer_rank >= 0 && static_cast<uint32_t>(op.peer_rank) < world_size_) {
                uint32_t src = (op.op_type == NCCLOpType::Send) ? op.rank : op.peer_rank;
                uint32_t dst = (op.op_type == NCCLOpType::Send) ? op.peer_rank : op.rank;
                
                matrix.bytes[src][dst] += op.data_size;
                matrix.count[src][dst]++;
                latency_sum[src][dst] += op.duration_ns;
            }
        } else if (op.op_type == NCCLOpType::AllReduce ||
                   op.op_type == NCCLOpType::AllGather ||
                   op.op_type == NCCLOpType::ReduceScatter ||
                   op.op_type == NCCLOpType::AllToAll) {
            // Collective operations - distribute across all pairs
            for (uint32_t i = 0; i < world_size_; i++) {
                for (uint32_t j = 0; j < world_size_; j++) {
                    if (i != j) {
                        matrix.bytes[i][j] += op.data_size / world_size_;
                        matrix.count[i][j]++;
                        latency_sum[i][j] += op.duration_ns;
                    }
                }
            }
        }
    }
    
    // Calculate average latency
    for (uint32_t i = 0; i < world_size_; i++) {
        for (uint32_t j = 0; j < world_size_; j++) {
            if (matrix.count[i][j] > 0) {
                matrix.avg_latency[i][j] = 
                    static_cast<double>(latency_sum[i][j]) / matrix.count[i][j];
            }
        }
    }
    
    return matrix;
}

CommAnalysis::CommPattern CommAnalysis::detectPattern() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (operations_.empty()) {
        return CommPattern::Unknown;
    }
    
    // Count operation types
    std::map<NCCLOpType, size_t> type_counts;
    for (const auto& op : operations_) {
        type_counts[op.op_type]++;
    }
    
    // Determine dominant pattern
    if (type_counts[NCCLOpType::AllToAll] > 0) {
        return CommPattern::AllToAll;
    }
    if (type_counts[NCCLOpType::Broadcast] > type_counts[NCCLOpType::AllReduce]) {
        return CommPattern::Broadcast;
    }
    if (type_counts[NCCLOpType::Send] + type_counts[NCCLOpType::Recv] > 
        operations_.size() / 2) {
        return CommPattern::PointToPoint;
    }
    if (type_counts[NCCLOpType::AllReduce] > 0 ||
        type_counts[NCCLOpType::AllGather] > 0 ||
        type_counts[NCCLOpType::ReduceScatter] > 0) {
        // These typically use ring algorithm
        return CommPattern::Ring;
    }
    
    return CommPattern::Custom;
}

const char* CommAnalysis::patternToString(CommPattern pattern) {
    switch (pattern) {
        case CommPattern::Unknown: return "Unknown";
        case CommPattern::AllToAll: return "AllToAll";
        case CommPattern::Ring: return "Ring";
        case CommPattern::Tree: return "Tree";
        case CommPattern::Butterfly: return "Butterfly";
        case CommPattern::PointToPoint: return "PointToPoint";
        case CommPattern::Broadcast: return "Broadcast";
        case CommPattern::Custom: return "Custom";
        default: return "Unknown";
    }
}

std::vector<CommAnalysis::Bottleneck> CommAnalysis::findBottlenecks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Bottleneck> bottlenecks;
    
    auto matrix = getCommMatrix();
    if (matrix.world_size == 0) {
        return bottlenecks;
    }
    
    // Find maximum bandwidth usage
    uint64_t max_bytes = 0;
    for (uint32_t i = 0; i < matrix.world_size; i++) {
        for (uint32_t j = 0; j < matrix.world_size; j++) {
            max_bytes = std::max(max_bytes, matrix.bytes[i][j]);
        }
    }
    
    if (max_bytes == 0) {
        return bottlenecks;
    }
    
    // Find links with high utilization
    for (uint32_t i = 0; i < matrix.world_size; i++) {
        for (uint32_t j = 0; j < matrix.world_size; j++) {
            double utilization = static_cast<double>(matrix.bytes[i][j]) / max_bytes;
            
            if (utilization > 0.9) {  // >90% of max
                Bottleneck b;
                b.rank_a = i;
                b.rank_b = j;
                b.utilization = utilization;
                b.reason = "High bandwidth utilization";
                bottlenecks.push_back(b);
            }
        }
    }
    
    return bottlenecks;
}

std::vector<CommAnalysis::LoadImbalance> CommAnalysis::analyzeLoadBalance() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<LoadImbalance> imbalances;
    
    if (world_size_ == 0) {
        return imbalances;
    }
    
    // Calculate per-rank statistics
    std::vector<uint64_t> rank_bytes(world_size_, 0);
    std::vector<uint64_t> rank_time(world_size_, 0);
    
    for (const auto& op : operations_) {
        if (op.rank < world_size_) {
            rank_bytes[op.rank] += op.data_size;
            rank_time[op.rank] += op.duration_ns;
        }
    }
    
    // Calculate averages
    uint64_t total_bytes = std::accumulate(rank_bytes.begin(), rank_bytes.end(), 0ULL);
    uint64_t avg_bytes = total_bytes / world_size_;
    
    // Find imbalances
    for (uint32_t i = 0; i < world_size_; i++) {
        double deviation = (avg_bytes > 0) ? 
            static_cast<double>(rank_bytes[i] - avg_bytes) / avg_bytes : 0.0;
        
        if (std::abs(deviation) > 0.1) {  // >10% deviation
            LoadImbalance li;
            li.rank = i;
            li.deviation = deviation;
            li.total_bytes = rank_bytes[i];
            li.total_time_ns = rank_time[i];
            imbalances.push_back(li);
        }
    }
    
    return imbalances;
}

std::string CommAnalysis::matrixToASCII() const {
    auto matrix = getCommMatrix();
    
    if (matrix.world_size == 0) {
        return "No communication data available";
    }
    
    std::ostringstream ss;
    ss << "Communication Matrix (bytes)\n";
    ss << std::string(matrix.world_size * 12 + 8, '-') << "\n";
    
    // Header
    ss << std::setw(6) << " ";
    for (uint32_t j = 0; j < matrix.world_size; j++) {
        ss << std::setw(10) << ("R" + std::to_string(j));
    }
    ss << "\n";
    
    // Data rows
    for (uint32_t i = 0; i < matrix.world_size; i++) {
        ss << std::setw(6) << ("R" + std::to_string(i));
        for (uint32_t j = 0; j < matrix.world_size; j++) {
            if (matrix.bytes[i][j] > 0) {
                ss << std::setw(10) << matrix.bytes[i][j];
            } else {
                ss << std::setw(10) << "-";
            }
        }
        ss << "\n";
    }
    
    return ss.str();
}

std::string CommAnalysis::matrixToHeatmapJSON() const {
    auto matrix = getCommMatrix();
    
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"world_size\": " << matrix.world_size << ",\n";
    ss << "  \"data\": [\n";
    
    for (uint32_t i = 0; i < matrix.world_size; i++) {
        ss << "    [";
        for (uint32_t j = 0; j < matrix.world_size; j++) {
            ss << matrix.bytes[i][j];
            if (j < matrix.world_size - 1) ss << ", ";
        }
        ss << "]";
        if (i < matrix.world_size - 1) ss << ",";
        ss << "\n";
    }
    
    ss << "  ]\n";
    ss << "}\n";
    
    return ss.str();
}

uint64_t CommAnalysis::getTotalBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t total = 0;
    for (const auto& op : operations_) {
        total += op.data_size;
    }
    return total;
}

uint64_t CommAnalysis::getTotalOperations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return operations_.size();
}

// =============================================================================
// Utility Functions
// =============================================================================

const char* ncclOpTypeToString(NCCLOpType type) {
    switch (type) {
        case NCCLOpType::Unknown: return "Unknown";
        case NCCLOpType::AllReduce: return "AllReduce";
        case NCCLOpType::AllGather: return "AllGather";
        case NCCLOpType::ReduceScatter: return "ReduceScatter";
        case NCCLOpType::Broadcast: return "Broadcast";
        case NCCLOpType::Reduce: return "Reduce";
        case NCCLOpType::AllToAll: return "AllToAll";
        case NCCLOpType::Send: return "Send";
        case NCCLOpType::Recv: return "Recv";
        case NCCLOpType::GroupStart: return "GroupStart";
        case NCCLOpType::GroupEnd: return "GroupEnd";
        default: return "Unknown";
    }
}

const char* ncclRedOpToString(NCCLRedOp op) {
    switch (op) {
        case NCCLRedOp::Sum: return "Sum";
        case NCCLRedOp::Prod: return "Prod";
        case NCCLRedOp::Max: return "Max";
        case NCCLRedOp::Min: return "Min";
        case NCCLRedOp::Avg: return "Avg";
        default: return "Unknown";
    }
}

const char* ncclDataTypeToString(NCCLDataType dtype) {
    switch (dtype) {
        case NCCLDataType::Int8: return "int8";
        case NCCLDataType::Uint8: return "uint8";
        case NCCLDataType::Int32: return "int32";
        case NCCLDataType::Uint32: return "uint32";
        case NCCLDataType::Int64: return "int64";
        case NCCLDataType::Uint64: return "uint64";
        case NCCLDataType::Float16: return "float16";
        case NCCLDataType::Float32: return "float32";
        case NCCLDataType::Float64: return "float64";
        case NCCLDataType::BFloat16: return "bfloat16";
        default: return "unknown";
    }
}

size_t ncclDataTypeSize(NCCLDataType dtype) {
    switch (dtype) {
        case NCCLDataType::Int8:
        case NCCLDataType::Uint8:
            return 1;
        case NCCLDataType::Float16:
        case NCCLDataType::BFloat16:
            return 2;
        case NCCLDataType::Int32:
        case NCCLDataType::Uint32:
        case NCCLDataType::Float32:
            return 4;
        case NCCLDataType::Int64:
        case NCCLDataType::Uint64:
        case NCCLDataType::Float64:
            return 8;
        default:
            return 0;
    }
}

} // namespace tracesmith::cluster

