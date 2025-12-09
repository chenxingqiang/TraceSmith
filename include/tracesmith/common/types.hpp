#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <map>

namespace tracesmith {

/// Version information
constexpr uint32_t VERSION_MAJOR = 0;
constexpr uint32_t VERSION_MINOR = 8;
constexpr uint32_t VERSION_PATCH = 2;

/// Event types that can be captured
enum class EventType : uint8_t {
    Unknown = 0,
    KernelLaunch,      // GPU kernel launch
    KernelComplete,    // GPU kernel completion
    MemcpyH2D,         // Host to Device memory copy
    MemcpyD2H,         // Device to Host memory copy
    MemcpyD2D,         // Device to Device memory copy
    MemsetDevice,      // Device memory set
    StreamSync,        // Stream synchronization
    DeviceSync,        // Device synchronization
    EventRecord,       // CUDA/HIP event record
    EventSync,         // Event synchronization
    StreamCreate,      // Stream creation
    StreamDestroy,     // Stream destruction
    ContextCreate,     // Context creation
    ContextDestroy,    // Context destruction
    MemAlloc,          // Memory allocation
    MemFree,           // Memory deallocation
    Marker,            // User-defined marker
    RangeStart,        // Range start (for nested profiling)
    RangeEnd,          // Range end
    // NCCL events (v0.7.1)
    NCCLStart,         // NCCL collective operation start
    NCCLComplete,      // NCCL collective operation complete
    Custom = 255       // Custom event type
};

/// Convert EventType to string
inline const char* eventTypeToString(EventType type) {
    switch (type) {
        case EventType::KernelLaunch:    return "KernelLaunch";
        case EventType::KernelComplete:  return "KernelComplete";
        case EventType::MemcpyH2D:       return "MemcpyH2D";
        case EventType::MemcpyD2H:       return "MemcpyD2H";
        case EventType::MemcpyD2D:       return "MemcpyD2D";
        case EventType::MemsetDevice:    return "MemsetDevice";
        case EventType::StreamSync:      return "StreamSync";
        case EventType::DeviceSync:      return "DeviceSync";
        case EventType::EventRecord:     return "EventRecord";
        case EventType::EventSync:       return "EventSync";
        case EventType::StreamCreate:    return "StreamCreate";
        case EventType::StreamDestroy:   return "StreamDestroy";
        case EventType::ContextCreate:   return "ContextCreate";
        case EventType::ContextDestroy:  return "ContextDestroy";
        case EventType::MemAlloc:        return "MemAlloc";
        case EventType::MemFree:         return "MemFree";
        case EventType::Marker:          return "Marker";
        case EventType::RangeStart:      return "RangeStart";
        case EventType::RangeEnd:        return "RangeEnd";
        case EventType::NCCLStart:       return "NCCLStart";
        case EventType::NCCLComplete:    return "NCCLComplete";
        case EventType::Custom:          return "Custom";
        default:                         return "Unknown";
    }
}

/// Stack frame for call stack capture
struct StackFrame {
    uint64_t address;           // Instruction pointer address
    std::string function_name;  // Demangled function name (if available)
    std::string file_name;      // Source file (if available)
    uint32_t line_number;       // Source line number (if available)
    
    StackFrame() : address(0), line_number(0) {}
    StackFrame(uint64_t addr) : address(addr), line_number(0) {}
};

/// Call stack captured at an event
struct CallStack {
    std::vector<StackFrame> frames;
    uint64_t thread_id;
    
    CallStack() : thread_id(0) {}
    
    bool empty() const { return frames.empty(); }
    size_t depth() const { return frames.size(); }
};

/// Kernel launch parameters
struct KernelParams {
    uint32_t grid_x, grid_y, grid_z;        // Grid dimensions
    uint32_t block_x, block_y, block_z;     // Block dimensions
    uint32_t shared_mem_bytes;               // Dynamic shared memory
    uint32_t registers_per_thread;           // Registers per thread
    
    KernelParams() 
        : grid_x(0), grid_y(0), grid_z(0)
        , block_x(0), block_y(0), block_z(0)
        , shared_mem_bytes(0)
        , registers_per_thread(0) {}
};

/// Memory operation parameters
struct MemoryParams {
    uint64_t src_address;
    uint64_t dst_address;
    uint64_t size_bytes;
    
    MemoryParams() : src_address(0), dst_address(0), size_bytes(0) {}
};

/// High-resolution timestamp (nanoseconds since epoch)
using Timestamp = uint64_t;

/// Get current timestamp
inline Timestamp getCurrentTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
}

/// Flow types for dependency tracking (Kineto-compatible)
enum class FlowType : uint8_t {
    None = 0,
    FwdBwd = 1,        // Forward-backward correlation
    AsyncCpuGpu = 2,   // Async CPU-GPU operation
    Custom = 255       // Custom flow type
};

/// Flow information for event dependencies (Kineto-inspired)
struct FlowInfo {
    uint64_t id;           // Flow ID for tracking dependencies
    FlowType type;         // Type of flow
    bool is_start;         // True if this is flow start, false if flow end
    
    FlowInfo() : id(0), type(FlowType::None), is_start(false) {}
    FlowInfo(uint64_t flow_id, FlowType flow_type, bool start)
        : id(flow_id), type(flow_type), is_start(start) {}
};

/// A single trace event (Kineto-compatible)
struct TraceEvent {
    EventType type;              // Type of event
    Timestamp timestamp;         // When the event occurred (nanoseconds)
    Timestamp duration;          // Duration in nanoseconds (for completed events)
    
