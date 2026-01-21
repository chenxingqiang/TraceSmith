/**
 * Tracy Integration Tests
 * 
 * Tests for the Tracy-TraceSmith integration including:
 * - Tracy client functionality
 * - Event export to Tracy
 * - Event import from Tracy format
 * - Type conversions
 */

#include <gtest/gtest.h>

// Include Tracy integration headers (works even without Tracy enabled)
#include "tracesmith/tracy/tracy_client.hpp"
#include "tracesmith/tracy/tracy_exporter.hpp"
#include "tracesmith/tracy/tracy_importer.hpp"
#include "tracesmith/common/types.hpp"

using namespace tracesmith;
using namespace tracesmith::tracy;

// =============================================================================
// Tracy Client Tests
// =============================================================================

class TracyClientTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TracyClientTest, IsTracyEnabledCompileTime) {
    // This should return true if built with -DTRACESMITH_ENABLE_TRACY=ON
    bool enabled = isTracyEnabled();
    // Just ensure it doesn't crash
    SUCCEED();
    (void)enabled;
}

TEST_F(TracyClientTest, GetColorForEventType) {
    // Test color mapping for all event types
    EXPECT_EQ(getColorForEventType(EventType::KernelLaunch), colors::KernelLaunch);
    EXPECT_EQ(getColorForEventType(EventType::KernelComplete), colors::KernelComplete);
    EXPECT_EQ(getColorForEventType(EventType::MemcpyH2D), colors::MemcpyH2D);
    EXPECT_EQ(getColorForEventType(EventType::MemcpyD2H), colors::MemcpyD2H);
    EXPECT_EQ(getColorForEventType(EventType::MemcpyD2D), colors::MemcpyD2D);
    EXPECT_EQ(getColorForEventType(EventType::MemAlloc), colors::MemAlloc);
    EXPECT_EQ(getColorForEventType(EventType::MemFree), colors::MemFree);
    EXPECT_EQ(getColorForEventType(EventType::StreamSync), colors::StreamSync);
    EXPECT_EQ(getColorForEventType(EventType::DeviceSync), colors::DeviceSync);
    EXPECT_EQ(getColorForEventType(EventType::Unknown), colors::Default);
}

TEST_F(TracyClientTest, EmitToTracyNoOp) {
    // These should be no-ops when Tracy is disabled
    TraceEvent event;
    event.type = EventType::KernelLaunch;
    event.name = "test_kernel";
    emitToTracy(event);
    
    MemoryEvent mem_event;
    mem_event.ptr = 0x1000;
    mem_event.bytes = 1024;
    mem_event.is_allocation = true;
    emitMemoryToTracy(mem_event);
    
    CounterEvent counter_event;
    counter_event.counter_name = "test_counter";
    counter_event.value = 42.0;
    emitCounterToTracy(counter_event);
    
    SUCCEED();
}

TEST_F(TracyClientTest, FrameMarking) {
    // These should be no-ops when Tracy is disabled
    markFrame();
    markFrame("TestFrame");
    markFrameStart("TestFrame");
    markFrameEnd("TestFrame");
    SUCCEED();
}

TEST_F(TracyClientTest, PlotConfiguration) {
    configurePlot("TestPlot", PlotType::Number, false, true, 0xFF0000);
    configurePlot("MemoryPlot", PlotType::Memory, true, true, 0);
    configurePlot("PercentPlot", PlotType::Percentage, false, false, 0x00FF00);
    SUCCEED();
}

TEST_F(TracyClientTest, AppInfo) {
    setAppInfo("Test Application", 16);
    setAppInfo(std::string("Test Application String"));
    SUCCEED();
}

TEST_F(TracyClientTest, MessageLogging) {
    logMessage("Test message", 12);
    logMessage("Colored message", 15, 0xFF0000);
    logMessage(std::string("String message"));
    logMessage(std::string("Colored string"), 0x00FF00);
    SUCCEED();
}

