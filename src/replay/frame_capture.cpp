/**
 * Frame Capture Implementation
 * 
 * RenderDoc-inspired frame-based GPU capture system.
 */

#include "tracesmith/replay/frame_capture.hpp"
#include "tracesmith/state/perfetto_exporter.hpp"
#include <algorithm>
#include <fstream>
#include <cstring>

namespace tracesmith {

// ============================================================================
// FrameCapture Implementation
// ============================================================================

FrameCapture::FrameCapture() : config_() {}

FrameCapture::FrameCapture(const FrameCaptureConfig& config) : config_(config) {}

FrameCapture::~FrameCapture() = default;

void FrameCapture::triggerCapture() {
    if (state_ == CaptureState::Idle) {
        state_ = CaptureState::Armed;
        frames_remaining_ = config_.frames_to_capture;
    }
}

void FrameCapture::onFrameEnd() {
    if (state_ == CaptureState::Armed) {
        // Start capturing from next frame
        beginCapture();
        return;
    }
    
    if (state_ == CaptureState::Capturing) {
        finalizeFrame();
        
        frames_remaining_--;
        if (frames_remaining_ == 0) {
            endCapture();
        } else {
            // Start new frame capture
            current_frame_ = CapturedFrame{};
            current_frame_.frame_number = ++current_frame_number_;
            current_frame_.start_time = getCurrentTimestamp();
            snapshotResources(current_frame_.initial_state);
        }
    }
}

void FrameCapture::beginCapture() {
    state_ = CaptureState::Capturing;
    current_frame_ = CapturedFrame{};
    current_frame_.frame_number = ++current_frame_number_;
    current_frame_.start_time = getCurrentTimestamp();
    
    // Snapshot initial resource state
    if (config_.capture_resource_state) {
        snapshotResources(current_frame_.initial_state);
    }
}

void FrameCapture::endCapture() {
    state_ = CaptureState::Complete;
}

void FrameCapture::finalizeFrame() {
    current_frame_.end_time = getCurrentTimestamp();
    
    // Snapshot final resource state
    if (config_.capture_resource_state) {
        snapshotResources(current_frame_.final_state);
    }
    
    // Compute statistics
    for (const auto& event : current_frame_.events) {
        switch (event.type) {
            case EventType::KernelLaunch:
            case EventType::KernelComplete:
                // Check if it's compute by looking at kernel params
                if (event.kernel_params && 
                    event.kernel_params->grid_x > 0) {
                    // Treat as compute dispatch
                    current_frame_.total_dispatches++;
                } else {
                    current_frame_.total_draw_calls++;
                }
                break;
            case EventType::MemcpyH2D:
            case EventType::MemcpyD2H:
            case EventType::MemcpyD2D:
            case EventType::MemAlloc:
            case EventType::MemFree:
                current_frame_.total_memory_ops++;
                break;
            case EventType::StreamSync:
            case EventType::DeviceSync:
            case EventType::EventSync:
                current_frame_.total_sync_ops++;
                break;
            default:
                break;
        }
    }
    
    // Store frame
    captured_frames_.push_back(std::move(current_frame_));
    
    // Notify callback
    if (on_frame_captured_) {
        on_frame_captured_(captured_frames_.back());
    }
}

void FrameCapture::recordDrawCall(const DrawCallInfo& draw) {
    if (state_ != CaptureState::Capturing) return;
    
    current_frame_.draw_calls.push_back(draw);
    
    // Snapshot resource state at this draw call
    if (config_.capture_resource_state) {
        std::map<uint64_t, ResourceState> state;
        snapshotResources(state);
        current_frame_.state_history[draw.call_id] = std::move(state);
    }
    
    if (on_draw_call_) {
        on_draw_call_(draw);
    }
}

void FrameCapture::recordDispatch(const DrawCallInfo& dispatch) {
    // Same handling as draw calls
    recordDrawCall(dispatch);
}

void FrameCapture::recordResourceCreate(const ResourceState& resource) {
    resources_[resource.resource_id] = resource;
}

void FrameCapture::recordResourceUpdate(uint64_t resource_id, 
                                        const void* data, size_t size) {
    auto it = resources_.find(resource_id);
    if (it == resources_.end()) return;
    
    it->second.last_modified = getCurrentTimestamp();
    
    // Optionally capture data
    if (config_.capture_buffer_contents && 
        it->second.type == ResourceType::Buffer &&
        size <= config_.max_buffer_capture_size) {
        it->second.data.resize(size);
        std::memcpy(it->second.data.data(), data, size);
    }
}

void FrameCapture::recordResourceBind(uint64_t resource_id,
                                      bool as_input, bool as_output) {
    auto it = resources_.find(resource_id);
    if (it == resources_.end()) return;
    
    it->second.bound_as_input = as_input;
    it->second.bound_as_output = as_output;
}

void FrameCapture::recordEvent(const TraceEvent& event) {
    if (state_ != CaptureState::Capturing) return;
    
    current_frame_.events.push_back(event);
}

const CapturedFrame* FrameCapture::getFrame(uint64_t frame_number) const {
    for (const auto& frame : captured_frames_) {
        if (frame.frame_number == frame_number) {
            return &frame;
        }
    }
    return nullptr;
}

const ResourceState* FrameCapture::getResource(uint64_t resource_id) const {
    auto it = resources_.find(resource_id);
    return it != resources_.end() ? &it->second : nullptr;
}

bool FrameCapture::replayToDrawCall(uint64_t frame_number, 
                                    uint64_t draw_call_id) {
    const auto* frame = getFrame(frame_number);
    if (!frame) return false;
    
    // Find the draw call
    for (const auto& draw : frame->draw_calls) {
        if (draw.call_id == draw_call_id) {
            // In a real implementation, this would:
            // 1. Restore initial resource state
            // 2. Replay all events up to this draw call
            // 3. Pause for inspection
            return true;
        }
    }
    
    return false;
}

std::optional<ResourceState> FrameCapture::getResourceStateAt(
    uint64_t frame_number, uint64_t draw_call_id, uint64_t resource_id) {
    
    const auto* frame = getFrame(frame_number);
    if (!frame) return std::nullopt;
    
    auto history_it = frame->state_history.find(draw_call_id);
    if (history_it == frame->state_history.end()) {
        return std::nullopt;
    }
    
    auto resource_it = history_it->second.find(resource_id);
    if (resource_it == history_it->second.end()) {
        return std::nullopt;
    }
    
    return resource_it->second;
}

void FrameCapture::clear() {
    captured_frames_.clear();
    resources_.clear();
    current_frame_ = CapturedFrame{};
    current_frame_number_ = 0;
    frames_remaining_ = 0;
    state_ = CaptureState::Idle;
}

void FrameCapture::snapshotResources(std::map<uint64_t, ResourceState>& out) {
    out = resources_;  // Copy current state
}

bool FrameCapture::exportToRDC(const std::string& filename, 
                               uint64_t frame_number) {
    // RDC is a proprietary format - this is a placeholder
    // In practice, you'd need to implement RenderDoc's serialization
    (void)filename;
    (void)frame_number;
    return false;
}

bool FrameCapture::exportToPerfetto(const std::string& filename,
                                    uint64_t frame_number) {
    const auto* frame = getFrame(frame_number);
    if (!frame) return false;
    
    PerfettoExporter exporter;
    
    // Export events (empty counters vector)
    std::vector<CounterEvent> empty_counters;
    return exporter.exportToFile(frame->events, empty_counters, filename);
}

// ============================================================================
// CapturedFrame Implementation
// ============================================================================

std::optional<ResourceState> CapturedFrame::getResourceStateAt(
    uint64_t resource_id, uint64_t draw_call_id) const {
    
    auto history_it = state_history.find(draw_call_id);
    if (history_it == state_history.end()) {
        // Fall back to initial state
        auto init_it = initial_state.find(resource_id);
        if (init_it != initial_state.end()) {
            return init_it->second;
        }
        return std::nullopt;
    }
    
    auto resource_it = history_it->second.find(resource_id);
    if (resource_it == history_it->second.end()) {
        return std::nullopt;
    }
    
    return resource_it->second;
}

// ============================================================================
// ResourceTracker Implementation
// ============================================================================

void ResourceTracker::registerResource(uint64_t id, ResourceType type,
                                       const std::string& name) {
    ResourceState state;
    state.resource_id = id;
    state.type = type;
    state.name = name.empty() ? ("Resource_" + std::to_string(id)) : name;
    state.last_modified = getCurrentTimestamp();
    
    resources_[id] = std::move(state);
}

void ResourceTracker::updateResourceBinding(uint64_t id, 
                                            uint64_t address, uint64_t size) {
    auto it = resources_.find(id);
    if (it == resources_.end()) return;
    
    it->second.address = address;
    it->second.size = size;
}

void ResourceTracker::markModified(uint64_t id, Timestamp when) {
    auto it = resources_.find(id);
    if (it == resources_.end()) return;
    
    it->second.last_modified = when;
}

void ResourceTracker::destroyResource(uint64_t id) {
    auto it = resources_.find(id);
    if (it == resources_.end()) return;
    
    destroyed_.push_back(id);
    resources_.erase(it);
}

const ResourceState* ResourceTracker::getResource(uint64_t id) const {
    auto it = resources_.find(id);
    return it != resources_.end() ? &it->second : nullptr;
}

std::vector<uint64_t> ResourceTracker::getLiveResources() const {
    std::vector<uint64_t> result;
    result.reserve(resources_.size());
    
    for (const auto& [id, state] : resources_) {
        result.push_back(id);
    }
    
    return result;
}

std::vector<uint64_t> ResourceTracker::getModifiedSince(Timestamp since) const {
    std::vector<uint64_t> result;
    
    for (const auto& [id, state] : resources_) {
        if (state.last_modified >= since) {
            result.push_back(id);
        }
    }
    
    return result;
}

} // namespace tracesmith

