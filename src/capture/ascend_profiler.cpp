/**
 * TraceSmith - Huawei Ascend NPU Profiler Implementation
 */

#include <tracesmith/capture/ascend_profiler.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <chrono>

#ifdef TRACESMITH_ENABLE_ASCEND
#include <acl/acl.h>
#include <acl/acl_prof.h>
#endif

namespace tracesmith {

struct AscendProfiler::Impl {
    bool acl_initialized = false;
    bool profiler_initialized = false;
    ProfilerConfig profiler_config;
};

AscendProfiler::AscendProfiler() : impl_(std::make_unique<Impl>()) {
#ifdef TRACESMITH_ENABLE_ASCEND
    // Initialize ACL if not already done
    aclError ret = aclInit(nullptr);
    if (ret == ACL_SUCCESS || ret == ACL_ERROR_REPEAT_INITIALIZE) {
        impl_->acl_initialized = true;
    }
#endif
}

AscendProfiler::~AscendProfiler() {
    if (is_running_) {
        stopCapture();
    }
    finalize();
}

void AscendProfiler::configure(const AscendProfilerConfig& config) {
    config_ = config;
}

bool AscendProfiler::isAvailable() const {
    return is_available();
}

bool AscendProfiler::initialize(const ProfilerConfig& config) {
    impl_->profiler_config = config;
    
#ifdef TRACESMITH_ENABLE_ASCEND
    // ACL should already be initialized in constructor
    if (!impl_->acl_initialized) {
        aclError ret = aclInit(nullptr);
        if (ret != ACL_SUCCESS && ret != ACL_ERROR_REPEAT_INITIALIZE) {
            std::cerr << "AscendProfiler: Failed to initialize ACL, error=" << ret << std::endl;
            return false;
        }
        impl_->acl_initialized = true;
    }
    return true;
#else
    return false;
#endif
}

void AscendProfiler::finalize() {
#ifdef TRACESMITH_ENABLE_ASCEND
    if (prof_config_) {
        aclprofDestroyConfig(prof_config_);
        prof_config_ = nullptr;
    }
    
    if (impl_->profiler_initialized) {
        aclprofFinalize();
        impl_->profiler_initialized = false;
    }
#endif
}

#ifdef TRACESMITH_ENABLE_ASCEND
uint64_t AscendProfiler::build_data_type_config() const {
    uint64_t config = 0;
    
    if (config_.capture_acl_api) {
        config |= ACL_PROF_ACL_API;
    }
    if (config_.capture_task_time) {
        config |= ACL_PROF_TASK_TIME;
    }
    if (config_.capture_aicore_metrics) {
        config |= ACL_PROF_AICORE_METRICS;
    }
    if (config_.capture_aicpu) {
        config |= ACL_PROF_AICPU;
    }
    if (config_.capture_hccl_trace) {
        config |= ACL_PROF_HCCL_TRACE;
    }
    if (config_.capture_memory) {
        config |= ACL_PROF_TASK_MEMORY;
    }
    
    return config;
}
#endif

bool AscendProfiler::startCapture() {
#ifdef TRACESMITH_ENABLE_ASCEND
    if (is_running_) {
        return false;
    }
    
    // Create output directory
    std::filesystem::create_directories(config_.output_dir);
    
    // Initialize profiler
    aclError ret = aclprofInit(config_.output_dir.c_str(), config_.output_dir.size());
    if (ret != ACL_SUCCESS && ret != ACL_ERROR_REPEAT_INITIALIZE) {
        std::cerr << "AscendProfiler: Failed to initialize profiler, error=" << ret << std::endl;
        return false;
    }
    impl_->profiler_initialized = true;
    
    // Set storage limit if specified
    if (config_.storage_limit_mb > 0) {
        std::string limit_str = std::to_string(config_.storage_limit_mb);
        aclprofSetConfig(ACL_PROF_STORAGE_LIMIT, limit_str.c_str(), limit_str.size());
    }
    
    // Build device list
    std::vector<uint32_t> devices = config_.device_ids;
    if (devices.empty()) {
        uint32_t count = get_device_count();
        for (uint32_t i = 0; i < count; ++i) {
            devices.push_back(i);
        }
    }
    
    if (devices.empty()) {
        std::cerr << "AscendProfiler: No devices available" << std::endl;
        return false;
    }
    
    // Create profiler config
    uint64_t data_type_config = build_data_type_config();
    aclprofAicoreMetrics metrics = static_cast<aclprofAicoreMetrics>(
        static_cast<int>(config_.aicore_metrics));
    
    prof_config_ = aclprofCreateConfig(
        devices.data(),
        static_cast<uint32_t>(devices.size()),
        metrics,
        nullptr,  // aicore events (not supported yet)
        data_type_config
    );
    
    if (!prof_config_) {
        std::cerr << "AscendProfiler: Failed to create profiler config" << std::endl;
        return false;
    }
    
    // Start profiling
    ret = aclprofStart(prof_config_);
    if (ret != ACL_SUCCESS) {
        std::cerr << "AscendProfiler: Failed to start profiling, error=" << ret << std::endl;
        aclprofDestroyConfig(prof_config_);
        prof_config_ = nullptr;
        return false;
    }
    
    is_running_ = true;
    return true;
#else
    std::cerr << "AscendProfiler: CANN/Ascend support not enabled at compile time" << std::endl;
    return false;
#endif
}

bool AscendProfiler::stopCapture() {
#ifdef TRACESMITH_ENABLE_ASCEND
    if (!is_running_) {
        return false;
    }
    
    aclError ret = aclprofStop(prof_config_);
    if (ret != ACL_SUCCESS) {
        std::cerr << "AscendProfiler: Failed to stop profiling, error=" << ret << std::endl;
    }
    
    is_running_ = false;
    
    // Parse the profiling output
    parse_profiling_output(config_.output_dir);
    
    return ret == ACL_SUCCESS;
#else
    return false;
#endif
}

bool AscendProfiler::parse_profiling_output(const std::string& output_dir) {
    // Look for profiling output files
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(output_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                
                // Parse summary files
                if (filename.find("summary") != std::string::npos && 
                    filename.find(".csv") != std::string::npos) {
#ifdef TRACESMITH_ENABLE_ASCEND
                    parse_summary_file(entry.path().string());
#endif
                }
                
                // Parse timeline files
                if (filename.find("timeline") != std::string::npos ||
                    filename.find("trace") != std::string::npos) {
#ifdef TRACESMITH_ENABLE_ASCEND
                    parse_timeline_file(entry.path().string());
#endif
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "AscendProfiler: Error parsing output: " << e.what() << std::endl;
        return false;
    }
    
    return true;
}

#ifdef TRACESMITH_ENABLE_ASCEND
void AscendProfiler::parse_summary_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return;
    }
    
