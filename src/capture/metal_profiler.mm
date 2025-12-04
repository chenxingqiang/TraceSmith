#include "tracesmith/capture/metal_profiler.hpp"
#include <iostream>

#ifdef TRACESMITH_ENABLE_METAL

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

namespace tracesmith {

//==============================================================================
// Helper Functions
//==============================================================================

bool isMetalAvailable() {
    @autoreleasepool {
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
        bool available = devices.count > 0;
        return available;
    }
}

int getMetalDeviceCount() {
    @autoreleasepool {
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
        return (int)devices.count;
    }
}

std::string getMetalVersion() {
    @autoreleasepool {
        if (@available(macOS 13.0, iOS 16.0, *)) {
            return "Metal 3";
        } else if (@available(macOS 12.0, iOS 15.0, *)) {
            return "Metal 2.4";
        } else if (@available(macOS 11.0, iOS 14.0, *)) {
            return "Metal 2.3";
        } else if (@available(macOS 10.15, iOS 13.0, *)) {
            return "Metal 2.2";
        }
        return "Metal 2.x";
    }
}

bool supportsMetalCapture() {
    @autoreleasepool {
        if (@available(macOS 10.15, iOS 13.0, *)) {
            MTLCaptureManager* captureManager = [MTLCaptureManager sharedCaptureManager];
            return [captureManager supportsDestination:MTLCaptureDestinationGPUTraceDocument];
        }
        return false;
    }
}

//==============================================================================
// MetalProfiler Implementation
//==============================================================================

MetalProfiler::MetalProfiler()
    : device_(nullptr)
    , command_queue_(nullptr)
    , capture_manager_(nullptr)
    , capture_descriptor_(nullptr)
    , capture_counters_(false)
    , initialized_(false)
    , capturing_(false)
    , events_captured_(0)
    , events_dropped_(0)
    , correlation_counter_(0) {
}

MetalProfiler::~MetalProfiler() {
    finalize();
}

bool MetalProfiler::isAvailable() const {
    return isMetalAvailable();
}

bool MetalProfiler::initialize(const ProfilerConfig& config) {
    @autoreleasepool {
        if (initialized_) {
            return true;
        }
        
        config_ = config;
        
        // Get default Metal device
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "Metal: No Metal-capable device found" << std::endl;
            return false;
        }
        
        device_ = (__bridge_retained void*)device;
        
        // Create command queue
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) {
            std::cerr << "Metal: Failed to create command queue" << std::endl;
            CFRelease(device_);
            device_ = nullptr;
            return false;
        }
        
        command_queue_ = (__bridge_retained void*)queue;
        
        // Setup capture manager
        if (@available(macOS 10.15, iOS 13.0, *)) {
            capture_manager_ = (__bridge void*)[MTLCaptureManager sharedCaptureManager];
        }
        
        initialized_ = true;
        
        std::cout << "Metal Profiler initialized on device: " 
                  << [device.name UTF8String] << std::endl;
        
        return true;
    }
}

void MetalProfiler::finalize() {
    @autoreleasepool {
        if (!initialized_) {
            return;
        }
        
        if (capturing_) {
            stopCapture();
        }
        
        cleanupCaptureManager();
        
        if (command_queue_) {
            CFRelease(command_queue_);
            command_queue_ = nullptr;
        }
        
        if (device_) {
            CFRelease(device_);
            device_ = nullptr;
        }
        
        initialized_ = false;
    }
}

bool MetalProfiler::startCapture() {
    @autoreleasepool {
        if (!initialized_) {
            std::cerr << "Metal: Profiler not initialized" << std::endl;
            return false;
        }
        
        if (capturing_) {
            return true;
        }
        
        // Clear previous events
        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            events_.clear();
        }
        events_captured_ = 0;
        events_dropped_ = 0;
        
        // Start Metal capture
        if (@available(macOS 10.15, iOS 13.0, *)) {
            setupCaptureManager();
        }
        
        capturing_ = true;
        
        std::cout << "Metal capture started" << std::endl;
        return true;
    }
}

