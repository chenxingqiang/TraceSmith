# TraceSmith Cluster Profiling - Implementation Plan

## Overview

This document outlines the implementation plan for multi-GPU cluster profiling support in TraceSmith, using a **Hierarchical Architecture** that balances scalability, performance, and ease of deployment.

## Architecture

```
                         ┌─────────────────────────┐
                         │     Global Master       │
                         │  ┌─────────────────┐   │
                         │  │ ClusterMaster   │   │
                         │  │ - Time Sync     │   │
                         │  │ - Aggregation   │   │
                         │  │ - Analysis      │   │
                         │  └────────┬────────┘   │
                         └───────────┼────────────┘
                                     │ gRPC
              ┌──────────────────────┼──────────────────────┐
              │                      │                      │
     ┌────────┴────────┐    ┌────────┴────────┐    ┌────────┴────────┐
     │   Node Master   │    │   Node Master   │    │   Node Master   │
     │    (DGX-0)      │    │    (DGX-1)      │    │    (DGX-N)      │
     │ ┌─────────────┐ │    │ ┌─────────────┐ │    │ ┌─────────────┐ │
     │ │NodeAggregator│ │    │ │NodeAggregator│ │    │ │NodeAggregator│ │
     │ └──────┬──────┘ │    │ └──────┬──────┘ │    │ └──────┬──────┘ │
     └────────┼────────┘    └────────┼────────┘    └────────┼────────┘
              │                      │                      │
     ┌────┬───┴───┬────┐    ┌────┬───┴───┬────┐    ┌────┬───┴───┬────┐
     │GPU0│GPU1│GPU2│GPU3│    │GPU0│GPU1│GPU2│GPU3│    │GPU0│GPU1│GPU2│GPU3│
     └────┴────┴────┴────┘    └────┴────┴────┴────┘    └────┴────┴────┴────┘
```

## Implementation Phases

### Phase 1: Single-Node Multi-GPU Support (v0.7.0)

**Goal**: Support profiling multiple GPUs within a single node.

#### 1.1 Multi-GPU Profiler Manager

```cpp
// include/tracesmith/cluster/multi_gpu_profiler.hpp

#pragma once

#include "tracesmith/capture/profiler.hpp"
#include <map>
#include <thread>

namespace tracesmith::cluster {

/// Per-GPU profiler context
struct GPUContext {
    uint32_t gpu_id;
    uint32_t device_index;                          // CUDA device index
    std::unique_ptr<IPlatformProfiler> profiler;
    std::vector<TraceEvent> local_events;
    std::atomic<uint64_t> event_count{0};
};

/// Configuration for multi-GPU profiling
struct MultiGPUConfig {
    std::vector<uint32_t> gpu_ids;                  // GPUs to profile (empty = all)
    size_t per_gpu_buffer_size = 1024 * 1024;       // Events per GPU
    bool enable_nvlink_tracking = true;
    bool enable_peer_access_tracking = true;
    uint32_t aggregation_interval_ms = 100;
};

/// Multi-GPU profiler manager
class MultiGPUProfiler {
public:
    explicit MultiGPUProfiler(const MultiGPUConfig& config = {});
    ~MultiGPUProfiler();
    
    // Initialization
    bool initialize();
    void finalize();
    
    // GPU management
    bool addGPU(uint32_t gpu_id);
    bool removeGPU(uint32_t gpu_id);
    std::vector<uint32_t> getActiveGPUs() const;
    
    // Capture control
    bool startCapture();
    bool stopCapture();
    bool isCapturing() const;
    
    // Event retrieval
    size_t getEvents(std::vector<TraceEvent>& events, size_t max_count = 0);
    size_t getEventsFromGPU(uint32_t gpu_id, std::vector<TraceEvent>& events);
    
    // Statistics
    uint64_t totalEventsCaptured() const;
    uint64_t eventsPerGPU(uint32_t gpu_id) const;
    
    // Device info
    std::vector<DeviceInfo> getAllDeviceInfo() const;
    DeviceInfo getDeviceInfo(uint32_t gpu_id) const;
    
    // NVLink tracking
    struct NVLinkTransfer {
        uint32_t src_gpu;
        uint32_t dst_gpu;
        size_t bytes;
        Timestamp timestamp;
        uint64_t duration_ns;
    };
    std::vector<NVLinkTransfer> getNVLinkTransfers() const;
    
private:
    void aggregationLoop();
    void trackNVLinkEvents();
    
    MultiGPUConfig config_;
    std::map<uint32_t, std::unique_ptr<GPUContext>> gpu_contexts_;
    std::vector<TraceEvent> aggregated_events_;
    std::vector<NVLinkTransfer> nvlink_transfers_;
    
    std::thread aggregation_thread_;
    std::atomic<bool> running_{false};
    std::mutex events_mutex_;
    std::mutex nvlink_mutex_;
};

} // namespace tracesmith::cluster
```