TEST_F(TracyClientTest, TracySmithZoneBasic) {
    {
        TracySmithZone zone("TestZone");
        // Zone should be active
    }
    // Zone should be destroyed
    SUCCEED();
}

TEST_F(TracyClientTest, TracySmithZoneWithColor) {
    {
        TracySmithZone zone("ColoredZone", colors::KernelLaunch);
    }
    SUCCEED();
}

// =============================================================================
// Tracy Exporter Tests
// =============================================================================

class TracyExporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.enable_gpu_zones = true;
        config_.enable_memory_tracking = true;
        config_.enable_counters = true;
        config_.auto_configure_plots = true;
    }
    
    void TearDown() override {}
    
    TracyExporterConfig config_;
};

TEST_F(TracyExporterTest, CreateDefaultExporter) {
    TracyExporter exporter;
    EXPECT_FALSE(exporter.isInitialized());
}

TEST_F(TracyExporterTest, CreateWithConfig) {
    TracyExporter exporter(config_);
    EXPECT_FALSE(exporter.isInitialized());
}

TEST_F(TracyExporterTest, Initialize) {
    TracyExporter exporter(config_);
    bool result = exporter.initialize();
    // Result depends on whether Tracy is enabled
    if (isTracyEnabled()) {
        EXPECT_TRUE(result);
        EXPECT_TRUE(exporter.isInitialized());
    }
}

TEST_F(TracyExporterTest, InitializeAndShutdown) {
    TracyExporter exporter(config_);
    exporter.initialize();
    exporter.shutdown();
    EXPECT_FALSE(exporter.isInitialized());
}

TEST_F(TracyExporterTest, EmitEvent) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    TraceEvent event;
    event.type = EventType::KernelLaunch;
    event.name = "test_kernel";
    event.timestamp = getCurrentTimestamp();
    event.duration = 1000000;  // 1ms
    event.device_id = 0;
    event.stream_id = 0;
    
    exporter.emitEvent(event);
    
    if (isTracyEnabled()) {
        EXPECT_EQ(exporter.eventsEmitted(), 1);
    }
}

TEST_F(TracyExporterTest, EmitMemoryEvent) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    MemoryEvent event;
    event.ptr = 0x1000;
    event.bytes = 4096;
    event.is_allocation = true;
    event.allocator_name = "TestAllocator";
    event.timestamp = getCurrentTimestamp();
    
    exporter.emitMemoryEvent(event);
    
    if (isTracyEnabled() && config_.enable_memory_tracking) {
        EXPECT_GE(exporter.eventsEmitted(), 1);
    }
}

TEST_F(TracyExporterTest, EmitCounterEvent) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    CounterEvent event;
    event.counter_name = "TestCounter";
    event.value = 42.0;
    event.timestamp = getCurrentTimestamp();
    
    exporter.emitCounterEvent(event);
    
    if (isTracyEnabled() && config_.enable_counters) {
        EXPECT_GE(exporter.eventsEmitted(), 1);
    }
}

TEST_F(TracyExporterTest, ExportMultipleEvents) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    std::vector<TraceEvent> events;
    for (int i = 0; i < 10; ++i) {
        TraceEvent event;
        event.type = EventType::KernelLaunch;
        event.name = "kernel_" + std::to_string(i);
        event.timestamp = getCurrentTimestamp();
        events.push_back(event);
    }
    
    exporter.exportEvents(events);
    
    if (isTracyEnabled()) {
        EXPECT_GE(exporter.eventsEmitted(), 10);
    }
}

TEST_F(TracyExporterTest, CreateGpuContext) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    uint8_t ctx1 = exporter.createGpuContext(0, "GPU 0");
    uint8_t ctx2 = exporter.createGpuContext(1, "GPU 1");
    
    // Different devices should get different contexts
    if (isTracyEnabled()) {
        EXPECT_NE(ctx1, ctx2);
    }
}

TEST_F(TracyExporterTest, CreateGpuContextSameDevice) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    uint8_t ctx1 = exporter.createGpuContext(0, "GPU 0");
    uint8_t ctx2 = exporter.createGpuContext(0, "GPU 0 again");
    
    // Same device should return same context
    EXPECT_EQ(ctx1, ctx2);
}