    uint32_t device_id;          // GPU device ID
    uint32_t stream_id;          // Stream ID (resource ID in Kineto terms)
    uint64_t correlation_id;     // ID to correlate start/end events
    
    std::string name;            // Kernel name or operation description
    
    // Kineto-inspired additions for better profiling
    uint32_t thread_id;          // Thread that launched the event (0 if unknown)
    std::map<std::string, std::string> metadata;  // Flexible key-value metadata
    FlowInfo flow_info;          // Structured flow information for dependencies
    
    // Optional detailed parameters
    std::optional<KernelParams> kernel_params;
    std::optional<MemoryParams> memory_params;
    std::optional<CallStack> call_stack;
    
    // Custom data
    std::vector<uint8_t> custom_data;
    
    TraceEvent()
        : type(EventType::Unknown)
        , timestamp(0)
        , duration(0)
        , device_id(0)
        , stream_id(0)
        , correlation_id(0)
        , thread_id(0) {}
    
    TraceEvent(EventType t, Timestamp ts = 0)
        : type(t)
        , timestamp(ts ? ts : getCurrentTimestamp())
        , duration(0)
        , device_id(0)
        , stream_id(0)
        , correlation_id(0)
        , thread_id(0) {}
};

/// Memory profiling event (Kineto-compatible, v0.2.0)
struct MemoryEvent {
    Timestamp timestamp;
    uint32_t device_id;
    uint32_t thread_id;
    
    uint64_t bytes;              // Allocation size
    uint64_t ptr;                // Memory address
    bool is_allocation;          // true = alloc, false = free
    std::string allocator_name;  // e.g., "cuda_allocator", "pytorch_caching"
    
    // Memory category
    enum class Category : uint8_t {
        Unknown = 0,
        Activation,              // Activation memory
        Gradient,                // Gradient memory
        Parameter,               // Model parameters
        Temporary,               // Temporary/workspace
        Cached,                  // Cached allocation
    } category;
    
    MemoryEvent()
        : timestamp(0)
        , device_id(0)
        , thread_id(0)
        , bytes(0)
        , ptr(0)
        , is_allocation(true)
        , category(Category::Unknown) {}
};

/// Counter/metric event for time-series data (Kineto-compatible, v0.2.0)
struct CounterEvent {
    Timestamp timestamp;
    uint32_t device_id;
    uint32_t track_id;           // Counter track identifier
    
    std::string counter_name;    // e.g., "GPU Memory Bandwidth", "SM Occupancy"
    double value;                // Counter value
    std::string unit;            // e.g., "GB/s", "%", "bytes"
    
    CounterEvent()
        : timestamp(0)
        , device_id(0)
        , track_id(0)
        , value(0.0) {}
    
    CounterEvent(const std::string& name, double val, Timestamp ts = 0, 
                 const std::string& unit_str = "")
        : timestamp(ts ? ts : getCurrentTimestamp())
        , device_id(0)
        , track_id(0)
        , counter_name(name)
        , value(val)
        , unit(unit_str) {}
};

/// GPU device information
struct DeviceInfo {
    uint32_t device_id;
    std::string name;
    std::string vendor;
    
    // Compute capability (for NVIDIA)
    uint32_t compute_major;
    uint32_t compute_minor;
    
    // Memory info
    uint64_t total_memory;
    uint64_t memory_clock_rate;  // kHz
    uint32_t memory_bus_width;   // bits
    
    // Compute info
    uint32_t multiprocessor_count;
    uint32_t max_threads_per_mp;
    uint32_t clock_rate;         // kHz
    uint32_t warp_size;
    
    DeviceInfo()
        : device_id(0)
        , compute_major(0)
        , compute_minor(0)
        , total_memory(0)
        , memory_clock_rate(0)
        , memory_bus_width(0)
        , multiprocessor_count(0)
        , max_threads_per_mp(0)
        , clock_rate(0)
        , warp_size(32) {}
};

/// Trace metadata
struct TraceMetadata {
    std::string application_name;
    std::string command_line;
    Timestamp start_time;
    Timestamp end_time;
    std::string hostname;
    uint32_t process_id;
    
    std::vector<DeviceInfo> devices;
    
    TraceMetadata()
        : start_time(0)
        , end_time(0)
        , process_id(0) {}
};

/// Container for a collection of trace events
class TraceRecord {
public:
    TraceRecord() = default;
    
    void addEvent(const TraceEvent& event) {
        events_.push_back(event);
    }
    
    void addEvent(TraceEvent&& event) {
        events_.push_back(std::move(event));
    }
    
    const std::vector<TraceEvent>& events() const { return events_; }
    std::vector<TraceEvent>& events() { return events_; }
    
    size_t size() const { return events_.size(); }
    bool empty() const { return events_.empty(); }
    
    void clear() { events_.clear(); }
    
    void reserve(size_t n) { events_.reserve(n); }
    
    const TraceMetadata& metadata() const { return metadata_; }
    TraceMetadata& metadata() { return metadata_; }
    
    // Sort events by timestamp
    void sortByTimestamp();
    
    // Get events filtered by type
    std::vector<TraceEvent> filterByType(EventType type) const;
    
    // Get events for a specific stream
    std::vector<TraceEvent> filterByStream(uint32_t stream_id) const;
    
    // Get events for a specific device
    std::vector<TraceEvent> filterByDevice(uint32_t device_id) const;

private:
    TraceMetadata metadata_;
    std::vector<TraceEvent> events_;
};

} // namespace tracesmith