#### 1.2 NVLink/NVSwitch Topology Discovery

```cpp
// include/tracesmith/cluster/gpu_topology.hpp

#pragma once

#include <vector>
#include <string>
#include <map>

namespace tracesmith::cluster {

/// GPU interconnect type
enum class GPULinkType {
    None,           // No direct connection
    PCIe,           // PCIe connection
    NVLink1,        // NVLink 1.0 (20 GB/s per link)
    NVLink2,        // NVLink 2.0 (25 GB/s per link)
    NVLink3,        // NVLink 3.0 (50 GB/s per link)
    NVLink4,        // NVLink 4.0 (100 GB/s per link)
    NVSwitch        // NVSwitch connection
};

/// Link between two GPUs
struct GPULink {
    uint32_t gpu_a;
    uint32_t gpu_b;
    GPULinkType type;
    uint32_t link_count;        // Number of links (e.g., 6 NVLinks)
    double bandwidth_gbps;      // Total bandwidth
    double measured_bandwidth;  // Actual measured bandwidth (if available)
};

/// GPU topology information
struct GPUTopologyInfo {
    uint32_t gpu_count;
    bool has_nvswitch;
    std::vector<GPULink> links;
    std::map<std::pair<uint32_t, uint32_t>, GPULinkType> link_matrix;
};

/// GPU topology discovery and query
class GPUTopology {
public:
    GPUTopology();
    
    // Discovery
    bool discover();
    
    // Query
    GPUTopologyInfo getTopology() const;
    GPULinkType getLinkType(uint32_t gpu_a, uint32_t gpu_b) const;
    double getBandwidth(uint32_t gpu_a, uint32_t gpu_b) const;
    bool canAccessPeer(uint32_t gpu_a, uint32_t gpu_b) const;
    
    // Optimal path finding
    std::vector<uint32_t> getOptimalPath(uint32_t src, uint32_t dst) const;
    
    // Visualization
    std::string toASCII() const;
    std::string toGraphviz() const;
    std::string toJSON() const;
    
private:
    GPUTopologyInfo topology_;
    bool discovered_ = false;
};

} // namespace tracesmith::cluster
```

#### 1.3 Files to Create

| File | Description |
|------|-------------|
| `include/tracesmith/cluster/multi_gpu_profiler.hpp` | Multi-GPU profiler interface |
| `include/tracesmith/cluster/gpu_topology.hpp` | GPU topology discovery |
| `src/cluster/multi_gpu_profiler.cpp` | Multi-GPU profiler implementation |
| `src/cluster/gpu_topology.cpp` | Topology discovery using NVML/CUDA |
| `src/cluster/CMakeLists.txt` | Build configuration |
| `examples/multi_gpu_example.cpp` | Multi-GPU profiling example |
| `tests/test_multi_gpu.cpp` | Unit tests |

#### 1.4 Dependencies

- **NVML** (NVIDIA Management Library) - GPU topology discovery
- **CUDA Driver API** - Peer access configuration
- **pthreads** - Multi-threaded aggregation

---

### Phase 2: Time Synchronization (v0.7.1)

**Goal**: Provide accurate time synchronization across GPUs and nodes.

#### 2.1 Time Sync Interface

