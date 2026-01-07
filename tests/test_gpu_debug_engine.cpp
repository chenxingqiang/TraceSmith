/**
 * @file test_gpu_debug_engine.cpp
 * @brief Unit tests for GPU Debug Engine
 * 
 * Test-Driven Development: These tests are written BEFORE implementation.
 * Tests verify GPU state management, breakpoints, and trace replay.
 */

#include <gtest/gtest.h>
#include <tracesmith/gdb/gpu_debug_engine.hpp>
#include <tracesmith/gdb/gdb_types.hpp>
#include <tracesmith/common/types.hpp>
#include <vector>
#include <string>

using namespace tracesmith;
using namespace tracesmith::gdb;

// ============================================================
// GPU Debug Engine Initialization Tests
// ============================================================

TEST(GPUDebugEngineTest, DefaultConstruction) {
    GPUDebugEngine engine;
    
    EXPECT_FALSE(engine.isInitialized());
}

TEST(GPUDebugEngineTest, CustomConfig) {
    GPUDebugConfig config;
    config.kernel_history_size = 500;
    config.event_history_size = 5000;
    config.auto_capture_on_break = false;
    
    GPUDebugEngine engine(config);
    
    EXPECT_FALSE(engine.isInitialized());
}

// ============================================================
// GPU Breakpoint Tests
// ============================================================

TEST(GPUDebugEngineTest, SetGPUBreakpoint) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    bp.kernel_pattern = "matmul*";
    
    int bp_id = engine.setGPUBreakpoint(bp);
    
    EXPECT_GT(bp_id, 0);
}

TEST(GPUDebugEngineTest, ListGPUBreakpoints) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp1;
    bp1.type = GPUBreakpointType::KernelLaunch;
    bp1.kernel_pattern = "kernel1";
    
    GPUBreakpoint bp2;
    bp2.type = GPUBreakpointType::MemcpyH2D;
    
    engine.setGPUBreakpoint(bp1);
    engine.setGPUBreakpoint(bp2);
    
    auto breakpoints = engine.listGPUBreakpoints();
    
    EXPECT_EQ(breakpoints.size(), 2u);
}

TEST(GPUDebugEngineTest, RemoveGPUBreakpoint) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    
    int bp_id = engine.setGPUBreakpoint(bp);
    EXPECT_EQ(engine.listGPUBreakpoints().size(), 1u);
    
    bool removed = engine.removeGPUBreakpoint(bp_id);
    
    EXPECT_TRUE(removed);
    EXPECT_EQ(engine.listGPUBreakpoints().size(), 0u);
}

TEST(GPUDebugEngineTest, RemoveNonexistentBreakpoint) {
    GPUDebugEngine engine;
    
    bool removed = engine.removeGPUBreakpoint(999);
    
    EXPECT_FALSE(removed);
}

TEST(GPUDebugEngineTest, EnableDisableGPUBreakpoint) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    
    int bp_id = engine.setGPUBreakpoint(bp);
    
    // Disable
    bool result = engine.enableGPUBreakpoint(bp_id, false);
    EXPECT_TRUE(result);
    
    auto bps = engine.listGPUBreakpoints();
    ASSERT_EQ(bps.size(), 1u);
    EXPECT_FALSE(bps[0].enabled);
    
    // Re-enable
    result = engine.enableGPUBreakpoint(bp_id, true);
    EXPECT_TRUE(result);
    
    bps = engine.listGPUBreakpoints();
    EXPECT_TRUE(bps[0].enabled);
}

TEST(GPUDebugEngineTest, CheckBreakpointKernelLaunch) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    bp.kernel_pattern = "matmul*";
    engine.setGPUBreakpoint(bp);
    
    TraceEvent event(EventType::KernelLaunch);
    event.name = "matmul_f32";
    
    auto matched = engine.checkBreakpoints(event);
    
    ASSERT_TRUE(matched.has_value());
    EXPECT_EQ(matched->type, GPUBreakpointType::KernelLaunch);
}

TEST(GPUDebugEngineTest, CheckBreakpointNoMatch) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    bp.kernel_pattern = "matmul*";
    engine.setGPUBreakpoint(bp);
    
    TraceEvent event(EventType::KernelLaunch);
    event.name = "conv2d_f32";
    
    auto matched = engine.checkBreakpoints(event);
    
    EXPECT_FALSE(matched.has_value());
}

