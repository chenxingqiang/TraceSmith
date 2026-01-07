/**
 * @file gpu_debug_engine.cpp
 * @brief Implementation of GPU Debug Engine
 */

#include "tracesmith/gdb/gpu_debug_engine.hpp"
#include "tracesmith/format/sbt_format.hpp"
#include <fnmatch.h>
#include <algorithm>

namespace tracesmith {
namespace gdb {

// ============================================================
// Construction/Destruction
// ============================================================

GPUDebugEngine::GPUDebugEngine(const GPUDebugConfig& config)
    : config_(config)
    , memory_profiler_(std::make_unique<MemoryProfiler>())
    , state_machine_(std::make_unique<GPUStateMachine>())
    , replay_engine_(std::make_unique<ReplayEngine>())
{
}

GPUDebugEngine::~GPUDebugEngine() {
    finalize();
}

// ============================================================
// Initialization
// ============================================================

bool GPUDebugEngine::initialize(pid_t target_pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return false;
    }
    
    target_pid_ = target_pid;
    
    // Detect platform and create profiler
    PlatformType platform = detectPlatform();
    if (platform == PlatformType::Unknown) {
        // No GPU available, but we can still work
        initialized_ = true;
        return true;
    }
    
    profiler_ = createProfiler(platform);
    if (!profiler_) {
        return false;
    }
    
    // Configure profiler
    ProfilerConfig prof_config;
    prof_config.capture_callstacks = config_.capture_callstacks;
    prof_config.callstack_depth = config_.callstack_depth;
    
    if (!profiler_->initialize(prof_config)) {
        profiler_.reset();
        return false;
    }
    
    // Set up event callback
    profiler_->setEventCallback([this](const TraceEvent& event) {
        handleEvent(event);
    });
    
    initialized_ = true;
    return true;
}

void GPUDebugEngine::finalize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    if (capturing_) {
        stopCapture();
    }
    
    if (profiler_) {
        profiler_->finalize();
        profiler_.reset();
    }
    
    kernel_history_.clear();
    event_history_.clear();
    gpu_breakpoints_.clear();
    captured_events_.clear();
    
    initialized_ = false;
    target_pid_ = 0;
}

// ============================================================
// GPU State
// ============================================================

GPUStateSnapshot GPUDebugEngine::getGPUState() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    GPUStateSnapshot state;
    state.timestamp = getCurrentTimestamp();
    
    if (!initialized_ || !profiler_) {
        return state;
    }
    
    // Get device info
    state.devices = profiler_->getDeviceInfo();
    
    // Get memory state per device
    for (const auto& dev : state.devices) {
        GPUStateSnapshot::DeviceMemoryState mem_state;
        mem_state.device_id = dev.device_id;
        mem_state.total_memory = dev.total_memory;
        mem_state.used_memory = memory_profiler_->getCurrentUsage();
        mem_state.free_memory = dev.total_memory - mem_state.used_memory;
        mem_state.allocation_count = memory_profiler_->getLiveAllocationCount();
        state.memory_states.push_back(mem_state);
    }
    
    // Get stream states from state machine
    for (const auto& history : state_machine_->exportHistory()) {
        GPUStateSnapshot::StreamState ss;
        ss.device_id = history.device_id;
        ss.stream_id = history.stream_id;
        auto* stream_state = state_machine_->getStreamState(history.device_id, history.stream_id);
        if (stream_state) {
            ss.state = stream_state->currentState();
        }
        ss.pending_operations = 0;  // TODO: track pending ops
        state.stream_states.push_back(ss);
    }
    
    // Recent events
    size_t recent_count = std::min(event_history_.size(), size_t(10));
    for (size_t i = 0; i < recent_count; ++i) {
        state.recent_events.push_back(event_history_[event_history_.size() - 1 - i]);
    }
    
    return state;
}

std::vector<DeviceInfo> GPUDebugEngine::getDevices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!profiler_) {
        return {};
    }
    
    return profiler_->getDeviceInfo();
}

MemorySnapshot GPUDebugEngine::getMemoryUsage(int device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // device_id is currently ignored - memory profiler doesn't track per-device
    (void)device_id;
    
    return memory_profiler_->takeSnapshot();
}

std::vector<GPUStateSnapshot::StreamState> GPUDebugEngine::getStreamStates() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<GPUStateSnapshot::StreamState> result;
    
    auto streams = state_machine_->getAllStreams();
    for (const auto& [device_id, stream_id] : streams) {
        auto* ss = state_machine_->getStreamState(device_id, stream_id);
        if (ss) {
            GPUStateSnapshot::StreamState state;
            state.device_id = device_id;
            state.stream_id = stream_id;
            state.state = ss->currentState();
            state.pending_operations = 0;
            result.push_back(state);
        }
    }
    
    return result;
}

// ============================================================
// Kernel History
// ============================================================

std::vector<KernelCallInfo> GPUDebugEngine::getKernelHistory(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<KernelCallInfo> result;
    size_t actual_count = std::min(count, kernel_history_.size());
    
    // Return most recent first
    for (size_t i = 0; i < actual_count; ++i) {
        result.push_back(kernel_history_[kernel_history_.size() - 1 - i]);
    }
    
    return result;
}

