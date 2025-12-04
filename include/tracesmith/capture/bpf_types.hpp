#pragma once

/**
 * eBPF Integration Types (v0.4.0)
 *
 * eBPF (extended Berkeley Packet Filter) enables kernel-level tracing
 * with zero application modification and minimal overhead.
 *
 * This module provides:
 * - BPF event types for GPU driver tracing
 * - Perf event integration structures
 * - Kernel-user space communication types
 *
 * Usage on Linux:
 *   // Load BPF program
 *   BPFTracer tracer;
 *   tracer.loadProgram("gpu_trace.bpf.o");
 *   tracer.attach("nvidia_uvm_*");
 *
 *   // Collect events
 *   auto events = tracer.pollEvents();
 *
 * Note: eBPF requires Linux kernel >= 4.14 and root/CAP_BPF privileges.
 */

#include "tracesmith/common/types.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace tracesmith {

/// BPF event types for GPU operations
enum class BPFEventType : uint32_t {
    Unknown = 0,

    // CUDA/NVIDIA events
    CudaLaunchKernel = 1,
    CudaMemcpy = 2,
    CudaMalloc = 3,
    CudaFree = 4,
    CudaSynchronize = 5,
    CudaSetDevice = 6,

    // UVM (Unified Virtual Memory) events
    UvmFault = 10,
    UvmMigrate = 11,
    UvmEvict = 12,
    UvmPrefetch = 13,

    // Driver-level events
    DriverIoctl = 20,
    DriverMmap = 21,
    DriverOpen = 22,
    DriverClose = 23,

    // PCIe events
    PcieDmaTransfer = 30,
    PcieMsiInterrupt = 31,

    // ROCm/AMD events
    HipLaunchKernel = 40,
    HipMemcpy = 41,
    HipMalloc = 42,
    HipFree = 43,

    // Custom events
    Custom = 100
};

/// Convert BPFEventType to string
inline const char* bpfEventTypeToString(BPFEventType type) {
    switch (type) {
        case BPFEventType::CudaLaunchKernel: return "cuda_launch_kernel";
        case BPFEventType::CudaMemcpy: return "cuda_memcpy";
        case BPFEventType::CudaMalloc: return "cuda_malloc";
        case BPFEventType::CudaFree: return "cuda_free";
        case BPFEventType::CudaSynchronize: return "cuda_synchronize";
        case BPFEventType::CudaSetDevice: return "cuda_set_device";
        case BPFEventType::UvmFault: return "uvm_fault";
        case BPFEventType::UvmMigrate: return "uvm_migrate";
        case BPFEventType::UvmEvict: return "uvm_evict";
        case BPFEventType::UvmPrefetch: return "uvm_prefetch";
        case BPFEventType::DriverIoctl: return "driver_ioctl";
        case BPFEventType::DriverMmap: return "driver_mmap";
        case BPFEventType::DriverOpen: return "driver_open";
        case BPFEventType::DriverClose: return "driver_close";
        case BPFEventType::PcieDmaTransfer: return "pcie_dma_transfer";
        case BPFEventType::PcieMsiInterrupt: return "pcie_msi_interrupt";
        case BPFEventType::HipLaunchKernel: return "hip_launch_kernel";
        case BPFEventType::HipMemcpy: return "hip_memcpy";
        case BPFEventType::HipMalloc: return "hip_malloc";
        case BPFEventType::HipFree: return "hip_free";
        default: return "unknown";
    }
}

/// BPF event record (matches kernel-side structure)
/// This is the raw event format from the BPF ring buffer
struct BPFEventRecord {
    uint64_t timestamp_ns;      // Kernel timestamp (boot time)
    uint32_t pid;               // Process ID
    uint32_t tid;               // Thread ID
    uint32_t cpu;               // CPU core
    BPFEventType type;          // Event type

    union {
        // Kernel launch data
        struct {
            uint64_t correlation_id;
            uint64_t stream_handle;
            uint32_t grid_x, grid_y, grid_z;
            uint32_t block_x, block_y, block_z;
            uint32_t shared_mem;
            char kernel_name[64];
        } kernel;

        // Memory operation data
        struct {
            uint64_t src_addr;
            uint64_t dst_addr;
            uint64_t size;
            uint32_t direction;  // 0=H2D, 1=D2H, 2=D2D
            uint32_t async;
        } memop;

        // UVM fault data
        struct {
            uint64_t fault_addr;
            uint64_t page_size;
            uint32_t fault_type;  // Read/Write/Prefetch
            uint32_t gpu_id;
        } uvm;

        // PCIe transfer data
        struct {
            uint64_t addr;
            uint32_t size;
            uint32_t direction;  // 0=to_device, 1=from_device
        } pcie;

        // Generic data buffer
        uint8_t raw_data[128];
    } data;

    BPFEventRecord() : timestamp_ns(0), pid(0), tid(0), cpu(0),
                       type(BPFEventType::Unknown) {
        std::memset(&data, 0, sizeof(data));
    }
};

/// BPF program info
struct BPFProgramInfo {
    std::string name;
    std::string path;
    uint32_t id;
    bool loaded;
    bool attached;

    // Attach points
    std::vector<std::string> kprobes;    // Kernel function probes
    std::vector<std::string> uprobes;    // User function probes
    std::vector<std::string> tracepoints; // Kernel tracepoints

    BPFProgramInfo() : id(0), loaded(false), attached(false) {}
};

/// BPF tracer statistics
struct BPFTracerStats {
    uint64_t events_received = 0;
    uint64_t events_dropped = 0;
    uint64_t bytes_received = 0;
    uint64_t poll_count = 0;
    double total_time_ms = 0;