```cpp
// include/tracesmith/cluster/time_sync.hpp

#pragma once

#include "tracesmith/common/types.hpp"
#include <map>
#include <string>

namespace tracesmith::cluster {

/// Time synchronization method
enum class TimeSyncMethod {
    SystemClock,    // Use system clock (basic)
    NTP,            // Network Time Protocol (~1ms)
    PTP,            // Precision Time Protocol (~1µs)
    CUDA,           // CUDA event timestamps (GPU-local)
    Custom          // Custom sync protocol
};

/// Time sync configuration
struct TimeSyncConfig {
    TimeSyncMethod method = TimeSyncMethod::SystemClock;
    std::string ntp_server = "pool.ntp.org";
    std::string ptp_interface = "eth0";
    uint32_t sync_interval_ms = 1000;
    int64_t max_acceptable_offset_ns = 1000000;  // 1ms default
};

/// Time synchronization result
struct SyncResult {
    bool success;
    int64_t offset_ns;          // Offset from reference
    int64_t round_trip_ns;      // RTT (for network methods)
    double uncertainty_ns;      // Estimated uncertainty
    Timestamp sync_time;        // When sync was performed
};

/// Time synchronization manager
class TimeSync {
public:
    explicit TimeSync(const TimeSyncConfig& config = {});
    ~TimeSync();
    
    // Initialization
    bool initialize();
    void finalize();
    
    // Synchronization
    SyncResult synchronize();
    SyncResult synchronizeWithNode(const std::string& node_id);
    
    // Timestamp conversion
    Timestamp toSynchronizedTime(Timestamp local_time) const;
    Timestamp toLocalTime(Timestamp sync_time) const;
    
    // Offset management
    int64_t getCurrentOffset() const;
    void setManualOffset(int64_t offset_ns);
    
    // GPU timestamp correlation
    void correlateGPUTimestamps(uint32_t gpu_id);
    int64_t getGPUOffset(uint32_t gpu_id) const;
    
    // Statistics
    double getAverageOffset() const;
    double getOffsetStdDev() const;
    
private:
    SyncResult syncNTP();
    SyncResult syncPTP();
    SyncResult syncCUDA(uint32_t gpu_id);
    
    TimeSyncConfig config_;
    std::atomic<int64_t> current_offset_{0};
    std::map<uint32_t, int64_t> gpu_offsets_;
    std::vector<SyncResult> sync_history_;
    std::mutex mutex_;
};

} // namespace tracesmith::cluster
```

#### 2.2 Clock Correlation

```cpp
// include/tracesmith/cluster/clock_correlator.hpp

#pragma once

#include "tracesmith/common/types.hpp"
#include <vector>

namespace tracesmith::cluster {

/// Correlate timestamps from different sources
class ClockCorrelator {
public:
    ClockCorrelator();
    
    // Add correlation points
    void addCorrelationPoint(
        const std::string& source_id,
        Timestamp source_time,
        Timestamp reference_time
    );
    
    // Calculate offset
    int64_t calculateOffset(const std::string& source_id) const;
    
    // Apply correction to events
    void correctTimestamps(
        const std::string& source_id,
        std::vector<TraceEvent>& events
    );
    
    // Linear regression for drift compensation
    struct DriftModel {
        double offset;      // Base offset
        double drift_rate;  // ns per second
        double r_squared;   // Model quality
    };
    DriftModel calculateDriftModel(const std::string& source_id) const;
    
private:
    struct CorrelationPoint {
        Timestamp source_time;
        Timestamp reference_time;
    };
    std::map<std::string, std::vector<CorrelationPoint>> correlation_data_;
};

} // namespace tracesmith::cluster
```

---

### Phase 3: NCCL Communication Tracking (v0.7.2)

**Goal**: Track NCCL collective operations and correlate with GPU events.

#### 3.1 NCCL Tracker

