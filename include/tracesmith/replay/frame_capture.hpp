#pragma once

/**
 * Frame Capture System (RenderDoc-inspired)
 * 
 * Provides frame-based GPU capture and replay capabilities:
 * - Frame boundary detection and marking
 * - Resource state snapshots (buffers, textures)
 * - API call recording with full state context
 * - Step-by-step debugging support
 * - Resource inspection between draw calls
 * 
 * Design Goals:
 * - Minimal overhead during non-capture mode
 * - Complete state reconstruction for any frame
 * - Support for out-of-order execution analysis
 */

#include "tracesmith/common/types.hpp"
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <optional>

namespace tracesmith {

// Forward declarations
class FrameCapture;
class ResourceTracker;

/// Resource types that can be captured
enum class ResourceType : uint32_t {
    Unknown = 0,
    Buffer,
    Texture1D,
    Texture2D,
    Texture3D,
    TextureCube,
    Sampler,
    Shader,
    Pipeline,
    DescriptorSet,
    CommandBuffer,
    QueryPool
};

/// Resource state at a specific point in time
struct ResourceState {
    uint64_t resource_id;
    ResourceType type;
    std::string name;
    
    // Memory state
    uint64_t address;
    uint64_t size;
    std::vector<uint8_t> data;  // Optional: actual data snapshot
    
    // Texture-specific
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint32_t mip_levels = 0;
    uint32_t array_layers = 0;
    std::string format;
    
    // Usage flags
    bool readable = false;
    bool writable = false;
    bool bound_as_input = false;
    bool bound_as_output = false;
    
    Timestamp last_modified;
    
    ResourceState() : resource_id(0), type(ResourceType::Unknown), 
                      address(0), size(0), last_modified(0) {}
};

/// Draw call information
struct DrawCallInfo {
    uint64_t call_id;
    std::string name;
    Timestamp timestamp;
    
    // Draw parameters
    uint32_t vertex_count = 0;
    uint32_t instance_count = 1;
    uint32_t first_vertex = 0;
    uint32_t first_instance = 0;
    
    // Indexed draw
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
    
    // Compute dispatch
    uint32_t group_count_x = 0;
    uint32_t group_count_y = 0;
    uint32_t group_count_z = 0;
    
    // Bound resources at this draw call
    std::vector<uint64_t> input_resources;
    std::vector<uint64_t> output_resources;
    
    // Pipeline state
    uint64_t pipeline_id = 0;
    std::string vertex_shader;
    std::string fragment_shader;
    std::string compute_shader;
    
    DrawCallInfo() : call_id(0), timestamp(0) {}
};

/// A single captured frame
struct CapturedFrame {
    uint64_t frame_number;
    Timestamp start_time;
    Timestamp end_time;
    
    // Events within this frame
    std::vector<TraceEvent> events;
    
    // Draw calls
    std::vector<DrawCallInfo> draw_calls;
    
    // Resource states at frame start
    std::map<uint64_t, ResourceState> initial_state;
    
    // Resource states at frame end
    std::map<uint64_t, ResourceState> final_state;
    
    // Per-draw-call resource changes
    std::map<uint64_t, std::map<uint64_t, ResourceState>> state_history;
    
    // Statistics
    uint64_t total_draw_calls = 0;
    uint64_t total_dispatches = 0;
    uint64_t total_memory_ops = 0;
    uint64_t total_sync_ops = 0;
    
    CapturedFrame() : frame_number(0), start_time(0), end_time(0) {}
    
    /// Get duration in nanoseconds
    Timestamp duration() const { return end_time - start_time; }
    
    /// Get resource state at a specific draw call
    std::optional<ResourceState> getResourceStateAt(
        uint64_t resource_id, uint64_t draw_call_id) const;
};

/// Frame capture configuration
struct FrameCaptureConfig {
    // Capture mode
    bool capture_on_keypress = true;    // F12 style trigger
    bool capture_after_present = true;  // Auto-detect frame boundaries
    uint32_t frames_to_capture = 1;     // Number of frames
    
    // What to capture
    bool capture_api_calls = true;
    bool capture_resource_state = true;
    bool capture_buffer_contents = false;  // Large data, off by default
    bool capture_texture_contents = false;
    
    // Performance tuning
    size_t max_buffer_capture_size = 64 * 1024 * 1024;  // 64MB
    size_t max_texture_capture_size = 256 * 1024 * 1024; // 256MB
    
    FrameCaptureConfig() = default;
};

/// Frame capture session state
enum class CaptureState {
    Idle,
    Armed,      // Waiting for trigger
    Capturing,
    Processing,
    Complete
};

/// Callback types
using FrameCaptureCallback = std::function<void(const CapturedFrame&)>;
using DrawCallCallback = std::function<void(const DrawCallInfo&)>;

/// Main frame capture class
class FrameCapture {
public:
    FrameCapture();
    explicit FrameCapture(const FrameCaptureConfig& config);
    ~FrameCapture();
    
    // Non-copyable
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;
    
    /// Configure capture
    void setConfig(const FrameCaptureConfig& config) { config_ = config; }
    const FrameCaptureConfig& getConfig() const { return config_; }
    
