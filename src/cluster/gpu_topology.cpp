/**
 * TraceSmith GPU Topology Implementation
 * 
 * Uses NVML for GPU topology discovery on NVIDIA systems.
 * Falls back to CUDA runtime API if NVML is not available.
 */

#include "tracesmith/cluster/gpu_topology.hpp"

#include <algorithm>
#include <iostream>
#include <queue>
#include <sstream>
#include <cstring>

#ifdef TRACESMITH_HAS_NVML
#include <nvml.h>
#endif

#ifdef TRACESMITH_ENABLE_CUDA
#include <cuda_runtime.h>
#endif

#ifdef TRACESMITH_ENABLE_MACA
#include <mcr/maca.h>
#include <mcr/mc_runtime_api.h>
#endif

namespace tracesmith {
namespace cluster {

// ============================================================================
// NVML Utility Functions
// ============================================================================

#ifdef TRACESMITH_HAS_NVML

static bool g_nvml_initialized = false;

static bool initNVML() {
    if (g_nvml_initialized) return true;
    
    nvmlReturn_t result = nvmlInit_v2();
    if (result == NVML_SUCCESS) {
        g_nvml_initialized = true;
        return true;
    }
    return false;
}

static void shutdownNVML() {
    if (g_nvml_initialized) {
        nvmlShutdown();
        g_nvml_initialized = false;
    }
}

static GPULinkType nvmlLinkTypeToGPULinkType(nvmlGpuP2PStatus_t status, int nvlinkVersion) {
    if (status != NVML_P2P_STATUS_OK) {
        return GPULinkType::None;
    }
    
    // Determine NVLink version
    switch (nvlinkVersion) {
        case 1: return GPULinkType::NVLink1;
        case 2: return GPULinkType::NVLink2;
        case 3: return GPULinkType::NVLink3;
        case 4: return GPULinkType::NVLink4;
        default: return GPULinkType::PCIe;
    }
}

#endif // TRACESMITH_HAS_NVML

// ============================================================================
// Global Functions
// ============================================================================

bool isNVMLAvailable() {
#ifdef TRACESMITH_HAS_NVML
    return initNVML();
#else
    return false;
#endif
}

std::string getNVMLVersion() {
#ifdef TRACESMITH_HAS_NVML
    if (!initNVML()) return "N/A";
    
    char version[NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE];
    if (nvmlSystemGetNVMLVersion(version, sizeof(version)) == NVML_SUCCESS) {
        return std::string(version);
    }
#endif
    return "N/A";
}

// ============================================================================
// GPUTopology Implementation
// ============================================================================

GPUTopology::GPUTopology() = default;

GPUTopology::~GPUTopology() {
#ifdef TRACESMITH_HAS_NVML
    // NVML cleanup handled globally
#endif
}

bool GPUTopology::discover() {
    if (discovered_) return true;
    
    // Try MACA first (MetaX GPUs)
#ifdef TRACESMITH_ENABLE_MACA
    if (discoverMACA()) {
        buildLinkMatrix();
        discovered_ = true;
        return true;
    }
#endif
    
    // Try NVML (NVIDIA GPUs)
#ifdef TRACESMITH_HAS_NVML
    if (discoverNVML()) {
        buildLinkMatrix();
        discovered_ = true;
        return true;
    }
#endif
    
    // Fall back to CUDA
#ifdef TRACESMITH_ENABLE_CUDA
    if (discoverCUDA()) {
        buildLinkMatrix();
        discovered_ = true;
        return true;
    }
#endif
    
    return false;
}

#ifdef TRACESMITH_HAS_NVML

bool GPUTopology::discoverNVML() {
    if (!initNVML()) {
        return false;
    }
    
    // Get device count
    unsigned int deviceCount = 0;
    if (nvmlDeviceGetCount_v2(&deviceCount) != NVML_SUCCESS) {
        return false;
    }
    
    topology_.gpu_count = deviceCount;
    topology_.devices.clear();
    topology_.links.clear();
    topology_.has_nvswitch = false;
    
    // Discover each device
    for (unsigned int i = 0; i < deviceCount; ++i) {
        nvmlDevice_t device;
        if (nvmlDeviceGetHandleByIndex_v2(i, &device) != NVML_SUCCESS) {
            continue;
        }
        
        GPUDeviceTopology devInfo;
        devInfo.gpu_id = i;
        
        // Get device name
        char name[NVML_DEVICE_NAME_V2_BUFFER_SIZE];
        if (nvmlDeviceGetName(device, name, sizeof(name)) == NVML_SUCCESS) {
            devInfo.name = name;
        }
        
        // Get PCI info
        nvmlPciInfo_t pciInfo;
        if (nvmlDeviceGetPciInfo_v3(device, &pciInfo) == NVML_SUCCESS) {
            devInfo.pci_bus_id = pciInfo.busId;
        }
        
        // Get NUMA node (if available)
        // Note: NVML doesn't directly provide NUMA info, would need to parse from PCI
        devInfo.numa_node = 0;
        
        // Check NVLink capability
        devInfo.has_nvlink = false;
        devInfo.nvlink_count = 0;
        
        // Count NVLinks
        for (unsigned int link = 0; link < NVML_NVLINK_MAX_LINKS; ++link) {
            nvmlEnableState_t isActive;
            if (nvmlDeviceGetNvLinkState(device, link, &isActive) == NVML_SUCCESS) {
                if (isActive == NVML_FEATURE_ENABLED) {
                    devInfo.has_nvlink = true;
                    devInfo.nvlink_count++;
                }
            }
        }
        
        topology_.devices.push_back(devInfo);
    }
    
    // Discover links between GPUs
    for (unsigned int i = 0; i < deviceCount; ++i) {
        nvmlDevice_t deviceA;
        if (nvmlDeviceGetHandleByIndex_v2(i, &deviceA) != NVML_SUCCESS) continue;
        
        for (unsigned int j = i + 1; j < deviceCount; ++j) {
            nvmlDevice_t deviceB;
            if (nvmlDeviceGetHandleByIndex_v2(j, &deviceB) != NVML_SUCCESS) continue;
            
            GPULink link;
            link.gpu_a = i;
            link.gpu_b = j;
            link.bidirectional = true;
            link.measured_bandwidth = 0;
            
            // Check P2P capability
            nvmlGpuP2PStatus_t p2pStatus;
            nvmlGpuP2PCapsIndex_t capIndex = NVML_P2P_CAPS_INDEX_READ;
            
            if (nvmlDeviceGetP2PStatus(deviceA, deviceB, capIndex, &p2pStatus) == NVML_SUCCESS) {
                if (p2pStatus == NVML_P2P_STATUS_OK) {
                    // Check for NVLink
                    unsigned int nvlinkCount = 0;
                    int nvlinkVersion = 0;
                    
                    // Count active NVLinks between these devices
                    for (unsigned int linkIdx = 0; linkIdx < NVML_NVLINK_MAX_LINKS; ++linkIdx) {
                        nvmlPciInfo_t remotePci;
                        if (nvmlDeviceGetNvLinkRemotePciInfo_v2(deviceA, linkIdx, &remotePci) == NVML_SUCCESS) {
                            // Check if this link goes to deviceB
                            nvmlPciInfo_t deviceBPci;
                            if (nvmlDeviceGetPciInfo_v3(deviceB, &deviceBPci) == NVML_SUCCESS) {
                                if (strcmp(remotePci.busId, deviceBPci.busId) == 0) {
                                    nvlinkCount++;
                                    
                                    // Get NVLink version
                                    unsigned int version;
                                    if (nvmlDeviceGetNvLinkVersion(deviceA, linkIdx, &version) == NVML_SUCCESS) {
                                        nvlinkVersion = std::max(nvlinkVersion, (int)version);
                                    }
                                }
                            }
                        }
                    }
                    
                    if (nvlinkCount > 0) {
                        link.type = nvmlLinkTypeToGPULinkType(p2pStatus, nvlinkVersion);
                        link.link_count = nvlinkCount;
                        link.bandwidth_gbps = getLinkBandwidth(link.type) * nvlinkCount;
                    } else {
                        // PCIe connection
                        link.type = GPULinkType::PCIe;
                        link.link_count = 1;
                        link.bandwidth_gbps = getLinkBandwidth(GPULinkType::PCIe);
                    }
                    
                    topology_.links.push_back(link);
                }
            }
        }
    }
    
    // Check for NVSwitch
    unsigned int switchCount = 0;
    if (nvmlUnitGetCount(&switchCount) == NVML_SUCCESS && switchCount > 0) {
        topology_.has_nvswitch = true;
    }
    
    return true;
}

#else

bool GPUTopology::discoverNVML() {
    return false;
}

#endif // TRACESMITH_HAS_NVML

#ifdef TRACESMITH_ENABLE_CUDA

bool GPUTopology::discoverCUDA() {
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    if (err != cudaSuccess || deviceCount == 0) {
        return false;
    }
    
    topology_.gpu_count = deviceCount;
    topology_.devices.clear();
    topology_.links.clear();
    topology_.has_nvswitch = false;
    
    // Discover each device
    for (int i = 0; i < deviceCount; ++i) {
        cudaDeviceProp prop;
        if (cudaGetDeviceProperties(&prop, i) != cudaSuccess) continue;
        
        GPUDeviceTopology devInfo;
        devInfo.gpu_id = i;
        devInfo.name = prop.name;
        
        // Format PCI bus ID
        char pciBusId[16];
        snprintf(pciBusId, sizeof(pciBusId), "%04x:%02x:%02x.0",
                 prop.pciDomainID, prop.pciBusID, prop.pciDeviceID);
        devInfo.pci_bus_id = pciBusId;
        devInfo.numa_node = 0;
        devInfo.has_nvlink = false;
        devInfo.nvlink_count = 0;
        
        topology_.devices.push_back(devInfo);
    }
    
    // Check P2P access between devices
    for (int i = 0; i < deviceCount; ++i) {
        for (int j = i + 1; j < deviceCount; ++j) {
            int canAccessPeer = 0;
            cudaDeviceCanAccessPeer(&canAccessPeer, i, j);
            
            if (canAccessPeer) {
                GPULink link;
                link.gpu_a = i;
                link.gpu_b = j;
                link.type = GPULinkType::PCIe;  // CUDA API doesn't tell us link type
                link.link_count = 1;
                link.bandwidth_gbps = getLinkBandwidth(GPULinkType::PCIe);
                link.measured_bandwidth = 0;
                link.bidirectional = true;
                
                topology_.links.push_back(link);
            }
        }
    }
    
    return true;
}

#else

bool GPUTopology::discoverCUDA() {
    return false;
}

#endif // TRACESMITH_ENABLE_CUDA

void GPUTopology::buildLinkMatrix() {
    topology_.link_matrix.clear();
    
    for (const auto& link : topology_.links) {
        topology_.link_matrix[{link.gpu_a, link.gpu_b}] = link.type;
        topology_.link_matrix[{link.gpu_b, link.gpu_a}] = link.type;
    }
}

GPULinkType GPUTopology::getLinkType(uint32_t gpu_a, uint32_t gpu_b) const {
    if (gpu_a == gpu_b) return GPULinkType::None;
    
    auto it = topology_.link_matrix.find({gpu_a, gpu_b});
    if (it != topology_.link_matrix.end()) {
        return it->second;
    }
    return GPULinkType::None;
}

double GPUTopology::getBandwidth(uint32_t gpu_a, uint32_t gpu_b) const {
    if (gpu_a == gpu_b) return 0;
    
    for (const auto& link : topology_.links) {
        if ((link.gpu_a == gpu_a && link.gpu_b == gpu_b) ||
            (link.gpu_a == gpu_b && link.gpu_b == gpu_a)) {
            return link.bandwidth_gbps;
        }
    }
    return 0;
}

bool GPUTopology::canAccessPeer(uint32_t gpu_a, uint32_t gpu_b) const {
    return getLinkType(gpu_a, gpu_b) != GPULinkType::None;
}

uint32_t GPUTopology::getNVLinkCount(uint32_t gpu_a, uint32_t gpu_b) const {
    for (const auto& link : topology_.links) {
        if ((link.gpu_a == gpu_a && link.gpu_b == gpu_b) ||
            (link.gpu_a == gpu_b && link.gpu_b == gpu_a)) {
            GPULinkType type = link.type;
            if (type >= GPULinkType::NVLink1 && type <= GPULinkType::NVLink4) {
                return link.link_count;
            }
        }
    }
    return 0;
}

bool GPUTopology::isDirectlyConnected(uint32_t gpu_a, uint32_t gpu_b) const {
    return getLinkType(gpu_a, gpu_b) != GPULinkType::None;
}

std::vector<uint32_t> GPUTopology::getConnectedGPUs(uint32_t gpu_id) const {
    std::vector<uint32_t> connected;
    
    for (const auto& link : topology_.links) {
        if (link.gpu_a == gpu_id) {
            connected.push_back(link.gpu_b);
        } else if (link.gpu_b == gpu_id) {
            connected.push_back(link.gpu_a);
        }
    }
    
    return connected;
}

GPUDeviceTopology GPUTopology::getDeviceInfo(uint32_t gpu_id) const {
    for (const auto& dev : topology_.devices) {
        if (dev.gpu_id == gpu_id) {
            return dev;
        }
    }
    return {};
}

std::vector<uint32_t> GPUTopology::getOptimalPath(uint32_t src, uint32_t dst) const {
    if (src == dst) return {src};
    if (!discovered_) return {};
    
    // BFS for shortest path
    std::queue<std::vector<uint32_t>> paths;
    std::vector<bool> visited(topology_.gpu_count, false);
    
    paths.push({src});
    visited[src] = true;
    
    while (!paths.empty()) {
        auto path = paths.front();
        paths.pop();
        
        uint32_t current = path.back();
        
        if (current == dst) {
            return path;
        }
        
        for (uint32_t neighbor : getConnectedGPUs(current)) {
            if (!visited[neighbor]) {
                visited[neighbor] = true;
                auto newPath = path;
                newPath.push_back(neighbor);
                paths.push(newPath);
            }
        }
    }
    
    return {};  // No path found
}

double GPUTopology::estimateTransferTime(uint32_t src, uint32_t dst, size_t bytes) const {
    double bandwidth = getBandwidth(src, dst);
    if (bandwidth <= 0) return -1;
    
    // Convert GB/s to bytes/us
    double bytes_per_us = bandwidth * 1e9 / 1e6;
    return bytes / bytes_per_us;
}

std::string GPUTopology::toASCII() const {
    if (!discovered_) return "Topology not discovered\n";
    
    std::ostringstream ss;
    
    ss << "GPU Topology (" << topology_.gpu_count << " GPUs)\n";
    ss << std::string(40, '=') << "\n\n";
    
    // Print devices
    for (const auto& dev : topology_.devices) {
        ss << "GPU " << dev.gpu_id << ": " << dev.name << "\n";
        ss << "  PCI: " << dev.pci_bus_id << "\n";
        if (dev.has_nvlink) {
            ss << "  NVLinks: " << dev.nvlink_count << "\n";
        }
        ss << "\n";
    }
    
    // Print connectivity matrix
    ss << "Connectivity Matrix:\n";
    ss << "     ";
    for (uint32_t i = 0; i < topology_.gpu_count; ++i) {
        ss << "GPU" << i << " ";
    }
    ss << "\n";
    
    for (uint32_t i = 0; i < topology_.gpu_count; ++i) {
        ss << "GPU" << i << " ";
        for (uint32_t j = 0; j < topology_.gpu_count; ++j) {
            if (i == j) {
                ss << "  -  ";
            } else {
                auto type = getLinkType(i, j);
                switch (type) {
                    case GPULinkType::NVLink1:
                    case GPULinkType::NVLink2:
                    case GPULinkType::NVLink3:
                    case GPULinkType::NVLink4:
                        ss << " NV" << getNVLinkCount(i, j) << " ";
                        break;
                    case GPULinkType::PCIe:
                        ss << " PCIe";
                        break;
                    case GPULinkType::NVSwitch:
                        ss << " NVS ";
                        break;
                    default:
                        ss << "  X  ";
                }
            }
        }
        ss << "\n";
    }
    
    if (topology_.has_nvswitch) {
        ss << "\n[NVSwitch detected]\n";
    }
    
    return ss.str();
}

std::string GPUTopology::toGraphviz() const {
    std::ostringstream ss;
    
    ss << "graph GPU_Topology {\n";
    ss << "  rankdir=LR;\n";
    ss << "  node [shape=box];\n\n";
    
    // Nodes
    for (const auto& dev : topology_.devices) {
        ss << "  GPU" << dev.gpu_id << " [label=\"GPU " << dev.gpu_id << "\\n" 
           << dev.name << "\"];\n";
    }
    ss << "\n";
    
    // Edges
    for (const auto& link : topology_.links) {
        ss << "  GPU" << link.gpu_a << " -- GPU" << link.gpu_b;
        ss << " [label=\"" << linkTypeToString(link.type);
        if (link.link_count > 1) {
            ss << " x" << link.link_count;
        }
        ss << "\\n" << link.bandwidth_gbps << " GB/s\"];\n";
    }
    
    ss << "}\n";
    
    return ss.str();
}

std::string GPUTopology::toJSON() const {
    std::ostringstream ss;
    
    ss << "{\n";
    ss << "  \"gpu_count\": " << topology_.gpu_count << ",\n";
    ss << "  \"has_nvswitch\": " << (topology_.has_nvswitch ? "true" : "false") << ",\n";
    
    // Devices
    ss << "  \"devices\": [\n";
    for (size_t i = 0; i < topology_.devices.size(); ++i) {
        const auto& dev = topology_.devices[i];
        ss << "    {\n";
        ss << "      \"gpu_id\": " << dev.gpu_id << ",\n";
        ss << "      \"name\": \"" << dev.name << "\",\n";
        ss << "      \"pci_bus_id\": \"" << dev.pci_bus_id << "\",\n";
        ss << "      \"has_nvlink\": " << (dev.has_nvlink ? "true" : "false") << ",\n";
        ss << "      \"nvlink_count\": " << dev.nvlink_count << "\n";
        ss << "    }" << (i < topology_.devices.size() - 1 ? "," : "") << "\n";
    }
    ss << "  ],\n";
    
    // Links
    ss << "  \"links\": [\n";
    for (size_t i = 0; i < topology_.links.size(); ++i) {
        const auto& link = topology_.links[i];
        ss << "    {\n";
        ss << "      \"gpu_a\": " << link.gpu_a << ",\n";
        ss << "      \"gpu_b\": " << link.gpu_b << ",\n";
        ss << "      \"type\": \"" << linkTypeToString(link.type) << "\",\n";
        ss << "      \"link_count\": " << link.link_count << ",\n";
        ss << "      \"bandwidth_gbps\": " << link.bandwidth_gbps << "\n";
        ss << "    }" << (i < topology_.links.size() - 1 ? "," : "") << "\n";
    }
    ss << "  ]\n";
    
    ss << "}\n";
    
    return ss.str();
}

void GPUTopology::printSummary() const {
    std::cout << toASCII();
}

// ============================================================================
// MACA Discovery Implementation (MetaX GPUs)
// ============================================================================

#ifdef TRACESMITH_ENABLE_MACA

bool GPUTopology::discoverMACA() {
    // Initialize MACA runtime
    mcError_t err = mcInit(0);
    if (err != mcSuccess) {
        return false;
    }
    
    // Get device count
    int deviceCount = 0;
    err = mcGetDeviceCount(&deviceCount);
    if (err != mcSuccess || deviceCount == 0) {
        return false;
    }
    
    topology_.gpu_count = deviceCount;
    topology_.devices.clear();
    topology_.links.clear();
    topology_.has_nvswitch = false;  // MetaX may have MXSwitch
    
    // Discover each device
    for (int i = 0; i < deviceCount; ++i) {
        GPUDeviceTopology devInfo;
        devInfo.gpu_id = i;
        devInfo.vendor = GPUVendor::MetaX;
        devInfo.has_nvlink = false;
        devInfo.nvlink_count = 0;
        devInfo.has_mxlink = false;
        devInfo.mxlink_count = 0;
        
        // Get device properties
        mcDeviceProp_t prop;
        if (mcGetDeviceProperties(&prop, i) == mcSuccess) {
            devInfo.name = prop.name;
            devInfo.total_memory = prop.totalGlobalMem;
            devInfo.compute_major = prop.major;
            devInfo.compute_minor = prop.minor;
            
            // Get PCI bus ID if available
            char pciBusId[16];
            if (mcDeviceGetPCIBusId(pciBusId, sizeof(pciBusId), i) == mcSuccess) {
                devInfo.pci_bus_id = pciBusId;
            }
        } else {
            devInfo.name = "MetaX GPU";
        }
        
        devInfo.numa_node = 0;  // Default
        
        topology_.devices.push_back(devInfo);
    }
    
    // Discover peer access topology (PCIe connections)
    for (int i = 0; i < deviceCount; ++i) {
        for (int j = i + 1; j < deviceCount; ++j) {
            int canAccessPeer = 0;
            mcDeviceCanAccessPeer(&canAccessPeer, i, j);
            
            if (canAccessPeer) {
                GPULink link;
                link.gpu_a = i;
                link.gpu_b = j;
                link.type = GPULinkType::PCIe;  // Default to PCIe for now
                link.link_count = 1;
                link.bandwidth_gbps = getLinkBandwidth(GPULinkType::PCIe);
                link.measured_bandwidth = 0.0;
                link.bidirectional = true;
                
                // TODO: Check for MXLink if MetaX provides API
                // For now, all peer-accessible devices are considered PCIe
                
                topology_.links.push_back(link);
            }
        }
    }
    
    return true;
}

#else

bool GPUTopology::discoverMACA() {
    return false;
}

#endif // TRACESMITH_ENABLE_MACA

// ============================================================================
// MACA Global Functions
// ============================================================================

bool isMACAMgmtAvailable() {
#ifdef TRACESMITH_ENABLE_MACA
    mcError_t err = mcInit(0);
    if (err != mcSuccess) {
        return false;
    }
    int count = 0;
    err = mcGetDeviceCount(&count);
    return (err == mcSuccess && count > 0);
#else
    return false;
#endif
}

std::string getMACAVersion() {
#ifdef TRACESMITH_ENABLE_MACA
    int version = 0;
    if (mcDriverGetVersion(&version) == mcSuccess) {
        int major = version / 1000;
        int minor = (version % 1000) / 10;
        return std::to_string(major) + "." + std::to_string(minor);
    }
#endif
    return "N/A";
}

} // namespace cluster
} // namespace tracesmith