TEST_F(TracyExporterTest, EmitGpuZone) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    uint8_t ctx = exporter.createGpuContext(0, "Test GPU");
    
    Timestamp cpu_start = getCurrentTimestamp();
    Timestamp cpu_end = cpu_start + 1000000;
    Timestamp gpu_start = cpu_start + 1000;
    Timestamp gpu_end = cpu_end - 1000;
    
    exporter.emitGpuZone(ctx, "test_kernel", cpu_start, cpu_end, 
                         gpu_start, gpu_end, colors::KernelLaunch);
    
    if (isTracyEnabled()) {
        EXPECT_EQ(exporter.gpuZonesEmitted(), 1);
    }
}

TEST_F(TracyExporterTest, FrameMarking) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    exporter.markFrame();
    exporter.markFrame("TestFrame");
    exporter.markFrameStart("Frame");
    exporter.markFrameEnd("Frame");
    
    SUCCEED();
}

TEST_F(TracyExporterTest, ConfigurePlot) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    exporter.configurePlot("TestPlot", PlotType::Number, false, true, 0xFF0000);
    exporter.configurePlot("MemPlot", PlotType::Memory, true, true, 0);
    
    SUCCEED();
}

TEST_F(TracyExporterTest, EmitPlotValue) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    exporter.emitPlotValue("TestPlot", 42.0);
    exporter.emitPlotValue("IntPlot", static_cast<int64_t>(100));
    
    SUCCEED();
}

TEST_F(TracyExporterTest, ResetStats) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    TraceEvent event;
    event.type = EventType::KernelLaunch;
    event.name = "test";
    exporter.emitEvent(event);
    
    exporter.resetStats();
    
    EXPECT_EQ(exporter.eventsEmitted(), 0);
    EXPECT_EQ(exporter.gpuZonesEmitted(), 0);
}

TEST_F(TracyExporterTest, MoveConstruction) {
    TracyExporter exporter1(config_);
    exporter1.initialize();
    
    TraceEvent event;
    event.type = EventType::Marker;
    event.name = "test";
    exporter1.emitEvent(event);
    
    TracyExporter exporter2(std::move(exporter1));
    
    if (isTracyEnabled()) {
        EXPECT_TRUE(exporter2.isInitialized());
        EXPECT_GE(exporter2.eventsEmitted(), 1);
    }
}

TEST_F(TracyExporterTest, ExportTraceRecord) {
    TracyExporter exporter(config_);
    exporter.initialize();
    
    TraceRecord record;
    record.metadata().application_name = "TestApp";
    
    TraceEvent event;
    event.type = EventType::KernelLaunch;
    event.name = "test_kernel";
    record.addEvent(event);
    
    exporter.exportTraceRecord(record);
    
    if (isTracyEnabled()) {
        EXPECT_GE(exporter.eventsEmitted(), 1);
    }
}

// =============================================================================
// Tracy Importer Tests
// =============================================================================

class TracyImporterTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(TracyImporterTest, CreateDefaultImporter) {
    TracyImporter importer;
    SUCCEED();
}

TEST_F(TracyImporterTest, CreateWithConfig) {
    TracyImporterConfig config;
    config.import_zones = true;
    config.import_gpu_zones = true;
    TracyImporter importer(config);
    EXPECT_TRUE(importer.config().import_zones);
    EXPECT_TRUE(importer.config().import_gpu_zones);
}

TEST_F(TracyImporterTest, SetConfig) {
    TracyImporter importer;
    
    TracyImporterConfig config;
    config.import_memory = false;
    importer.setConfig(config);
    
    EXPECT_FALSE(importer.config().import_memory);
}

TEST_F(TracyImporterTest, ImportNonExistentFile) {
    TracyImporter importer;
    auto result = importer.importFile("nonexistent.tracy");
    
    EXPECT_FALSE(result.success());
    EXPECT_GT(result.errors.size(), 0);
}