bool MetalProfiler::stopCapture() {
    @autoreleasepool {
        if (!capturing_) {
            return true;
        }
        
        capturing_ = false;
        
        // Stop Metal capture
        if (@available(macOS 10.15, iOS 13.0, *)) {
            MTLCaptureManager* captureManager = (__bridge MTLCaptureManager*)capture_manager_;
            if (captureManager && captureManager.isCapturing) {
                [captureManager stopCapture];
            }
        }
        
        cleanupCaptureManager();
        
        std::cout << "Metal capture stopped" << std::endl;
        return true;
    }
}

size_t MetalProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
    std::lock_guard<std::mutex> lock(events_mutex_);
    
    size_t count = (max_count > 0 && max_count < events_.size()) 
                   ? max_count 
                   : events_.size();
    
    events.insert(events.end(), 
                  events_.begin(), 
                  events_.begin() + count);
    
    events_.erase(events_.begin(), events_.begin() + count);
    
    return count;
}

std::vector<DeviceInfo> MetalProfiler::getDeviceInfo() const {
    @autoreleasepool {
        std::vector<DeviceInfo> devices;
        
        NSArray<id<MTLDevice>>* metalDevices = MTLCopyAllDevices();
        
        for (id<MTLDevice> device in metalDevices) {
            DeviceInfo info;
            info.device_id = devices.size();
            info.name = [device.name UTF8String];
            info.vendor = "Apple";
            
            // Memory info
            if (@available(macOS 10.15, *)) {
                info.total_memory = device.recommendedMaxWorkingSetSize;
            }
            
            devices.push_back(info);
        }
        
        return devices;
    }
}

void MetalProfiler::setEventCallback(EventCallback callback) {
    callback_ = std::move(callback);
}

void* MetalProfiler::getDevice() const {
    return device_;
}

void MetalProfiler::setCaptureCounters(bool enable) {
    capture_counters_ = enable;
}

std::string MetalProfiler::getFeatureSet() const {
    @autoreleasepool {
        if (!device_) {
            return "Unknown";
        }
        
        id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
        
        if (@available(macOS 11.0, iOS 14.0, *)) {
            if ([device supportsFamily:MTLGPUFamilyApple7]) {
                return "Apple GPU Family 7 (M1 Pro/Max/Ultra, M2)";
            } else if ([device supportsFamily:MTLGPUFamilyApple6]) {
                return "Apple GPU Family 6 (A14, M1)";
            } else if ([device supportsFamily:MTLGPUFamilyMac2]) {
                return "Mac GPU Family 2 (Intel, AMD)";
            }
        }
        
        return "Metal 2.x Compatible";
    }
}

void MetalProfiler::trackCommandBuffer(void* commandBuffer) {
    @autoreleasepool {
        if (!capturing_) {
            return;
        }
        
        id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)commandBuffer;
        
        CommandBufferInfo info;
        info.correlation_id = correlation_counter_.fetch_add(1);
        info.start_time = getCurrentTimestamp();
        info.label = cmdBuffer.label ? [cmdBuffer.label UTF8String] : "CommandBuffer";
        
        {
            std::lock_guard<std::mutex> lock(tracking_mutex_);
            tracked_buffers_[commandBuffer] = info;
        }
        
        // Add completion handler to capture timing
        MetalProfiler* __unsafe_unretained weakSelf = this;
        [cmdBuffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
            if (weakSelf) {
                weakSelf->processCommandBuffer((__bridge void*)buffer);
            }
        }];
    }
}

