/**
 * Frame Capture Example (RenderDoc-inspired)
 * 
 * Demonstrates how to use TraceSmith's frame capture system
 * for debugging GPU applications:
 * 
 * 1. Setup and configuration
 * 2. Resource tracking
 * 3. F12-style capture triggering
 * 4. Draw call inspection
 * 5. Export to Perfetto
 */

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <random>

#include "tracesmith/replay/frame_capture.hpp"
#include "tracesmith/common/types.hpp"

using namespace tracesmith;

// Simulate a simple rendering frame
void simulateFrame(FrameCapture& capture, ResourceTracker& tracker, int frame_num) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> vertex_dist(1000, 10000);
    std::uniform_int_distribution<> instance_dist(1, 100);
    
    // Simulate resource updates
    if (frame_num % 5 == 0) {
        // Update uniform buffer
        tracker.markModified(1, getCurrentTimestamp());
    }
    
    // Simulate multiple draw calls per frame
    int num_draws = 5 + (frame_num % 3);
    
    for (int i = 0; i < num_draws; i++) {
        // Record draw call
        DrawCallInfo draw;
        draw.call_id = frame_num * 100 + i;
        draw.name = "DrawIndexed_" + std::to_string(i);
        draw.timestamp = getCurrentTimestamp();
        draw.vertex_count = vertex_dist(gen);
        draw.instance_count = instance_dist(gen);
        draw.pipeline_id = i % 3;
        draw.vertex_shader = "main_vs";
        draw.fragment_shader = "pbr_fs";
        
        capture.recordDrawCall(draw);
        
        // Record corresponding kernel event
        TraceEvent kernel;
        kernel.type = EventType::KernelLaunch;
        kernel.name = "rasterize_" + std::to_string(i);
        kernel.timestamp = draw.timestamp;
        kernel.stream_id = 0;
        capture.recordEvent(kernel);
        
        // Small delay to simulate GPU work
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Simulate compute dispatch
    DrawCallInfo compute;
    compute.call_id = frame_num * 100 + 50;
    compute.name = "PostProcess_Bloom";
    compute.timestamp = getCurrentTimestamp();
    compute.group_count_x = 64;
    compute.group_count_y = 64;
    compute.group_count_z = 1;
    compute.compute_shader = "bloom_cs";
    capture.recordDispatch(compute);
    
    // Simulate memory operations
    TraceEvent memcpy_event;
    memcpy_event.type = EventType::MemcpyH2D;
    memcpy_event.name = "upload_constants";
    memcpy_event.timestamp = getCurrentTimestamp();
    capture.recordEvent(memcpy_event);
    
    // End of frame (like Present/SwapBuffers)
    capture.onFrameEnd();
}

int main() {
    std::cout << "TraceSmith Frame Capture Example (RenderDoc-inspired)\n";
    std::cout << "=====================================================\n\n";
    
    // 1. Setup resource tracker
    ResourceTracker tracker;
    
    // Register some GPU resources
    tracker.registerResource(1, ResourceType::Buffer, "UniformBuffer");
    tracker.updateResourceBinding(1, 0x1000, 256);
    
    tracker.registerResource(2, ResourceType::Buffer, "VertexBuffer");
    tracker.updateResourceBinding(2, 0x2000, 1024 * 1024);
    
    tracker.registerResource(3, ResourceType::Texture2D, "Albedo");
    tracker.updateResourceBinding(3, 0x10000, 4 * 1024 * 1024);
    
    tracker.registerResource(4, ResourceType::Texture2D, "Normal");
    tracker.updateResourceBinding(4, 0x500000, 4 * 1024 * 1024);
    
    tracker.registerResource(5, ResourceType::Pipeline, "PBR_Pipeline");
    
    std::cout << "✓ Registered " << tracker.getLiveResources().size() << " resources\n";
    
    // 2. Configure frame capture
    FrameCaptureConfig config;
    config.frames_to_capture = 3;           // Capture 3 frames
    config.capture_api_calls = true;
    config.capture_resource_state = true;
    config.capture_buffer_contents = false; // Don't capture actual data
    
    FrameCapture capture(config);
    
    // 3. Register resources with capture
    for (uint64_t id : tracker.getLiveResources()) {
        const ResourceState* res = tracker.getResource(id);
        if (res) {
            capture.recordResourceCreate(*res);
        }
    }
    
    std::cout << "✓ Frame capture configured (capturing " 
              << config.frames_to_capture << " frames)\n\n";
    
    // 4. Simulate some frames before capture
    std::cout << "Simulating frames...\n";
    for (int i = 0; i < 5; i++) {
        simulateFrame(capture, tracker, i);
        std::cout << "  Frame " << i << " (not capturing)\n";
    }
    
    // 5. Trigger capture (like pressing F12)
    std::cout << "\n[F12] Triggering capture!\n\n";
    capture.triggerCapture();
    
    // Next frame end will start actual capture
    simulateFrame(capture, tracker, 5);
    std::cout << "  Capture started...\n";
    
    // Capture remaining frames
    for (int i = 6; i < 9; i++) {
        simulateFrame(capture, tracker, i);
        std::cout << "  Frame " << i << " captured\n";
    }
    
    // 6. Analyze captured frames
    std::cout << "\n═══════════════════════════════════════════════════\n";
    std::cout << "Captured Frame Analysis\n";
    std::cout << "═══════════════════════════════════════════════════\n\n";
    
    const auto& frames = capture.getCapturedFrames();
    std::cout << "Total frames captured: " << frames.size() << "\n\n";
    
    for (const auto& frame : frames) {
        std::cout << "Frame #" << frame.frame_number << "\n";
        std::cout << "  Duration: " << std::fixed << std::setprecision(2) 
                  << frame.duration() / 1000.0 << " µs\n";
        std::cout << "  Events: " << frame.events.size() << "\n";
        std::cout << "  Draw calls: " << frame.draw_calls.size() << "\n";
        std::cout << "  Memory ops: " << frame.total_memory_ops << "\n";
        
        // Show first few draw calls
        std::cout << "  Draw calls detail:\n";
        int shown = 0;
        for (const auto& draw : frame.draw_calls) {
            if (shown++ >= 3) {
                std::cout << "    ... and " << (frame.draw_calls.size() - 3) << " more\n";
                break;
            }
            std::cout << "    - " << draw.name;
            if (draw.vertex_count > 0) {
                std::cout << " (verts=" << draw.vertex_count 
                         << ", inst=" << draw.instance_count << ")";
            } else if (draw.group_count_x > 0) {
                std::cout << " (dispatch " << draw.group_count_x << "x"
                         << draw.group_count_y << "x" << draw.group_count_z << ")";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
    
    // 7. Export to Perfetto
    if (!frames.empty()) {
        std::string filename = "frame_capture.json";
        if (capture.exportToPerfetto(filename, frames[0].frame_number)) {
            std::cout << "✓ Exported to " << filename << "\n";
            std::cout << "  Open in https://ui.perfetto.dev to visualize\n";
        }
    }
    
    // 8. Resource state inspection
    std::cout << "\n═══════════════════════════════════════════════════\n";
    std::cout << "Resource State\n";
    std::cout << "═══════════════════════════════════════════════════\n\n";
    
    for (const auto& [id, state] : capture.getResources()) {
        std::cout << "  " << state.name << " (" 
                  << resourceTypeToString(state.type) << ")\n";
        std::cout << "    Address: 0x" << std::hex << state.address << std::dec << "\n";
        std::cout << "    Size: " << state.size << " bytes\n";
    }
    
    std::cout << "\n✅ Frame capture example complete!\n";
    
    return 0;
}