std::vector<KernelCallInfo> GPUDebugEngine::getActiveKernels() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<KernelCallInfo> result;
    
    for (const auto& info : kernel_history_) {
        if (!info.isComplete()) {
            result.push_back(info);
        }
    }
    
    return result;
}

std::vector<KernelCallInfo> GPUDebugEngine::findKernels(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<KernelCallInfo> result;
    
    for (const auto& info : kernel_history_) {
        if (matchesPattern(info.kernel_name, pattern)) {
            result.push_back(info);
        }
    }
    
    return result;
}

// ============================================================
// GPU Breakpoints
// ============================================================

int GPUDebugEngine::setGPUBreakpoint(const GPUBreakpoint& bp) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    GPUBreakpoint new_bp = bp;
    new_bp.id = next_gpu_bp_id_++;
    new_bp.hit_count = 0;
    
    gpu_breakpoints_.push_back(new_bp);
    
    return new_bp.id;
}

bool GPUDebugEngine::removeGPUBreakpoint(int bp_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(gpu_breakpoints_.begin(), gpu_breakpoints_.end(),
        [bp_id](const GPUBreakpoint& bp) { return bp.id == bp_id; });
    
    if (it == gpu_breakpoints_.end()) {
        return false;
    }
    
    gpu_breakpoints_.erase(it);
    return true;
}

bool GPUDebugEngine::enableGPUBreakpoint(int bp_id, bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(gpu_breakpoints_.begin(), gpu_breakpoints_.end(),
        [bp_id](const GPUBreakpoint& bp) { return bp.id == bp_id; });
    
    if (it == gpu_breakpoints_.end()) {
        return false;
    }
    
    it->enabled = enable;
    return true;
}

std::vector<GPUBreakpoint> GPUDebugEngine::listGPUBreakpoints() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gpu_breakpoints_;
}

std::optional<GPUBreakpoint> GPUDebugEngine::checkBreakpoints(const TraceEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& bp : gpu_breakpoints_) {
        if (bp.matches(event)) {
            bp.hit_count++;
            return bp;
        }
    }
    
    return std::nullopt;
}

// ============================================================
// GPU Memory Access
// ============================================================

std::vector<uint8_t> GPUDebugEngine::readGPUMemory(int device, uint64_t addr, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return {};
    }
    
    // TODO: Implement GPU memory read via CUDA/MACA API
    // For now, return empty
    (void)device;
    (void)addr;
    (void)len;
    
    return {};
}

bool GPUDebugEngine::writeGPUMemory(int device, uint64_t addr, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    // TODO: Implement GPU memory write via CUDA/MACA API
    (void)device;
    (void)addr;
    (void)data;
    
    return false;
}

std::vector<MemoryAllocation> GPUDebugEngine::getMemoryAllocations(int device) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // device filter is currently ignored
    (void)device;
    
    return memory_profiler_->getLiveAllocations();
}

// ============================================================
// Trace Capture
// ============================================================

bool GPUDebugEngine::startCapture() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !profiler_) {
        return false;
    }
    
    if (capturing_) {
        return false;
    }
    
    captured_events_.clear();
    
    if (!profiler_->startCapture()) {
        return false;
    }
    
    capturing_ = true;
    return true;
}

bool GPUDebugEngine::stopCapture() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!capturing_ || !profiler_) {
        return false;
    }
    
    profiler_->stopCapture();
    
    // Drain events
    std::vector<TraceEvent> events;
    profiler_->getEvents(events);
    captured_events_.insert(captured_events_.end(), events.begin(), events.end());
    
    capturing_ = false;
    return true;
}

bool GPUDebugEngine::isCapturing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capturing_;
}

std::vector<TraceEvent> GPUDebugEngine::getCapturedEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    return captured_events_;
}

bool GPUDebugEngine::saveTrace(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (captured_events_.empty()) {
        return false;
    }
    
    SBTWriter writer(filename);
    if (!writer.isOpen()) {
        return false;
    }
    
    for (const auto& event : captured_events_) {
        if (!writer.writeEvent(event)) {
            return false;
        }
    }
    
    return writer.finalize().success;
}

// ============================================================
// Trace Replay
// ============================================================

bool GPUDebugEngine::loadTrace(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SBTReader reader(filename);
    if (!reader.isOpen() || !reader.isValid()) {
        return false;
    }
    
    replay_events_.clear();
    
    TraceRecord record;
    if (!reader.readAll(record)) {
        return false;
    }
    
    replay_events_ = record.events();
    
    if (replay_events_.empty()) {
        return false;
    }
    
    replay_state_.trace_file = filename;
    replay_state_.total_events = replay_events_.size();
    replay_state_.current_event_index = 0;
    replay_state_.current_timestamp = replay_events_.front().timestamp;
    replay_state_.total_duration = replay_events_.back().timestamp - replay_events_.front().timestamp;
    replay_state_.active = false;
    replay_state_.paused = false;
    
    return true;
}

ReplayState GPUDebugEngine::getReplayState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return replay_state_;
}