```cpp
// include/tracesmith/cluster/nccl_tracker.hpp

#pragma once

#include "tracesmith/common/types.hpp"
#include <vector>
#include <functional>

namespace tracesmith::cluster {

/// NCCL operation types
enum class NCCLOpType {
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
    Sum,
    Prod,
    Max,
    Min,
    Avg
};

/// NCCL data types
enum class NCCLDataType {
    Int8,
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
    uint64_t op_id;                 // Unique operation ID
    NCCLOpType op_type;
    NCCLRedOp red_op;               // For reduction ops
    NCCLDataType data_type;
    
    uint64_t comm_id;               // Communicator ID
    uint32_t rank;                  // Local rank
    uint32_t world_size;            // Total ranks
    
    size_t count;                   // Element count
    size_t data_size;               // Total bytes
    
    Timestamp start_time;
    Timestamp end_time;
    uint64_t duration_ns;
    
    // For P2P operations
    int32_t peer_rank = -1;
    
    // Associated CUDA stream
    uint64_t cuda_stream;
    
    // Correlation with GPU events
    uint64_t correlation_id;
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
    bool isHooked() const;
    
    // Capture control
    void startCapture();
    void stopCapture();
    void clear();
    
    // Get captured operations
    std::vector<NCCLOperation> getOperations() const;
    std::vector<NCCLOperation> getOperationsByType(NCCLOpType type) const;
    std::vector<NCCLOperation> getOperationsByComm(uint64_t comm_id) const;
    
    // Convert to TraceEvents
    std::vector<TraceEvent> toTraceEvents() const;
    
    // Correlation with GPU events
    void correlateWithGPUEvents(std::vector<TraceEvent>& gpu_events);
    
    // Statistics
    struct Statistics {
        uint64_t total_operations;
        uint64_t total_bytes_transferred;
        uint64_t total_duration_ns;
        std::map<NCCLOpType, uint64_t> ops_by_type;
        std::map<NCCLOpType, uint64_t> bytes_by_type;
        std::map<NCCLOpType, uint64_t> duration_by_type;
    };
    Statistics getStatistics() const;
    
    // Callback for real-time notification
    using OperationCallback = std::function<void(const NCCLOperation&)>;
    void setOperationCallback(OperationCallback callback);
    
private:
    // Hook handlers (called from NCCL intercept)
    static void onNCCLCall(NCCLOpType type, void* args);
    static void onNCCLComplete(uint64_t op_id);
    
    NCCLTrackerConfig config_;
    std::vector<NCCLOperation> operations_;
    std::map<uint64_t, NCCLOperation> pending_ops_;
    OperationCallback callback_;
    
    std::atomic<bool> capturing_{false};
    std::atomic<uint64_t> op_counter_{0};
    mutable std::mutex mutex_;
    
    static NCCLTracker* instance_;
};

} // namespace tracesmith::cluster
```

#### 3.2 Communication Matrix Analysis

```cpp
// include/tracesmith/cluster/comm_analysis.hpp

#pragma once

#include "tracesmith/cluster/nccl_tracker.hpp"
#include <Eigen/Dense>  // Optional: for matrix operations

namespace tracesmith::cluster {

/// Communication pattern analysis
class CommAnalysis {
public:
    CommAnalysis();
    
    // Build from NCCL operations
    void addOperations(const std::vector<NCCLOperation>& ops);
    
    // Communication matrix (rank x rank)
    struct CommMatrix {
        std::vector<std::vector<uint64_t>> bytes;       // Bytes transferred
        std::vector<std::vector<uint64_t>> count;       // Operation count
        std::vector<std::vector<double>> avg_latency;   // Average latency
        uint32_t world_size;
    };
    CommMatrix getCommMatrix() const;
    
    // Pattern detection
    enum class CommPattern {
        AllToAll,
        Ring,
        Tree,
        Butterfly,
        Custom
    };
    CommPattern detectPattern() const;
    
    // Bottleneck analysis
    struct Bottleneck {
        uint32_t rank_a;
        uint32_t rank_b;
        double utilization;
        std::string reason;
    };
    std::vector<Bottleneck> findBottlenecks() const;
    
    // Load imbalance
    struct LoadImbalance {
        uint32_t rank;
        double deviation;       // From average
        uint64_t total_bytes;
        uint64_t total_time_ns;
    };
    std::vector<LoadImbalance> analyzeLoadBalance() const;
    
    // Visualization
    std::string matrixToASCII() const;
    std::string matrixToHeatmapJSON() const;
    
private:
    std::vector<NCCLOperation> operations_;
    uint32_t world_size_ = 0;
};

} // namespace tracesmith::cluster
```

---

### Phase 4: Node Aggregator (v0.8.0)

**Goal**: Aggregate GPU traces within a single node and prepare for cluster-wide collection.

#### 4.1 Node Aggregator