TEST_F(TracyImporterTest, ImportInvalidFile) {
    // Create a temporary invalid file
    const char* temp_file = "/tmp/test_invalid.tracy";
    FILE* f = fopen(temp_file, "wb");
    if (f) {
        const char* invalid_data = "not a tracy file";
        fwrite(invalid_data, 1, strlen(invalid_data), f);
        fclose(f);
        
        TracyImporter importer;
        auto result = importer.importFile(temp_file);
        
        EXPECT_FALSE(result.success());
        
        // Clean up
        remove(temp_file);
    }
}

TEST_F(TracyImporterTest, ConvertZone) {
    TracyZone zone;
    zone.name = "test_zone";
    zone.source_file = "test.cpp";
    zone.function = "testFunction";
    zone.source_line = 42;
    zone.start_time = 1000000;
    zone.end_time = 2000000;
    zone.thread_id = 1;
    zone.color = 0xFF0000;
    zone.depth = 0;
    zone.is_gpu = false;
    
    TraceEvent event = TracyImporter::convertZone(zone);
    
    EXPECT_EQ(event.name, "test_zone");
    EXPECT_EQ(event.timestamp, 1000000);
    EXPECT_EQ(event.duration, 1000000);
    EXPECT_EQ(event.thread_id, 1);
    EXPECT_EQ(event.metadata["source_file"], "test.cpp");
    EXPECT_EQ(event.metadata["function"], "testFunction");
    EXPECT_EQ(event.metadata["source_line"], "42");
    EXPECT_EQ(event.metadata["source"], "tracy");
}

TEST_F(TracyImporterTest, ConvertGpuZone) {
    // Use the struct from tracy_importer.hpp, not the RAII class from tracy_exporter.hpp
    tracesmith::tracy::TracyGpuZone zone;
    zone.name = "gpu_kernel";
    zone.cpu_start = 1000000;
    zone.cpu_end = 2000000;
    zone.gpu_start = 1100000;
    zone.gpu_end = 1900000;
    zone.context_id = 0;
    zone.thread_id = 1;
    zone.color = 0x00FF00;
    
    TraceEvent event = TracyImporter::convertGpuZone(zone);
    
    EXPECT_EQ(event.type, EventType::KernelLaunch);
    EXPECT_EQ(event.name, "gpu_kernel");
    EXPECT_EQ(event.timestamp, 1100000);
    EXPECT_EQ(event.duration, 800000);
    EXPECT_EQ(event.device_id, 0);
    EXPECT_EQ(event.metadata["source"], "tracy_gpu");
}

TEST_F(TracyImporterTest, ConvertMemoryAllocAlloc) {
    TracyMemoryAlloc alloc;
    alloc.ptr = 0x1000;
    alloc.size = 4096;
    alloc.alloc_time = 1000000;
    alloc.thread_id = 1;
    alloc.pool_name = "test_pool";
    
    MemoryEvent event = TracyImporter::convertMemoryAlloc(alloc, false);
    
    EXPECT_EQ(event.ptr, 0x1000);
    EXPECT_EQ(event.bytes, 4096);
    EXPECT_EQ(event.timestamp, 1000000);
    EXPECT_TRUE(event.is_allocation);
    EXPECT_EQ(event.thread_id, 1);
    EXPECT_EQ(event.allocator_name, "test_pool");
}

TEST_F(TracyImporterTest, ConvertMemoryAllocFree) {
    TracyMemoryAlloc alloc;
    alloc.ptr = 0x1000;
    alloc.size = 4096;
    alloc.free_time = 2000000;
    alloc.thread_id = 1;
    
    MemoryEvent event = TracyImporter::convertMemoryAlloc(alloc, true);
    
    EXPECT_EQ(event.timestamp, 2000000);
    EXPECT_FALSE(event.is_allocation);
}