    /// Trigger capture (like pressing F12 in RenderDoc)
    void triggerCapture();
    
    /// Check if currently capturing
    bool isCapturing() const { return state_ == CaptureState::Capturing; }
    
    /// Get current state
    CaptureState getState() const { return state_; }
    
    // ========================================================================
    // API Call Recording (called by interceptor layer)
    // ========================================================================
    
    /// Record frame boundary (called on Present/SwapBuffers)
    void onFrameEnd();
    
    /// Record a draw call
    void recordDrawCall(const DrawCallInfo& draw);
    
    /// Record a compute dispatch
    void recordDispatch(const DrawCallInfo& dispatch);
    
    /// Record a resource creation
    void recordResourceCreate(const ResourceState& resource);
    
    /// Record a resource update
    void recordResourceUpdate(uint64_t resource_id, 
                              const void* data, size_t size);
    
    /// Record a resource binding
    void recordResourceBind(uint64_t resource_id, 
                           bool as_input, bool as_output);
    
    /// Record a generic trace event
    void recordEvent(const TraceEvent& event);
    
    // ========================================================================
    // Captured Data Access
    // ========================================================================
    
    /// Get all captured frames
    const std::vector<CapturedFrame>& getCapturedFrames() const { 
        return captured_frames_; 
    }
    
    /// Get a specific frame
    const CapturedFrame* getFrame(uint64_t frame_number) const;
    
    /// Get resource by ID
    const ResourceState* getResource(uint64_t resource_id) const;
    
    /// Get all resources
    const std::map<uint64_t, ResourceState>& getResources() const {
        return resources_;
    }
    
    // ========================================================================
    // Replay Support
    // ========================================================================
    
    /// Replay to a specific draw call within a frame
    bool replayToDrawCall(uint64_t frame_number, uint64_t draw_call_id);
    
    /// Get resource state at a specific point
    std::optional<ResourceState> getResourceStateAt(
        uint64_t frame_number, uint64_t draw_call_id, uint64_t resource_id);
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    /// Set callback for frame capture completion
    void setFrameCaptureCallback(FrameCaptureCallback callback) {
        on_frame_captured_ = std::move(callback);
    }
    
    /// Set callback for each draw call (for live inspection)
    void setDrawCallCallback(DrawCallCallback callback) {
        on_draw_call_ = std::move(callback);
    }
    
    /// Clear all captured data
    void clear();
    
    // ========================================================================
    // Export
    // ========================================================================
    
    /// Export captured frame to RenderDoc-compatible format (RDC)
    bool exportToRDC(const std::string& filename, uint64_t frame_number);
    
    /// Export to Perfetto trace
    bool exportToPerfetto(const std::string& filename, uint64_t frame_number);
    
private:
    FrameCaptureConfig config_;
    CaptureState state_ = CaptureState::Idle;
    
    // Current frame being captured
    CapturedFrame current_frame_;
    uint64_t current_frame_number_ = 0;
    uint32_t frames_remaining_ = 0;
    
    // Captured data
    std::vector<CapturedFrame> captured_frames_;
    std::map<uint64_t, ResourceState> resources_;
    
    // Callbacks
    FrameCaptureCallback on_frame_captured_;
    DrawCallCallback on_draw_call_;
    
    // Internal methods
    void beginCapture();
    void endCapture();
    void finalizeFrame();
    void snapshotResources(std::map<uint64_t, ResourceState>& out);
};

/// Resource tracker for monitoring GPU resource lifecycle
class ResourceTracker {
public:
    ResourceTracker() = default;
    
    /// Register a new resource
    void registerResource(uint64_t id, ResourceType type, 
                         const std::string& name = "");
    
    /// Update resource address/size
    void updateResourceBinding(uint64_t id, uint64_t address, uint64_t size);
    
    /// Mark resource as modified
    void markModified(uint64_t id, Timestamp when);
    
    /// Mark resource as destroyed
    void destroyResource(uint64_t id);
    
    /// Get resource info
    const ResourceState* getResource(uint64_t id) const;
    
    /// Get all live resources
    std::vector<uint64_t> getLiveResources() const;
    
    /// Get resources modified since timestamp
    std::vector<uint64_t> getModifiedSince(Timestamp since) const;
    
private:
    std::map<uint64_t, ResourceState> resources_;
    std::vector<uint64_t> destroyed_;
};

// ============================================================================
// Utility Functions
// ============================================================================

/// Convert ResourceType to string
inline const char* resourceTypeToString(ResourceType type) {
    switch (type) {
        case ResourceType::Buffer: return "Buffer";
        case ResourceType::Texture1D: return "Texture1D";
        case ResourceType::Texture2D: return "Texture2D";
        case ResourceType::Texture3D: return "Texture3D";
        case ResourceType::TextureCube: return "TextureCube";
        case ResourceType::Sampler: return "Sampler";
        case ResourceType::Shader: return "Shader";
        case ResourceType::Pipeline: return "Pipeline";
        case ResourceType::DescriptorSet: return "DescriptorSet";
        case ResourceType::CommandBuffer: return "CommandBuffer";
        case ResourceType::QueryPool: return "QueryPool";
        default: return "Unknown";
    }
}

} // namespace tracesmith