    std::string line;
    bool header_parsed = false;
    std::vector<std::string> headers;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::vector<std::string> fields;
        std::string field;
        
        while (std::getline(ss, field, ',')) {
            fields.push_back(field);
        }
        
        if (!header_parsed) {
            headers = fields;
            header_parsed = true;
            continue;
        }
        
        // Parse data row into TraceEvent
        if (fields.size() >= 4) {
            TraceEvent event;
            event.type = EventType::KernelLaunch;
            
            // Try to extract common fields
            for (size_t i = 0; i < headers.size() && i < fields.size(); ++i) {
                if (headers[i] == "Op Name" || headers[i] == "op_name") {
                    event.name = fields[i];
                } else if (headers[i] == "Task Duration(us)" || headers[i] == "duration") {
                    try {
                        double duration_us = std::stod(fields[i]);
                        event.duration = static_cast<uint64_t>(duration_us * 1000); // to ns
                        stats_.total_kernel_time_ms += duration_us / 1000.0;
                    } catch (...) {}
                } else if (headers[i] == "Start Time(us)" || headers[i] == "start_time") {
                    try {
                        double start_us = std::stod(fields[i]);
                        event.timestamp = static_cast<uint64_t>(start_us * 1000); // to ns
                    } catch (...) {}
                }
            }
            
            if (!event.name.empty()) {
                events_.push_back(event);
                stats_.kernel_count++;
                stats_.total_events++;
            }
        }
    }
}

void AscendProfiler::parse_timeline_file(const std::string& filepath) {
    // Timeline files are typically in JSON or custom format
    // Parse based on file extension
    if (filepath.find(".json") != std::string::npos) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return;
        }
        
        // Simple JSON parsing for timeline events
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        // Look for event patterns in JSON
        // This is a simplified parser - production code should use a JSON library
        size_t pos = 0;
        while ((pos = content.find("\"name\":", pos)) != std::string::npos) {
            TraceEvent event;
            
            // Extract name
            size_t start = content.find("\"", pos + 7) + 1;
            size_t end = content.find("\"", start);
            if (start != std::string::npos && end != std::string::npos) {
                event.name = content.substr(start, end - start);
            }
            
            // Extract timestamp
            size_t ts_pos = content.find("\"ts\":", pos);
            if (ts_pos != std::string::npos && ts_pos < pos + 500) {
                size_t ts_start = ts_pos + 5;
                size_t ts_end = content.find_first_of(",}", ts_start);
                try {
                    event.timestamp = std::stoull(content.substr(ts_start, ts_end - ts_start)) * 1000;
                } catch (...) {}
            }
            
            // Extract duration
            size_t dur_pos = content.find("\"dur\":", pos);
            if (dur_pos != std::string::npos && dur_pos < pos + 500) {
                size_t dur_start = dur_pos + 6;
                size_t dur_end = content.find_first_of(",}", dur_start);
                try {
                    event.duration = std::stoull(content.substr(dur_start, dur_end - dur_start)) * 1000;
                } catch (...) {}
            }
            
            if (!event.name.empty()) {
                event.type = EventType::KernelLaunch;
                events_.push_back(event);
                stats_.total_events++;
            }
            
            pos = end;
        }
    }
}
#endif