TEST_F(TracyImporterTest, ConvertPlotPoint) {
    TracyPlotPoint point;
    point.name = "test_plot";
    point.timestamp = 1000000;
    point.value = 42.5;
    point.is_int = false;
    
    CounterEvent event = TracyImporter::convertPlotPoint(point);
    
    EXPECT_EQ(event.counter_name, "test_plot");
    EXPECT_EQ(event.timestamp, 1000000);
    EXPECT_DOUBLE_EQ(event.value, 42.5);
}

TEST_F(TracyImporterTest, ConvertPlotPointInt) {
    TracyPlotPoint point;
    point.name = "int_plot";
    point.timestamp = 1000000;
    point.is_int = true;
    point.int_value = 100;
    
    CounterEvent event = TracyImporter::convertPlotPoint(point);
    
    EXPECT_DOUBLE_EQ(event.value, 100.0);
}

TEST_F(TracyImporterTest, IsTracyFileInvalid) {
    EXPECT_FALSE(isTracyFile("nonexistent.tracy"));
    EXPECT_FALSE(isTracyFile("/tmp"));  // Directory, not file
}

TEST_F(TracyImporterTest, GetTracyFileVersionInvalid) {
    EXPECT_EQ(getTracyFileVersion("nonexistent.tracy"), 0);
}

TEST_F(TracyImporterTest, ProgressCallback) {
    TracyImporter importer;
    
    bool callback_called = false;
    importer.setProgressCallback([&](float progress, const std::string& status) {
        callback_called = true;
        EXPECT_GE(progress, 0.0f);
        EXPECT_LE(progress, 1.0f);
        EXPECT_FALSE(status.empty());
    });
    
    // Import will fail but callback should be called
    importer.importFile("nonexistent.tracy");
}

// =============================================================================
// Tracy GPU Zone RAII Tests
// =============================================================================

class TracyGpuZoneScopeTest : public ::testing::Test {
protected:
    void SetUp() override {
        exporter_.initialize();
    }
    
    void TearDown() override {
        exporter_.shutdown();
    }
    
    TracyExporter exporter_;
};

TEST_F(TracyGpuZoneScopeTest, BasicZone) {
    uint8_t ctx = exporter_.createGpuContext(0, "Test GPU");
    
    {
        TracyGpuZoneScope zone(exporter_, ctx, "test_zone");
        // Zone active
    }
    // Zone destroyed, should emit
    
    if (isTracyEnabled()) {
        EXPECT_EQ(exporter_.gpuZonesEmitted(), 1);
    }
}

TEST_F(TracyGpuZoneScopeTest, ZoneWithColor) {
    uint8_t ctx = exporter_.createGpuContext(0, "Test GPU");
    
    {
        TracyGpuZoneScope zone(exporter_, ctx, "colored_zone", colors::KernelLaunch);
    }
    
    if (isTracyEnabled()) {
        EXPECT_EQ(exporter_.gpuZonesEmitted(), 1);
    }
}

TEST_F(TracyGpuZoneScopeTest, ZoneWithGpuTimestamps) {
    uint8_t ctx = exporter_.createGpuContext(0, "Test GPU");
    
    {
        TracyGpuZoneScope zone(exporter_, ctx, "gpu_zone");
        zone.setGpuTimestamps(getCurrentTimestamp(), getCurrentTimestamp() + 1000000);
    }
    
    if (isTracyEnabled()) {
        EXPECT_EQ(exporter_.gpuZonesEmitted(), 1);
    }
}

// =============================================================================
// Global Exporter Tests
// =============================================================================

TEST(GlobalExporterTest, GetGlobalExporter) {
    auto& exporter = getGlobalTracyExporter();
    // Should be initialized
    if (isTracyEnabled()) {
        EXPECT_TRUE(exporter.isInitialized());
    }
}

TEST(GlobalExporterTest, SetGlobalConfig) {
    TracyExporterConfig config;
    config.gpu_context_name = "Custom GPU";
    setGlobalTracyExporterConfig(config);
    
    // Config should be applied to new global exporter
    // Note: This test may not work if global exporter is already created
    SUCCEED();
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