    BPFTracerStats() = default;
};

/// BPF tracer interface (platform-independent)
/// Actual implementation requires Linux and libbpf
class BPFTracer {
public:
    /// Configuration
    struct Config {
        size_t ring_buffer_pages = 64;  // Ring buffer size (pages)
        uint32_t poll_timeout_ms = 100; // Poll timeout
        bool capture_stack = false;     // Capture call stacks
        uint32_t max_stack_depth = 16;  // Max stack frames

        // Filter options
        uint32_t target_pid = 0;        // 0 = all processes
        std::vector<BPFEventType> event_filter;

        Config() = default;
    };

    BPFTracer() = default;
    explicit BPFTracer(const Config& config) : config_(config) {}
    virtual ~BPFTracer() = default;

    /// Load BPF program from object file
    /// @param path Path to compiled .bpf.o file
    /// @return true if loaded successfully
    virtual bool loadProgram(const std::string& path) {
        // Base implementation - platform-specific code needed
        (void)path;
        return false;
    }

    /// Attach to kernel probe points
    /// @param pattern Function name pattern (e.g., "nvidia_*", "cuda_*")
    /// @return Number of attach points
    virtual int attach(const std::string& pattern) {
        (void)pattern;
        return 0;
    }

    /// Detach from all probe points
    virtual void detach() {}

    /// Start collecting events
    virtual bool start() { return false; }

    /// Stop collecting events
    virtual void stop() {}

    /// Poll for new events
    /// @param max_events Maximum events to return
    /// @return Vector of raw BPF events
    virtual std::vector<BPFEventRecord> pollEvents(size_t max_events = 1000) {
        (void)max_events;
        return {};
    }

    /// Convert BPF events to TraceSmith events
    std::vector<TraceEvent> convertToTraceEvents(
        const std::vector<BPFEventRecord>& bpf_events);

    /// Get program info
    const BPFProgramInfo& getProgramInfo() const { return program_info_; }

    /// Get statistics
    const BPFTracerStats& getStatistics() const { return stats_; }

    /// Check if BPF is available on this system
    static bool isAvailable();

    /// Get list of available GPU-related tracepoints
    static std::vector<std::string> getGPUTracepoints();

protected:
    Config config_;
    BPFProgramInfo program_info_;
    BPFTracerStats stats_;
    bool running_ = false;
};

/// Convert BPF event to TraceEvent
inline TraceEvent bpfEventToTraceEvent(const BPFEventRecord& bpf_event) {
    TraceEvent event;
    event.timestamp = bpf_event.timestamp_ns;
    event.thread_id = bpf_event.tid;

    // Map BPF event type to TraceSmith event type
    switch (bpf_event.type) {
        case BPFEventType::CudaLaunchKernel:
        case BPFEventType::HipLaunchKernel: {
            event.type = EventType::KernelLaunch;
            event.name = bpf_event.data.kernel.kernel_name;
            event.stream_id = bpf_event.data.kernel.stream_handle & 0xFFFFFFFF;
            event.correlation_id = bpf_event.data.kernel.correlation_id;

            // Set kernel params
            KernelParams kp;
            kp.grid_x = bpf_event.data.kernel.grid_x;
            kp.grid_y = bpf_event.data.kernel.grid_y;
            kp.grid_z = bpf_event.data.kernel.grid_z;
            kp.block_x = bpf_event.data.kernel.block_x;
            kp.block_y = bpf_event.data.kernel.block_y;
            kp.block_z = bpf_event.data.kernel.block_z;
            kp.shared_mem_bytes = bpf_event.data.kernel.shared_mem;
            event.kernel_params = kp;
            break;
        }

        case BPFEventType::CudaMemcpy:
        case BPFEventType::HipMemcpy: {
            switch (bpf_event.data.memop.direction) {
                case 0: event.type = EventType::MemcpyH2D; break;
                case 1: event.type = EventType::MemcpyD2H; break;
                case 2: event.type = EventType::MemcpyD2D; break;
                default: event.type = EventType::MemcpyH2D; break;
            }
            event.name = "memcpy";

            // Set memory params
            MemoryParams mp;
            mp.src_address = bpf_event.data.memop.src_addr;
            mp.dst_address = bpf_event.data.memop.dst_addr;
            mp.size_bytes = bpf_event.data.memop.size;
            event.memory_params = mp;
            break;
        }

        case BPFEventType::CudaMalloc:
        case BPFEventType::HipMalloc:
            event.type = EventType::MemAlloc;
            event.name = "malloc";
            break;

        case BPFEventType::CudaFree:
        case BPFEventType::HipFree:
            event.type = EventType::MemFree;
            event.name = "free";
            break;

        case BPFEventType::CudaSynchronize:
            event.type = EventType::DeviceSync;
            event.name = "synchronize";
            break;

        case BPFEventType::UvmFault:
            event.type = EventType::Custom;
            event.name = "uvm_fault";
            event.metadata["fault_addr"] = std::to_string(bpf_event.data.uvm.fault_addr);
            event.metadata["page_size"] = std::to_string(bpf_event.data.uvm.page_size);
            break;

        case BPFEventType::UvmMigrate:
            event.type = EventType::Custom;
            event.name = "uvm_migrate";
            break;

        default:
            event.type = EventType::Custom;
            event.name = bpfEventTypeToString(bpf_event.type);
            break;
    }

    // Add BPF-specific metadata
    event.metadata["bpf_pid"] = std::to_string(bpf_event.pid);
    event.metadata["bpf_cpu"] = std::to_string(bpf_event.cpu);

    return event;
}

} // namespace tracesmith