TEST(GPUDebugEngineTest, CheckBreakpointMemcpy) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::MemcpyH2D;
    engine.setGPUBreakpoint(bp);
    
    TraceEvent event;
    event.type = EventType::MemcpyH2D;
    
    auto matched = engine.checkBreakpoints(event);
    
    ASSERT_TRUE(matched.has_value());
    EXPECT_EQ(matched->type, GPUBreakpointType::MemcpyH2D);
}

TEST(GPUDebugEngineTest, CheckBreakpointMultiple) {
    GPUDebugEngine engine;
    
    GPUBreakpoint bp1;
    bp1.type = GPUBreakpointType::KernelLaunch;
    bp1.kernel_pattern = "kernel1";
    engine.setGPUBreakpoint(bp1);
    
    GPUBreakpoint bp2;
    bp2.type = GPUBreakpointType::KernelLaunch;
    bp2.kernel_pattern = "kernel2";
    engine.setGPUBreakpoint(bp2);
    
    TraceEvent event(EventType::KernelLaunch);
    event.name = "kernel2";
    
    auto matched = engine.checkBreakpoints(event);
    
    ASSERT_TRUE(matched.has_value());
    EXPECT_EQ(matched->kernel_pattern, "kernel2");
}

// ============================================================
// Kernel History Tests
// ============================================================

TEST(GPUDebugEngineTest, KernelHistoryEmpty) {
    GPUDebugEngine engine;
    
    auto history = engine.getKernelHistory(10);
    
    EXPECT_TRUE(history.empty());
}

TEST(GPUDebugEngineTest, GetActiveKernelsEmpty) {
    GPUDebugEngine engine;
    
    auto active = engine.getActiveKernels();
    
    EXPECT_TRUE(active.empty());
}

TEST(GPUDebugEngineTest, FindKernelsEmpty) {
    GPUDebugEngine engine;
    
    auto found = engine.findKernels("test*");
    
    EXPECT_TRUE(found.empty());
}

// ============================================================
// GPU State Tests
// ============================================================

TEST(GPUDebugEngineTest, GetGPUStateUninitialized) {
    GPUDebugEngine engine;
    
    GPUStateSnapshot state = engine.getGPUState();
    
    // Should return empty/default state when not initialized
    EXPECT_TRUE(state.devices.empty());
}

TEST(GPUDebugEngineTest, GetDevicesUninitialized) {
    GPUDebugEngine engine;
    
    auto devices = engine.getDevices();
    
    EXPECT_TRUE(devices.empty());
}

TEST(GPUDebugEngineTest, GetStreamStatesUninitialized) {
    GPUDebugEngine engine;
    
    auto streams = engine.getStreamStates();
    
    EXPECT_TRUE(streams.empty());
}

// ============================================================
// Trace Capture Tests
// ============================================================

TEST(GPUDebugEngineTest, CaptureNotInitialized) {
    GPUDebugEngine engine;
    
    // Should fail gracefully when not initialized
    bool started = engine.startCapture();
    
    EXPECT_FALSE(started);
    EXPECT_FALSE(engine.isCapturing());
}

TEST(GPUDebugEngineTest, StopCaptureWhenNotCapturing) {
    GPUDebugEngine engine;
    
    // Should not crash
    bool stopped = engine.stopCapture();
    
    EXPECT_FALSE(stopped);
}

TEST(GPUDebugEngineTest, GetCapturedEventsEmpty) {
    GPUDebugEngine engine;
    
    auto events = engine.getCapturedEvents();
    
    EXPECT_TRUE(events.empty());
}

TEST(GPUDebugEngineTest, SaveTraceNotInitialized) {
    GPUDebugEngine engine;
    
    bool saved = engine.saveTrace("/tmp/test_trace.sbt");
    
    EXPECT_FALSE(saved);
}

// ============================================================
// Trace Replay Tests
// ============================================================

TEST(GPUDebugEngineTest, LoadTraceNonexistent) {
    GPUDebugEngine engine;
    
    bool loaded = engine.loadTrace("/nonexistent/path.sbt");
    
    EXPECT_FALSE(loaded);
}

TEST(GPUDebugEngineTest, GetReplayStateDefault) {
    GPUDebugEngine engine;
    
    ReplayState state = engine.getReplayState();
    
    EXPECT_FALSE(state.active);
    EXPECT_FALSE(state.paused);
    EXPECT_EQ(state.current_event_index, 0u);
}