```cpp
// include/tracesmith/cluster/node_aggregator.hpp

#pragma once

#include "tracesmith/cluster/multi_gpu_profiler.hpp"
#include "tracesmith/cluster/nccl_tracker.hpp"
#include "tracesmith/cluster/time_sync.hpp"
#include "tracesmith/cluster/gpu_topology.hpp"

namespace tracesmith::cluster {

/// Node identification
struct NodeInfo {
    std::string node_id;            // Unique node identifier
    std::string hostname;
    std::string ip_address;
    uint32_t gpu_count;
    std::vector<DeviceInfo> gpus;
    GPUTopologyInfo topology;
};

/// Node aggregator configuration
struct NodeAggregatorConfig {
    std::string node_id;            // Auto-generated if empty
    MultiGPUConfig multi_gpu_config;
    NCCLTrackerConfig nccl_config;
    TimeSyncConfig time_sync_config;
    
    size_t aggregation_buffer_size = 10 * 1024 * 1024;  // 10M events
    uint32_t flush_interval_ms = 1000;
    bool enable_compression = true;
};

/// Node-level trace aggregator
class NodeAggregator {
public:
    explicit NodeAggregator(const NodeAggregatorConfig& config = {});
    ~NodeAggregator();
    
    // Lifecycle
    bool initialize();
    void finalize();
    
    // Node info
    NodeInfo getNodeInfo() const;
    
    // Capture control
    bool startCapture();
    bool stopCapture();
    bool isCapturing() const;
    
    // Event retrieval
    TraceRecord exportTrace();
    size_t getEventCount() const;
    
    // Time synchronization
    void synchronizeTime();
    int64_t getTimeOffset() const;
    
    // NCCL data
    std::vector<NCCLOperation> getNCCLOperations() const;
    
    // Statistics
    struct Statistics {
        uint64_t total_events;
        uint64_t events_per_gpu[16];
        uint64_t nccl_operations;
        uint64_t nccl_bytes;
        Timestamp start_time;
        Timestamp end_time;
        double capture_duration_sec;
    };
    Statistics getStatistics() const;
    
    // Serialization for network transfer
    std::vector<uint8_t> serializeEvents(bool compress = true);
    
private:
    void aggregationLoop();
    void correlateEvents();
    
    NodeAggregatorConfig config_;
    NodeInfo node_info_;
    
    std::unique_ptr<MultiGPUProfiler> multi_gpu_profiler_;
    std::unique_ptr<NCCLTracker> nccl_tracker_;
    std::unique_ptr<TimeSync> time_sync_;
    std::unique_ptr<GPUTopology> topology_;
    
    std::vector<TraceEvent> aggregated_events_;
    
    std::thread aggregation_thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
};

} // namespace tracesmith::cluster
```

---

### Phase 5: Cluster Master (v0.9.0)

**Goal**: Coordinate profiling across multiple nodes and provide unified analysis.

#### 5.1 Cluster Master

