/**
 * BPF Tracer Implementation
 * 
 * Provides eBPF-based GPU event tracing on Linux.
 * Falls back to no-op on other platforms.
 * 
 * Full implementation requires:
 * - Linux kernel >= 4.14
 * - libbpf library
 * - CAP_BPF or root privileges
 */

#include "tracesmith/capture/bpf_types.hpp"
#include <algorithm>
#include <fstream>
#include <cstring>
#include <cstdio>

#ifdef __linux__
#include <unistd.h>
#include <sys/utsname.h>
#endif

namespace tracesmith {

// Static method implementations

bool BPFTracer::isAvailable() {
#ifdef __linux__
    // Check kernel version (need >= 4.14 for BPF features)
    struct utsname info;
    if (uname(&info) != 0) {
        return false;
    }
    
    int major = 0, minor = 0;
    if (sscanf(info.release, "%d.%d", &major, &minor) < 2) {
        return false;
    }
    
    // Require at least kernel 4.14
    if (major < 4 || (major == 4 && minor < 14)) {
        return false;
    }
    
    // Check for BTF support (needed for CO-RE)
    std::ifstream btf("/sys/kernel/btf/vmlinux");
    if (!btf.good()) {
        // BTF not available, but basic BPF might still work
        // Check for BPF filesystem
        std::ifstream bpffs("/sys/fs/bpf");
        return bpffs.good();
    }
    
    return true;
#else
    return false;  // BPF only available on Linux
#endif
}

std::vector<std::string> BPFTracer::getGPUTracepoints() {
    std::vector<std::string> tracepoints;
    
#ifdef __linux__
    // Common GPU-related tracepoints to check
    const std::vector<std::string> potential_tracepoints = {
        // NVIDIA UVM tracepoints
        "nvidia_uvm:uvm_fault",
        "nvidia_uvm:uvm_migrate",
        "nvidia_uvm:uvm_evict",
        "nvidia_uvm:uvm_prefetch",
        
        // DRM subsystem tracepoints (AMD, Intel)
        "drm:drm_vblank_event",
        "drm_sched:drm_sched_job",
        "drm_sched:drm_sched_process_job",
        
        // AMDGPU tracepoints
        "amdgpu:amdgpu_cs_ioctl",
        "amdgpu:amdgpu_vm_bo_map",
        "amdgpu:amdgpu_vm_bo_unmap",
        "amdgpu:amdgpu_ttm_bo_move",
        
        // DMA-buf tracepoints
        "dma_fence:dma_fence_emit",
        "dma_fence:dma_fence_signaled",
        
        // PCIe tracepoints
        "pci:pci_bus_read_config",
        "pci:pci_bus_write_config",
        
        // Syscall tracepoints for driver calls
        "raw_syscalls:sys_enter",
        "raw_syscalls:sys_exit"
    };
    
    // Check which tracepoints are available
    for (const auto& tp : potential_tracepoints) {
        // Parse category:name format
        size_t colon = tp.find(':');
        if (colon == std::string::npos) continue;
        
        std::string category = tp.substr(0, colon);
        std::string name = tp.substr(colon + 1);
        
        std::string path = "/sys/kernel/debug/tracing/events/" + 
                          category + "/" + name + "/id";
        std::ifstream f(path);
        if (f.good()) {
            tracepoints.push_back(tp);
        }
    }
#endif
    
    return tracepoints;
}

std::vector<TraceEvent> BPFTracer::convertToTraceEvents(
    const std::vector<BPFEventRecord>& bpf_events) {
    
    std::vector<TraceEvent> events;
    events.reserve(bpf_events.size());
    
    for (const auto& bpf_event : bpf_events) {
        events.push_back(bpfEventToTraceEvent(bpf_event));
        stats_.events_received++;
    }
    
    return events;
}

// ============================================================================
// Linux-specific BPFTracer implementation
// ============================================================================

#ifdef __linux__

// Note: Full implementation would use libbpf
// This is a placeholder that shows the intended API

class LinuxBPFTracer : public BPFTracer {
public:
    explicit LinuxBPFTracer(const Config& config) : BPFTracer(config) {}
    
    bool loadProgram(const std::string& path) override {
        // In a full implementation, this would:
        // 1. Use bpf_object__open_file() to open the BPF object
        // 2. Use bpf_object__load() to load it into the kernel
        // 3. Parse maps and programs from the object
        
        program_info_.path = path;
        program_info_.loaded = false;  // Would be true after successful load
        
        // For now, return false as libbpf is not linked
        return false;
    }
    
    int attach(const std::string& pattern) override {
        if (!program_info_.loaded) {
            return 0;
        }
        
        // In a full implementation, this would:
        // 1. Find matching kernel functions via /proc/kallsyms
        // 2. Use bpf_program__attach_kprobe() for each
        // 3. Store attachment handles for cleanup
        
        (void)pattern;
        return 0;
    }
    
    void detach() override {
        // Would call bpf_link__destroy() for each attached probe
        program_info_.attached = false;
    }
    
    bool start() override {
        if (!program_info_.attached) {
            return false;
        }
        
        // In a full implementation, this would:
        // 1. Set up ring buffer callback
        // 2. Start polling thread
        
        running_ = true;
        return true;
    }
    
    void stop() override {
        running_ = false;
    }
    
    std::vector<BPFEventRecord> pollEvents(size_t max_events) override {
        std::vector<BPFEventRecord> events;
        
        if (!running_) {
            return events;
        }
        
        // In a full implementation, this would:
        // 1. Call ring_buffer__poll() or perf_buffer__poll()
        // 2. Process events via callback
        // 3. Return accumulated events
        
        stats_.poll_count++;
        (void)max_events;
        
        return events;
    }
};

#endif // __linux__

// ============================================================================
// BPF Availability Check Helper
// ============================================================================

struct BPFAvailability {
    bool available = false;
    bool btf_available = false;
    bool has_permissions = false;
    std::string kernel_version;
    std::string error_message;
};

BPFAvailability checkBPFAvailability() {
    BPFAvailability result;
    
#ifdef __linux__
    // Check kernel version
    struct utsname info;
    if (uname(&info) == 0) {
        result.kernel_version = info.release;
        
        int major = 0, minor = 0;
        sscanf(info.release, "%d.%d", &major, &minor);
        
        if (major >= 5 || (major == 4 && minor >= 14)) {
            result.available = true;
        } else {
            result.error_message = "Kernel version too old (need >= 4.14)";
        }
    }
    
    // Check BTF
    std::ifstream btf("/sys/kernel/btf/vmlinux");
    result.btf_available = btf.good();
    
    // Check permissions (simplified)
    result.has_permissions = (geteuid() == 0);
    if (!result.has_permissions) {
        // Could also check CAP_BPF, CAP_SYS_ADMIN
        result.error_message = "Root or CAP_BPF required";
    }
#else
    result.error_message = "BPF only available on Linux";
#endif
    
    return result;
}

} // namespace tracesmith