TEST(GPUDebugEngineTest, ControlReplayNotLoaded) {
    GPUDebugEngine engine;
    
    ReplayControl ctrl;
    ctrl.command = ReplayControl::Command::Start;
    
    bool result = engine.controlReplay(ctrl);
    
    EXPECT_FALSE(result);
}

TEST(GPUDebugEngineTest, GetCurrentReplayEventNotActive) {
    GPUDebugEngine engine;
    
    auto event = engine.getCurrentReplayEvent();
    
    EXPECT_FALSE(event.has_value());
}

// ============================================================
// GPU Memory Access Tests
// ============================================================

TEST(GPUDebugEngineTest, ReadGPUMemoryNotInitialized) {
    GPUDebugEngine engine;
    
    auto data = engine.readGPUMemory(0, 0x1000, 256);
    
    EXPECT_TRUE(data.empty());
}

TEST(GPUDebugEngineTest, WriteGPUMemoryNotInitialized) {
    GPUDebugEngine engine;
    
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    bool result = engine.writeGPUMemory(0, 0x1000, data);
    
    EXPECT_FALSE(result);
}

TEST(GPUDebugEngineTest, GetMemoryAllocationsEmpty) {
    GPUDebugEngine engine;
    
    auto allocs = engine.getMemoryAllocations();
    
    EXPECT_TRUE(allocs.empty());
}

// ============================================================
// Event Callback Tests
// ============================================================

TEST(GPUDebugEngineTest, SetEventCallback) {
    GPUDebugEngine engine;
    
    bool callback_called = false;
    engine.setEventCallback([&](const TraceEvent&, const GPUBreakpoint*) {
        callback_called = true;
    });
    
    // Callback should be set but not called yet
    EXPECT_FALSE(callback_called);
}

// ============================================================
// Process Integration Tests
// ============================================================

TEST(GPUDebugEngineTest, OnProcessStop) {
    GPUDebugEngine engine;
    
    // Should not crash even when not initialized
    engine.onProcessStop();
}

TEST(GPUDebugEngineTest, OnProcessResume) {
    GPUDebugEngine engine;
    
    // Should not crash even when not initialized
    engine.onProcessResume();
}

// ============================================================
// Pattern Matching Tests (Internal)
// ============================================================

// Test wildcard pattern matching used in kernel breakpoints
class GPUDebugEnginePatternTest : public ::testing::Test {
protected:
    bool matchPattern(const std::string& name, const std::string& pattern) {
        // Simple wildcard matching: * matches any suffix
        if (pattern.empty()) return name.empty();
        if (pattern == "*") return true;
        
        size_t star_pos = pattern.find('*');
        if (star_pos == std::string::npos) {
            return name == pattern;
        }
        
        // Pattern has wildcard at end
        std::string prefix = pattern.substr(0, star_pos);
        return name.compare(0, prefix.length(), prefix) == 0;
    }
};

TEST_F(GPUDebugEnginePatternTest, ExactMatch) {
    EXPECT_TRUE(matchPattern("kernel", "kernel"));
    EXPECT_FALSE(matchPattern("kernel", "other"));
}

TEST_F(GPUDebugEnginePatternTest, WildcardSuffix) {
    EXPECT_TRUE(matchPattern("matmul_f32", "matmul*"));
    EXPECT_TRUE(matchPattern("matmul_f16", "matmul*"));
    EXPECT_TRUE(matchPattern("matmul", "matmul*"));
    EXPECT_FALSE(matchPattern("conv2d", "matmul*"));
}

TEST_F(GPUDebugEnginePatternTest, AllWildcard) {
    EXPECT_TRUE(matchPattern("anything", "*"));
    EXPECT_TRUE(matchPattern("", "*"));
}

TEST_F(GPUDebugEnginePatternTest, EmptyPattern) {
    EXPECT_TRUE(matchPattern("", ""));
    EXPECT_FALSE(matchPattern("kernel", ""));
}

// ============================================================
// Memory Usage Tracking Tests
// ============================================================

TEST(GPUDebugEngineTest, GetMemoryUsageDefault) {
    GPUDebugEngine engine;
    
    auto usage = engine.getMemoryUsage(-1);  // All devices
    
    EXPECT_EQ(usage.live_bytes, 0u);
    EXPECT_EQ(usage.live_allocations, 0u);
}

TEST(GPUDebugEngineTest, GetMemoryUsageSpecificDevice) {
    GPUDebugEngine engine;
    
    auto usage = engine.getMemoryUsage(0);
    
    EXPECT_EQ(usage.live_bytes, 0u);
}