```cpp
// include/tracesmith/cluster/cluster_master.hpp

#pragma once

#include "tracesmith/cluster/node_aggregator.hpp"
#include "tracesmith/cluster/comm_analysis.hpp"
#include <grpcpp/grpcpp.h>

namespace tracesmith::cluster {

/// Remote node connection
struct NodeConnection {
    std::string node_id;
    std::string address;
    uint16_t port;
    bool connected;
    NodeInfo info;
    int64_t time_offset;
    Timestamp last_heartbeat;
};

/// Cluster master configuration
struct ClusterMasterConfig {
    uint16_t listen_port = 50051;
    uint32_t heartbeat_interval_ms = 5000;
    uint32_t node_timeout_ms = 30000;
    TimeSyncConfig time_sync_config;
    
    size_t max_events_per_node = 10 * 1024 * 1024;
    bool enable_streaming = true;
};

/// Cluster-wide profiling master
class ClusterMaster {
public:
    explicit ClusterMaster(const ClusterMasterConfig& config = {});
    ~ClusterMaster();
    
    // Server control
    bool start();
    void stop();
    bool isRunning() const;
    
    // Node management
    void registerNode(const std::string& address, uint16_t port);
    void unregisterNode(const std::string& node_id);
    std::vector<NodeConnection> getConnectedNodes() const;
    bool isNodeConnected(const std::string& node_id) const;
    
    // Cluster-wide capture
    bool startClusterCapture();
    bool stopClusterCapture();
    
    // Time synchronization
    void synchronizeClusterTime();
    std::map<std::string, int64_t> getNodeTimeOffsets() const;
    
    // Event collection
    TraceRecord collectAllTraces();
    TraceRecord collectTracesFromNodes(const std::vector<std::string>& node_ids);
    
    // Real-time streaming
    using EventCallback = std::function<void(const std::string& node_id, 
                                              const TraceEvent& event)>;
    void subscribeToEvents(EventCallback callback);
    void unsubscribeFromEvents();
    
    // Analysis
    struct ClusterStatistics {
        uint32_t total_nodes;
        uint32_t total_gpus;
        uint64_t total_events;
        uint64_t total_nccl_ops;
        uint64_t total_nccl_bytes;
        std::map<std::string, NodeAggregator::Statistics> per_node_stats;
    };
    ClusterStatistics getClusterStatistics() const;
    
    // Communication analysis
    CommAnalysis::CommMatrix getClusterCommMatrix() const;
    
    // Timeline
    struct ClusterTimeline {
        Timestamp start_time;
        Timestamp end_time;
        std::vector<TraceEvent> events;         // All events, sorted
        std::vector<NCCLOperation> nccl_ops;    // All NCCL ops
        GPUTopologyInfo cluster_topology;
    };
    ClusterTimeline buildClusterTimeline();
    
    // Export
    bool exportToSBT(const std::string& filename);
    bool exportToPerfetto(const std::string& filename);
    bool exportToJSON(const std::string& filename);
    
private:
    // gRPC service implementation
    class ClusterServiceImpl;
    
    void heartbeatLoop();
    void collectFromNode(const std::string& node_id);
    void mergeTraces(TraceRecord& target, const TraceRecord& source);
    
    ClusterMasterConfig config_;
    std::map<std::string, NodeConnection> nodes_;
    
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<ClusterServiceImpl> service_;
    std::unique_ptr<TimeSync> time_sync_;
    
    std::thread heartbeat_thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
};

} // namespace tracesmith::cluster
```

#### 5.2 gRPC Service Definition

```protobuf
// proto/tracesmith_cluster.proto

syntax = "proto3";

package tracesmith.cluster;

service ClusterService {
    // Node registration
    rpc RegisterNode(RegisterNodeRequest) returns (RegisterNodeResponse);
    rpc Heartbeat(HeartbeatRequest) returns (HeartbeatResponse);
    
    // Capture control
    rpc StartCapture(StartCaptureRequest) returns (StartCaptureResponse);
    rpc StopCapture(StopCaptureRequest) returns (StopCaptureResponse);
    
    // Data collection
    rpc CollectTrace(CollectTraceRequest) returns (stream TraceChunk);
    rpc StreamEvents(StreamEventsRequest) returns (stream TraceEvent);
    
    // Time sync
    rpc SyncTime(SyncTimeRequest) returns (SyncTimeResponse);
}

message RegisterNodeRequest {
    string node_id = 1;
    string hostname = 2;
    uint32 gpu_count = 3;
    repeated DeviceInfo gpus = 4;
}

message RegisterNodeResponse {
    bool success = 1;
    string assigned_node_id = 2;
    int64 time_offset_ns = 3;
}

message TraceChunk {
    string node_id = 1;
    uint64 chunk_id = 2;
    bytes compressed_data = 3;
    uint64 event_count = 4;
    bool is_last = 5;
}

message TraceEvent {
    uint64 timestamp = 1;
    uint32 type = 2;
    string name = 3;
    uint32 device_id = 4;
    uint32 stream_id = 5;
    uint64 correlation_id = 6;
    uint64 duration = 7;
    string node_id = 8;
    bytes extra_data = 9;
}

message DeviceInfo {
    string name = 1;
    uint32 device_index = 2;
    uint64 total_memory = 3;
    uint32 compute_capability_major = 4;
    uint32 compute_capability_minor = 5;
}

// ... additional messages
```

---

### Phase 6: Python Bindings & CLI (v0.9.1)

