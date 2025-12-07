/**
 * TraceSmith GPU Topology Discovery
 * 
 * Discovers and queries GPU interconnect topology including NVLink,
 * NVSwitch, and PCIe connections using NVML.
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tracesmith {
namespace cluster {

/**
 * GPU interconnect type
 */
enum class GPULinkType {
    None,           // No direct connection
    PCIe,           // PCIe connection
    NVLink1,        // NVLink 1.0 (20 GB/s per link)
    NVLink2,        // NVLink 2.0 (25 GB/s per link)
    NVLink3,        // NVLink 3.0 (50 GB/s per link)
    NVLink4,        // NVLink 4.0 (100 GB/s per link)
    NVSwitch,       // NVSwitch connection
    // MetaX link types
    MXLink1,        // MetaX MXLink 1.0
    MXLink2,        // MetaX MXLink 2.0
    MXSwitch        // MetaX Switch connection
};

/**
 * Convert GPULinkType to string
 */
inline const char* linkTypeToString(GPULinkType type) {
    switch (type) {
        case GPULinkType::None:     return "None";
        case GPULinkType::PCIe:     return "PCIe";
        case GPULinkType::NVLink1:  return "NVLink1";
        case GPULinkType::NVLink2:  return "NVLink2";
        case GPULinkType::NVLink3:  return "NVLink3";
        case GPULinkType::NVLink4:  return "NVLink4";
        case GPULinkType::NVSwitch: return "NVSwitch";
        case GPULinkType::MXLink1:  return "MXLink1";
        case GPULinkType::MXLink2:  return "MXLink2";
        case GPULinkType::MXSwitch: return "MXSwitch";
        default:                    return "Unknown";
    }
}

/**
 * Get bandwidth for link type (GB/s per link)
 */
inline double getLinkBandwidth(GPULinkType type) {
    switch (type) {
        case GPULinkType::NVLink1:  return 20.0;
        case GPULinkType::NVLink2:  return 25.0;
        case GPULinkType::NVLink3:  return 50.0;
        case GPULinkType::NVLink4:  return 100.0;
        case GPULinkType::PCIe:     return 16.0;   // PCIe 4.0 x16
        case GPULinkType::NVSwitch: return 900.0;  // Full bisection
        case GPULinkType::MXLink1:  return 50.0;   // MetaX MXLink 1.0
        case GPULinkType::MXLink2:  return 100.0;  // MetaX MXLink 2.0
        case GPULinkType::MXSwitch: return 800.0;  // MetaX Switch
        default:                    return 0.0;
    }
}

/**
 * Link between two GPUs
 */
struct GPULink {
    uint32_t gpu_a;             // First GPU
    uint32_t gpu_b;             // Second GPU
    GPULinkType type;           // Connection type
    uint32_t link_count;        // Number of links (e.g., 6 NVLinks)
    double bandwidth_gbps;      // Total bandwidth in GB/s
    double measured_bandwidth;  // Actual measured bandwidth (if available)
    bool bidirectional;         // True if link works in both directions
};

/**
 * GPU vendor type
 */
enum class GPUVendor {
    Unknown,
    NVIDIA,
    AMD,
    MetaX,
    Apple
};

/**
 * GPU device information for topology
 */
struct GPUDeviceTopology {
    uint32_t gpu_id;            // GPU index
    std::string name;           // Device name
    std::string pci_bus_id;     // PCI bus ID
    uint32_t numa_node;         // NUMA node affinity
    GPUVendor vendor;           // GPU vendor
    bool has_nvlink;            // Has NVLink capability (NVIDIA)
    uint32_t nvlink_count;      // Number of NVLink connections
    bool has_mxlink;            // Has MXLink capability (MetaX)
    uint32_t mxlink_count;      // Number of MXLink connections
    size_t total_memory;        // Total memory in bytes
    int compute_major;          // Compute capability major
    int compute_minor;          // Compute capability minor
};

/**
 * Complete GPU topology information
 */