void MetalProfiler::processCommandBuffer(void* buffer) {
    @autoreleasepool {
        id<MTLCommandBuffer> cmdBuffer = (__bridge id<MTLCommandBuffer>)buffer;
        
        CommandBufferInfo info;
        {
            std::lock_guard<std::mutex> lock(tracking_mutex_);
            auto it = tracked_buffers_.find(buffer);
            if (it == tracked_buffers_.end()) {
                return;
            }
            info = it->second;
            tracked_buffers_.erase(it);
        }
        
        Timestamp end_time = getCurrentTimestamp();
        
        // Get GPU timing if available
        if (@available(macOS 10.15, *)) {
            if (cmdBuffer.GPUStartTime > 0 && cmdBuffer.GPUEndTime > 0) {
                // GPU timestamps in seconds, convert to nanoseconds
                uint64_t gpu_start_ns = (uint64_t)(cmdBuffer.GPUStartTime * 1e9);
                uint64_t gpu_end_ns = (uint64_t)(cmdBuffer.GPUEndTime * 1e9);
                
                TraceEvent event;
                event.type = EventType::KernelLaunch;
                event.name = info.label;
                event.timestamp = gpu_start_ns;
                event.correlation_id = info.correlation_id;
                event.device_id = 0;
                event.stream_id = 0;
                
                // Duration
                event.duration = gpu_end_ns - gpu_start_ns;
                
                // Add event
                events_captured_++;
                if (callback_) {
                    callback_(event);
                }
                
                std::lock_guard<std::mutex> lock(events_mutex_);
                if (config_.buffer_size > 0 && events_.size() >= config_.buffer_size) {
                    events_dropped_++;
                } else {
                    events_.push_back(std::move(event));
                }
            }
        }
    }
}

void MetalProfiler::setupCaptureManager() {
    @autoreleasepool {
        if (@available(macOS 10.15, iOS 13.0, *)) {
            MTLCaptureManager* captureManager = (__bridge MTLCaptureManager*)capture_manager_;
            if (!captureManager) {
                return;
            }
            
            id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
            
            MTLCaptureDescriptor* descriptor = [[MTLCaptureDescriptor alloc] init];
            descriptor.captureObject = device;
            
            if (capture_counters_) {
                // Enable counter sampling if supported
                if ([captureManager supportsDestination:MTLCaptureDestinationGPUTraceDocument]) {
                    descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
                }
            }
            
            NSError* error = nil;
            BOOL success = [captureManager startCaptureWithDescriptor:descriptor error:&error];
            
            if (!success) {
                if (error) {
                    std::cerr << "Metal: Failed to start capture: " 
                              << [[error localizedDescription] UTF8String] << std::endl;
                }
            }
            
            capture_descriptor_ = (__bridge_retained void*)descriptor;
        }
    }
}

void MetalProfiler::cleanupCaptureManager() {
    @autoreleasepool {
        if (capture_descriptor_) {
            CFRelease(capture_descriptor_);
            capture_descriptor_ = nullptr;
        }
    }
}

bool MetalProfiler::supportsGPUCapture() const {
    @autoreleasepool {
        if (@available(macOS 10.15, iOS 13.0, *)) {
            MTLCaptureManager* captureManager = (__bridge MTLCaptureManager*)capture_manager_;
            if (captureManager) {
                return [captureManager supportsDestination:MTLCaptureDestinationGPUTraceDocument];
            }
        }
        return false;
    }
}

} // namespace tracesmith

#else // !TRACESMITH_ENABLE_METAL

namespace tracesmith {

// Stub implementations when Metal is not available

MetalProfiler::MetalProfiler()
    : initialized_(false)
    , capturing_(false)
    , events_captured_(0)
    , events_dropped_(0)
    , correlation_counter_(0) {
}

MetalProfiler::~MetalProfiler() {
}

bool MetalProfiler::isAvailable() const {
    return false;
}

bool MetalProfiler::initialize(const ProfilerConfig& config) {
    std::cerr << "TraceSmith was compiled without Metal support" << std::endl;
    return false;
}

void MetalProfiler::finalize() {
}

bool MetalProfiler::startCapture() {
    return false;
}

bool MetalProfiler::stopCapture() {
    return false;
}

size_t MetalProfiler::getEvents(std::vector<TraceEvent>& events, size_t max_count) {
    return 0;
}

std::vector<DeviceInfo> MetalProfiler::getDeviceInfo() const {
    return {};
}

void MetalProfiler::setEventCallback(EventCallback callback) {
}

bool isMetalAvailable() {
    return false;
}

int getMetalDeviceCount() {
    return 0;
}

std::string getMetalVersion() {
    return "Not available";
}

bool supportsMetalCapture() {
    return false;
}

} // namespace tracesmith

#endif // TRACESMITH_ENABLE_METAL