size_t AscendProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
    size_t count = max_count > 0 ? std::min(max_count, events_.size()) : events_.size();
    
    events.insert(events.end(), events_.begin(), events_.begin() + count);
    events_.erase(events_.begin(), events_.begin() + count);
    
    return count;
}

std::vector<DeviceInfo> AscendProfiler::getDeviceInfo() const {
    std::vector<DeviceInfo> devices;
    
    auto ascend_devices = get_all_device_info();
    for (const auto& dev : ascend_devices) {
        DeviceInfo info;
        info.device_id = dev.device_id;
        info.name = dev.name;
        info.vendor = "Huawei";
        info.total_memory = dev.total_memory;
        info.compute_major = 1;  // Ascend doesn't use SM versioning
        info.compute_minor = 0;
        info.multiprocessor_count = dev.ai_core_count;
        info.clock_rate = 0;  // Not exposed via ACL
        devices.push_back(info);
    }
    
    return devices;
}

AscendProfiler::Statistics AscendProfiler::get_statistics() const {
    return stats_;
}

bool AscendProfiler::is_available() {
#ifdef TRACESMITH_ENABLE_ASCEND
    // Try to initialize ACL
    aclError ret = aclInit(nullptr);
    if (ret == ACL_SUCCESS) {
        // Don't finalize, just check
        return true;
    } else if (ret == ACL_ERROR_REPEAT_INITIALIZE) {
        return true;
    }
    return false;
#else
    // Check for CANN installation
    return std::filesystem::exists("/usr/local/Ascend/ascend-toolkit");
#endif
}

std::string AscendProfiler::get_cann_version() {
    // Try to read version from config file
    std::string version_file = "/usr/local/Ascend/ascend-toolkit/latest/version.cfg";
    std::ifstream file(version_file);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("runtime_running_version") != std::string::npos) {
                size_t pos = line.find("=");
                if (pos != std::string::npos) {
                    return line.substr(pos + 1);
                }
            }
        }
    }
    return "unknown";
}

uint32_t AscendProfiler::get_device_count() {
#ifdef TRACESMITH_ENABLE_ASCEND
    uint32_t dev_count = 0;
    aclError ret = aclrtGetDeviceCount(&dev_count);
    if (ret == ACL_SUCCESS) {
        return dev_count;
    }
#endif
    
    // Fallback: check for device files
    uint32_t fallback_count = 0;
    for (int i = 0; i < 16; ++i) {
        std::string dev_path = "/dev/davinci" + std::to_string(i);
        if (std::filesystem::exists(dev_path)) {
            fallback_count++;
        }
    }
    return fallback_count;
}

AscendDeviceInfo AscendProfiler::get_device_info(uint32_t device_id) {
    AscendDeviceInfo info;
    info.device_id = device_id;
    
#ifdef TRACESMITH_ENABLE_ASCEND
    aclError ret = aclrtSetDevice(device_id);
    if (ret != ACL_SUCCESS) {
        return info;
    }
    
    // Get memory info
    size_t free_mem = 0, total_mem = 0;
    ret = aclrtGetMemInfo(ACL_HBM_MEM, &free_mem, &total_mem);
    if (ret == ACL_SUCCESS) {
        info.free_memory = free_mem;
        info.total_memory = total_mem;
    }
    
    // Get device name (through soc info)
    const char* soc_name = aclrtGetSocName();
    if (soc_name && strlen(soc_name) > 0) {
        info.chip_name = soc_name;
        info.name = std::string("Huawei ") + soc_name;
    } else {
        info.name = "Huawei Ascend NPU";
        info.chip_name = "Ascend";
    }
#else
    info.name = "Huawei Ascend NPU " + std::to_string(device_id);
    info.chip_name = "Ascend";
#endif
    
    info.cann_version = get_cann_version();
    
    return info;
}

std::vector<AscendDeviceInfo> AscendProfiler::get_all_device_info() {
    std::vector<AscendDeviceInfo> devices;
    uint32_t count = get_device_count();
    
    for (uint32_t i = 0; i < count; ++i) {
        devices.push_back(get_device_info(i));
    }
    
    return devices;
}

// Global functions
bool is_ascend_available() {
    return AscendProfiler::is_available();
}

std::string get_cann_version() {
    return AscendProfiler::get_cann_version();
}

uint32_t get_ascend_device_count() {
    return AscendProfiler::get_device_count();
}

} // namespace tracesmith