bool GPUDebugEngine::controlReplay(const ReplayControl& control) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (replay_events_.empty()) {
        return false;
    }
    
    switch (control.command) {
        case ReplayControl::Command::Start:
            replay_state_.active = true;
            replay_state_.paused = false;
            replay_state_.current_event_index = 0;
            break;
            
        case ReplayControl::Command::Stop:
            replay_state_.active = false;
            replay_state_.paused = false;
            replay_state_.current_event_index = 0;
            break;
            
        case ReplayControl::Command::Pause:
            replay_state_.paused = true;
            break;
            
        case ReplayControl::Command::Resume:
            replay_state_.paused = false;
            break;
            
        case ReplayControl::Command::StepEvent:
            if (replay_state_.current_event_index < replay_events_.size()) {
                replay_state_.current_event_index++;
                if (replay_state_.current_event_index < replay_events_.size()) {
                    replay_state_.current_timestamp = 
                        replay_events_[replay_state_.current_event_index].timestamp;
                }
            }
            break;
            
        case ReplayControl::Command::StepKernel:
            while (replay_state_.current_event_index < replay_events_.size()) {
                replay_state_.current_event_index++;
                if (replay_state_.current_event_index < replay_events_.size() &&
                    replay_events_[replay_state_.current_event_index].type == EventType::KernelLaunch) {
                    replay_state_.current_timestamp = 
                        replay_events_[replay_state_.current_event_index].timestamp;
                    break;
                }
            }
            break;
            
        case ReplayControl::Command::GotoTimestamp:
            for (size_t i = 0; i < replay_events_.size(); ++i) {
                if (replay_events_[i].timestamp >= control.target_timestamp) {
                    replay_state_.current_event_index = i;
                    replay_state_.current_timestamp = replay_events_[i].timestamp;
                    break;
                }
            }
            break;
            
        case ReplayControl::Command::GotoEvent:
            if (control.target_event_index < replay_events_.size()) {
                replay_state_.current_event_index = control.target_event_index;
                replay_state_.current_timestamp = 
                    replay_events_[replay_state_.current_event_index].timestamp;
            }
            break;
    }
    
    return true;
}

std::optional<TraceEvent> GPUDebugEngine::getCurrentReplayEvent() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!replay_state_.active || replay_state_.current_event_index >= replay_events_.size()) {
        return std::nullopt;
    }
    
    return replay_events_[replay_state_.current_event_index];
}

// ============================================================
// Event Callback
// ============================================================

void GPUDebugEngine::setEventCallback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = callback;
}

// ============================================================
// Process Integration
// ============================================================

void GPUDebugEngine::onProcessStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!config_.auto_capture_on_break) {
        return;
    }
    
    // Auto-capture GPU state when CPU stops
    // This is already handled by the profiler running in background
}

void GPUDebugEngine::onProcessResume() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Nothing special needed on resume
}

// ============================================================
// Internal Helpers
// ============================================================

void GPUDebugEngine::handleEvent(const TraceEvent& event) {
    // Add to history (no lock needed, called from within locked context)
    event_history_.push_back(event);
    while (event_history_.size() > config_.event_history_size) {
        event_history_.pop_front();
    }
    
    // Update state machine
    state_machine_->processEvent(event);
    
    // Track kernel history
    addToKernelHistory(event);
    
    // If capturing, store event
    if (capturing_) {
        captured_events_.push_back(event);
    }
    
    // Check breakpoints
    GPUBreakpoint* matched_bp = nullptr;
    for (auto& bp : gpu_breakpoints_) {
        if (bp.matches(event)) {
            bp.hit_count++;
            matched_bp = &bp;
            break;
        }
    }
    
    // Fire callback
    if (event_callback_) {
        event_callback_(event, matched_bp);
    }
}

void GPUDebugEngine::addToKernelHistory(const TraceEvent& event) {
    if (event.type == EventType::KernelLaunch) {
        KernelCallInfo info;
        info.call_id = event.correlation_id;
        info.kernel_name = event.name;
        info.launch_time = event.timestamp;
        info.complete_time = 0;  // Not complete yet
        info.device_id = event.device_id;
        info.stream_id = event.stream_id;
        
        if (event.kernel_params) {
            info.params = *event.kernel_params;
        }
        
        if (event.call_stack) {
            info.host_callstack = *event.call_stack;
        }
        
        kernel_history_.push_back(info);
        
        while (kernel_history_.size() > config_.kernel_history_size) {
            kernel_history_.pop_front();
        }
    }
    else if (event.type == EventType::KernelComplete) {
        // Find matching launch and update complete time
        for (auto it = kernel_history_.rbegin(); it != kernel_history_.rend(); ++it) {
            if (it->call_id == event.correlation_id && !it->isComplete()) {
                it->complete_time = event.timestamp;
                break;
            }
        }
    }
}

bool GPUDebugEngine::matchesPattern(const std::string& name, const std::string& pattern) const {
    if (pattern.empty()) {
        return true;
    }
    
    return fnmatch(pattern.c_str(), name.c_str(), 0) == 0;
}

} // namespace gdb
} // namespace tracesmith