struct GPUTopologyInfo {
    uint32_t gpu_count;                                     // Total GPUs
    bool has_nvswitch;                                      // Has NVSwitch
    std::vector<GPUDeviceTopology> devices;                 // Device info
    std::vector<GPULink> links;                             // All links
    std::map<std::pair<uint32_t, uint32_t>, GPULinkType> link_matrix;  // Quick lookup
};

/**
 * GPU Topology Discovery and Query
 * 
 * Uses NVML to discover GPU topology including:
 * - Number of GPUs and their properties
 * - NVLink/NVSwitch connections
 * - PCIe topology
 * - NUMA node affinity
 */
class GPUTopology {
public:
    GPUTopology();
    ~GPUTopology();
    
    // Non-copyable
    GPUTopology(const GPUTopology&) = delete;
    GPUTopology& operator=(const GPUTopology&) = delete;
    
    // =========================================================================
    // Discovery
    // =========================================================================
    
    /**
     * Discover GPU topology using NVML
     * @return true if discovery succeeded
     */
    bool discover();
    
    /**
     * Check if topology has been discovered
     */
    bool isDiscovered() const { return discovered_; }
    
    // =========================================================================
    // Query
    // =========================================================================
    
    /**
     * Get complete topology information
     */
    GPUTopologyInfo getTopology() const { return topology_; }
    
    /**
     * Get number of GPUs
     */
    uint32_t getGPUCount() const { return topology_.gpu_count; }
    
    /**
     * Get link type between two GPUs
     */
    GPULinkType getLinkType(uint32_t gpu_a, uint32_t gpu_b) const;
    
    /**
     * Get total bandwidth between two GPUs (GB/s)
     */
    double getBandwidth(uint32_t gpu_a, uint32_t gpu_b) const;
    
    /**
     * Check if peer access is possible between GPUs
     */
    bool canAccessPeer(uint32_t gpu_a, uint32_t gpu_b) const;
    
    /**
     * Get number of NVLinks between GPUs
     */
    uint32_t getNVLinkCount(uint32_t gpu_a, uint32_t gpu_b) const;
    
    /**
     * Check if GPUs are directly connected
     */
    bool isDirectlyConnected(uint32_t gpu_a, uint32_t gpu_b) const;
    
    /**
     * Get all GPUs connected to a specific GPU
     */
    std::vector<uint32_t> getConnectedGPUs(uint32_t gpu_id) const;
    
    /**
     * Get device information for a GPU
     */
    GPUDeviceTopology getDeviceInfo(uint32_t gpu_id) const;
    
    // =========================================================================
    // Path Finding
    // =========================================================================
    
    /**
     * Find optimal path between two GPUs (minimizing hops)
     * @return Vector of GPU IDs forming the path (including src and dst)
     */
    std::vector<uint32_t> getOptimalPath(uint32_t src, uint32_t dst) const;
    
    /**
     * Get estimated transfer time in microseconds
     */
    double estimateTransferTime(uint32_t src, uint32_t dst, size_t bytes) const;
    
    // =========================================================================
    // Visualization
    // =========================================================================
    
    /**
     * Generate ASCII art representation of topology
     */
    std::string toASCII() const;
    
    /**
     * Generate Graphviz DOT format
     */
    std::string toGraphviz() const;
    
    /**
     * Generate JSON representation
     */
    std::string toJSON() const;
    
    /**
     * Print topology summary to stdout
     */
    void printSummary() const;
    
private:
    // Internal discovery methods
    bool discoverNVML();
    bool discoverCUDA();
    bool discoverMACA();
    void buildLinkMatrix();
    
    GPUTopologyInfo topology_;
    bool discovered_ = false;
};

/**
 * Check if NVML is available
 */
bool isNVMLAvailable();

/**
 * Get NVML version string
 */
std::string getNVMLVersion();

/**
 * Check if MACA management is available (MetaX)
 */
bool isMACAMgmtAvailable();

/**
 * Get MACA version string
 */
std::string getMACAVersion();

} // namespace cluster
} // namespace tracesmith