#### 6.1 Python API

```python
# python/tracesmith/cluster/__init__.py

from tracesmith._tracesmith import (
    MultiGPUProfiler,
    MultiGPUConfig,
    GPUTopology,
    NodeAggregator,
    NodeAggregatorConfig,
    ClusterMaster,
    ClusterMasterConfig,
    NCCLTracker,
    NCCLOperation,
    TimeSync,
)

__all__ = [
    'MultiGPUProfiler',
    'MultiGPUConfig',
    'GPUTopology',
    'NodeAggregator',
    'NodeAggregatorConfig', 
    'ClusterMaster',
    'ClusterMasterConfig',
    'NCCLTracker',
    'NCCLOperation',
    'TimeSync',
]
```

#### 6.2 CLI Commands

```bash
# Start node agent
tracesmith cluster agent --port 50052

# Start cluster master
tracesmith cluster master --port 50051

# Register nodes
tracesmith cluster add-node --address node1:50052
tracesmith cluster add-node --address node2:50052

# Cluster-wide capture
tracesmith cluster record -o cluster_trace.sbt -d 10

# Show cluster status
tracesmith cluster status

# Analyze cluster communication
tracesmith cluster analyze-comm cluster_trace.sbt
```

---

## File Structure

```
include/tracesmith/cluster/
├── multi_gpu_profiler.hpp
├── gpu_topology.hpp
├── time_sync.hpp
├── clock_correlator.hpp
├── nccl_tracker.hpp
├── comm_analysis.hpp
├── node_aggregator.hpp
├── cluster_master.hpp
└── cluster_types.hpp

src/cluster/
├── CMakeLists.txt
├── multi_gpu_profiler.cpp
├── gpu_topology.cpp
├── time_sync.cpp
├── clock_correlator.cpp
├── nccl_tracker.cpp
├── comm_analysis.cpp
├── node_aggregator.cpp
├── cluster_master.cpp
└── cluster_service.cpp

proto/
└── tracesmith_cluster.proto

examples/
├── multi_gpu_example.cpp
├── nccl_tracking_example.cpp
├── cluster_agent_example.cpp
└── cluster_master_example.cpp

tests/
├── test_multi_gpu.cpp
├── test_topology.cpp
├── test_time_sync.cpp
├── test_nccl_tracker.cpp
└── test_cluster.cpp
```

---

## Dependencies

| Dependency | Purpose | Required |
|------------|---------|----------|
| **NVML** | GPU topology, monitoring | Yes (NVIDIA) |
| **CUDA Driver** | Multi-GPU management | Yes (NVIDIA) |
| **gRPC** | Cluster communication | Phase 4+ |
| **Protobuf** | Serialization | Phase 4+ |
| **NCCL** | Collective tracking | Phase 3 |
| **libunwind** | Call stack capture | Optional |
| **zstd** | Compression | Optional |

---

## Timeline

| Phase | Version | Features | ETA |
|-------|---------|----------|-----|
| 1 | v0.7.0 | Multi-GPU single node | 2 weeks |
| 2 | v0.7.1 | Time synchronization | 1 week |
| 3 | v0.7.2 | NCCL tracking | 2 weeks |
| 4 | v0.8.0 | Node aggregator | 2 weeks |
| 5 | v0.9.0 | Cluster master | 3 weeks |
| 6 | v0.9.1 | Python & CLI | 1 week |

**Total: ~11 weeks**

---

## Success Metrics

1. **Single-node multi-GPU**: Profile 8 GPUs with <1% overhead
2. **Time sync**: Cross-node accuracy <100µs with PTP
3. **NCCL tracking**: Capture all collective operations with <5% overhead
4. **Cluster scale**: Support 64+ nodes, 512+ GPUs
5. **Data volume**: Handle 100M+ events per capture session

---

## References

- [NVIDIA NVML Documentation](https://developer.nvidia.com/nvidia-management-library-nvml)
- [NCCL Documentation](https://docs.nvidia.com/deeplearning/nccl/user-guide/docs/)
- [Precision Time Protocol (IEEE 1588)](https://en.wikipedia.org/wiki/Precision_Time_Protocol)
- [gRPC Documentation](https://grpc.io/docs/)

