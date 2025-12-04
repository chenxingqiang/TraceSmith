#include <gtest/gtest.h>
#include <tracesmith/common/types.hpp>
#include <tracesmith/state/perfetto_proto_exporter.hpp>
#include <tracesmith/common/xray_importer.hpp>
#include <tracesmith/capture/bpf_types.hpp>
#include <thread>
#include <atomic>

using namespace tracesmith;

TEST(TypesTest, EventTypeToString) {
    EXPECT_STREQ(eventTypeToString(EventType::KernelLaunch), "KernelLaunch");
    EXPECT_STREQ(eventTypeToString(EventType::MemcpyH2D), "MemcpyH2D");
    EXPECT_STREQ(eventTypeToString(EventType::MemcpyD2H), "MemcpyD2H");
    EXPECT_STREQ(eventTypeToString(EventType::StreamSync), "StreamSync");
    EXPECT_STREQ(eventTypeToString(EventType::Unknown), "Unknown");
}

TEST(TypesTest, TraceEventDefault) {
    TraceEvent event;
    
    EXPECT_EQ(event.type, EventType::Unknown);
    EXPECT_EQ(event.timestamp, 0u);
    EXPECT_EQ(event.duration, 0u);
    EXPECT_EQ(event.device_id, 0u);
    EXPECT_EQ(event.stream_id, 0u);
    EXPECT_EQ(event.correlation_id, 0u);
    EXPECT_TRUE(event.name.empty());
    EXPECT_FALSE(event.kernel_params.has_value());
    EXPECT_FALSE(event.memory_params.has_value());
    EXPECT_FALSE(event.call_stack.has_value());
}

TEST(TypesTest, TraceEventWithType) {
    TraceEvent event(EventType::KernelLaunch);
    
    EXPECT_EQ(event.type, EventType::KernelLaunch);
    EXPECT_GT(event.timestamp, 0u);  // Auto-generated timestamp
}

TEST(TypesTest, TraceRecordAddEvent) {
    TraceRecord record;
    
    EXPECT_TRUE(record.empty());
    EXPECT_EQ(record.size(), 0u);
    
    TraceEvent event1(EventType::KernelLaunch);
    event1.name = "kernel1";
    record.addEvent(event1);
    
    EXPECT_FALSE(record.empty());
    EXPECT_EQ(record.size(), 1u);
    
    TraceEvent event2(EventType::MemcpyH2D);
    event2.name = "memcpy1";
    record.addEvent(std::move(event2));
    
    EXPECT_EQ(record.size(), 2u);
}

TEST(TypesTest, TraceRecordFilterByType) {
    TraceRecord record;
    
    for (int i = 0; i < 5; ++i) {
        TraceEvent event(EventType::KernelLaunch);
        event.name = "kernel" + std::to_string(i);
        record.addEvent(event);
    }
    
    for (int i = 0; i < 3; ++i) {
        TraceEvent event(EventType::MemcpyH2D);
        event.name = "memcpy" + std::to_string(i);
        record.addEvent(event);
    }
    
    auto kernels = record.filterByType(EventType::KernelLaunch);
    EXPECT_EQ(kernels.size(), 5u);
    
    auto memcpys = record.filterByType(EventType::MemcpyH2D);
    EXPECT_EQ(memcpys.size(), 3u);
    
    auto syncs = record.filterByType(EventType::StreamSync);
    EXPECT_EQ(syncs.size(), 0u);
}

TEST(TypesTest, TraceRecordFilterByStream) {
    TraceRecord record;
    
    for (int i = 0; i < 10; ++i) {
        TraceEvent event(EventType::KernelLaunch);
        event.stream_id = i % 3;
        record.addEvent(event);
    }
    
    auto stream0 = record.filterByStream(0);
    auto stream1 = record.filterByStream(1);
    auto stream2 = record.filterByStream(2);
    
    EXPECT_EQ(stream0.size(), 4u);  // i = 0, 3, 6, 9
    EXPECT_EQ(stream1.size(), 3u);  // i = 1, 4, 7
    EXPECT_EQ(stream2.size(), 3u);  // i = 2, 5, 8
}

TEST(TypesTest, TraceRecordFilterByDevice) {
    TraceRecord record;
    
    for (int i = 0; i < 10; ++i) {
        TraceEvent event(EventType::KernelLaunch);
        event.device_id = i % 2;
        record.addEvent(event);
    }
    
    auto device0 = record.filterByDevice(0);
    auto device1 = record.filterByDevice(1);
    
    EXPECT_EQ(device0.size(), 5u);
    EXPECT_EQ(device1.size(), 5u);
}

TEST(TypesTest, TraceRecordSortByTimestamp) {
    TraceRecord record;
    
    // Add events out of order
    TraceEvent event3(EventType::KernelLaunch, 3000);
    TraceEvent event1(EventType::KernelLaunch, 1000);
    TraceEvent event2(EventType::KernelLaunch, 2000);
    
    record.addEvent(event3);
    record.addEvent(event1);
    record.addEvent(event2);
    
    record.sortByTimestamp();
    
    const auto& events = record.events();
    EXPECT_EQ(events[0].timestamp, 1000u);
    EXPECT_EQ(events[1].timestamp, 2000u);
    EXPECT_EQ(events[2].timestamp, 3000u);
}

TEST(TypesTest, DeviceInfoDefault) {
    DeviceInfo info;
    
    EXPECT_EQ(info.device_id, 0u);
    EXPECT_TRUE(info.name.empty());
    EXPECT_EQ(info.compute_major, 0u);
    EXPECT_EQ(info.compute_minor, 0u);
    EXPECT_EQ(info.warp_size, 32u);  // Default warp size
}

TEST(TypesTest, KernelParamsDefault) {
    KernelParams params;
    
    EXPECT_EQ(params.grid_x, 0u);
    EXPECT_EQ(params.grid_y, 0u);
    EXPECT_EQ(params.grid_z, 0u);
    EXPECT_EQ(params.block_x, 0u);
    EXPECT_EQ(params.block_y, 0u);
    EXPECT_EQ(params.block_z, 0u);
    EXPECT_EQ(params.shared_mem_bytes, 0u);
    EXPECT_EQ(params.registers_per_thread, 0u);
}

TEST(TypesTest, CallStackEmpty) {
    CallStack cs;
    
    EXPECT_TRUE(cs.empty());
    EXPECT_EQ(cs.depth(), 0u);
    
    cs.frames.push_back(StackFrame(0x12345678));
    
    EXPECT_FALSE(cs.empty());
    EXPECT_EQ(cs.depth(), 1u);
}

TEST(TypesTest, GetCurrentTimestamp) {
    auto ts1 = getCurrentTimestamp();
    auto ts2 = getCurrentTimestamp();
    
    EXPECT_GT(ts1, 0u);
    EXPECT_GE(ts2, ts1);
}

// ============================================================
// Kineto Schema Tests
// ============================================================

TEST(KinetoSchemaTest, TraceEventThreadId) {
    TraceEvent event(EventType::KernelLaunch);
    
    // Default thread_id should be 0
    EXPECT_EQ(event.thread_id, 0u);
    
    // Set thread_id
    event.thread_id = 12345;
    EXPECT_EQ(event.thread_id, 12345u);
}

TEST(KinetoSchemaTest, TraceEventMetadata) {
    TraceEvent event(EventType::KernelLaunch);
    
    // Default metadata should be empty
    EXPECT_TRUE(event.metadata.empty());
    
    // Add metadata
    event.metadata["operator"] = "aten::add";
    event.metadata["input_shape"] = "[256, 256]";
    event.metadata["flops"] = "131072";
    
    EXPECT_EQ(event.metadata.size(), 3u);
    EXPECT_EQ(event.metadata["operator"], "aten::add");
    EXPECT_EQ(event.metadata["input_shape"], "[256, 256]");
    EXPECT_EQ(event.metadata["flops"], "131072");
}

TEST(KinetoSchemaTest, FlowInfoDefault) {
    FlowInfo flow;
    
    EXPECT_EQ(flow.id, 0u);
    EXPECT_EQ(flow.type, FlowType::None);
    EXPECT_FALSE(flow.is_start);
}

TEST(KinetoSchemaTest, FlowInfoConstruct) {
    FlowInfo flow(42, FlowType::FwdBwd, true);
    
    EXPECT_EQ(flow.id, 42u);
    EXPECT_EQ(flow.type, FlowType::FwdBwd);
    EXPECT_TRUE(flow.is_start);
}

TEST(KinetoSchemaTest, TraceEventFlowInfo) {
    TraceEvent event(EventType::KernelLaunch);
    
    // Default flow_info
    EXPECT_EQ(event.flow_info.id, 0u);
    EXPECT_EQ(event.flow_info.type, FlowType::None);
    
    // Set flow info
    event.flow_info = FlowInfo(100, FlowType::AsyncCpuGpu, true);
    
    EXPECT_EQ(event.flow_info.id, 100u);
    EXPECT_EQ(event.flow_info.type, FlowType::AsyncCpuGpu);
    EXPECT_TRUE(event.flow_info.is_start);
}

TEST(KinetoSchemaTest, KinetoCompatibleEvent) {
    // Create a fully-populated Kineto-compatible event
    TraceEvent event(EventType::KernelLaunch);
    event.name = "vectorAdd";
    event.timestamp = 1000000;
    event.duration = 500000;
    event.device_id = 0;
    event.stream_id = 1;
    event.correlation_id = 42;
    
    // Kineto-specific fields
    event.thread_id = 98765;
    event.metadata["operator"] = "aten::add";
    event.metadata["input_shape"] = "[1024, 1024]";
    event.flow_info = FlowInfo(42, FlowType::FwdBwd, true);
    
    // Verify all fields
    EXPECT_EQ(event.type, EventType::KernelLaunch);
    EXPECT_EQ(event.name, "vectorAdd");
    EXPECT_EQ(event.thread_id, 98765u);
    EXPECT_EQ(event.metadata.size(), 2u);
    EXPECT_EQ(event.flow_info.id, 42u);
}

TEST(KinetoSchemaTest, FlowTypeEnum) {
    EXPECT_EQ(static_cast<uint8_t>(FlowType::None), 0);
    EXPECT_EQ(static_cast<uint8_t>(FlowType::FwdBwd), 1);
    EXPECT_EQ(static_cast<uint8_t>(FlowType::AsyncCpuGpu), 2);
    EXPECT_EQ(static_cast<uint8_t>(FlowType::Custom), 255);
}

// ============================================================
// Kineto v0.2.0 Extended Types Tests
// ============================================================

TEST(KinetoV2Test, MemoryEventDefault) {
    MemoryEvent event;
    
    EXPECT_EQ(event.timestamp, 0u);
    EXPECT_EQ(event.device_id, 0u);
    EXPECT_EQ(event.thread_id, 0u);
    EXPECT_EQ(event.bytes, 0u);
    EXPECT_EQ(event.ptr, 0u);
    EXPECT_TRUE(event.is_allocation);
    EXPECT_TRUE(event.allocator_name.empty());
    EXPECT_EQ(event.category, MemoryEvent::Category::Unknown);
}

TEST(KinetoV2Test, MemoryEventAllocation) {
    MemoryEvent event;
    event.timestamp = 1000000;
    event.device_id = 0;
    event.thread_id = 12345;
    event.bytes = 4 * 1024 * 1024;  // 4MB
    event.ptr = 0x7f0000000000;
    event.is_allocation = true;
    event.allocator_name = "pytorch_caching";
    event.category = MemoryEvent::Category::Activation;
    
    EXPECT_EQ(event.bytes, 4 * 1024 * 1024u);
    EXPECT_EQ(event.allocator_name, "pytorch_caching");
    EXPECT_EQ(event.category, MemoryEvent::Category::Activation);
}

TEST(KinetoV2Test, MemoryEventCategories) {
    EXPECT_EQ(static_cast<uint8_t>(MemoryEvent::Category::Unknown), 0);
    EXPECT_EQ(static_cast<uint8_t>(MemoryEvent::Category::Activation), 1);
    EXPECT_EQ(static_cast<uint8_t>(MemoryEvent::Category::Gradient), 2);
    EXPECT_EQ(static_cast<uint8_t>(MemoryEvent::Category::Parameter), 3);
    EXPECT_EQ(static_cast<uint8_t>(MemoryEvent::Category::Temporary), 4);
    EXPECT_EQ(static_cast<uint8_t>(MemoryEvent::Category::Cached), 5);
}

TEST(KinetoV2Test, CounterEventDefault) {
    CounterEvent event;
    
    EXPECT_EQ(event.timestamp, 0u);
    EXPECT_EQ(event.device_id, 0u);
    EXPECT_EQ(event.track_id, 0u);
    EXPECT_TRUE(event.counter_name.empty());
    EXPECT_DOUBLE_EQ(event.value, 0.0);
    EXPECT_TRUE(event.unit.empty());
}

TEST(KinetoV2Test, CounterEventConstruct) {
    CounterEvent event("GPU Memory Bandwidth", 450.5, 1000000);
    
    EXPECT_EQ(event.counter_name, "GPU Memory Bandwidth");
    EXPECT_DOUBLE_EQ(event.value, 450.5);
    EXPECT_EQ(event.timestamp, 1000000u);
}

TEST(KinetoV2Test, CounterEventWithUnit) {
    CounterEvent event;
    event.counter_name = "SM Occupancy";
    event.value = 85.5;
    event.unit = "%";
    event.device_id = 0;
    event.track_id = 1;
    
    EXPECT_EQ(event.counter_name, "SM Occupancy");
    EXPECT_DOUBLE_EQ(event.value, 85.5);
    EXPECT_EQ(event.unit, "%");
}

// ============================================================
// TracingSession Tests (v0.3.0)
// ============================================================

TEST(TracingSessionTest, DefaultConstruction) {
    TracingSession session;
    
    EXPECT_EQ(session.getState(), TracingSession::State::Stopped);
    EXPECT_FALSE(session.isActive());
    EXPECT_EQ(session.eventBufferCapacity(), 65536u);
}

TEST(TracingSessionTest, CustomBufferSize) {
    TracingSession session(1024, 256);
    
    // Buffer size is rounded to power of 2
    EXPECT_GE(session.eventBufferCapacity(), 1024u);
}

TEST(TracingSessionTest, StartStop) {
    TracingSession session;
    TracingConfig config;
    
    EXPECT_TRUE(session.start(config));
    EXPECT_EQ(session.getState(), TracingSession::State::Running);
    EXPECT_TRUE(session.isActive());
    
    session.stop();
    EXPECT_EQ(session.getState(), TracingSession::State::Stopped);
    EXPECT_FALSE(session.isActive());
}

TEST(TracingSessionTest, DoubleStartFails) {
    TracingSession session;
    TracingConfig config;
    
    EXPECT_TRUE(session.start(config));
    EXPECT_FALSE(session.start(config));  // Second start should fail
    
    session.stop();
}

TEST(TracingSessionTest, EmitEvents) {
    TracingSession session;
    TracingConfig config;
    session.start(config);
    
    // Emit some events
    for (int i = 0; i < 100; ++i) {
        TraceEvent event(EventType::KernelLaunch);
        event.name = "kernel_" + std::to_string(i);
        event.device_id = 0;
        event.stream_id = i % 4;
        EXPECT_TRUE(session.emit(std::move(event)));
    }
    
    session.stop();
    
    const auto& stats = session.getStatistics();
    EXPECT_EQ(stats.events_emitted, 100u);
    EXPECT_EQ(stats.events_dropped, 0u);
    
    const auto& events = session.getEvents();
    EXPECT_EQ(events.size(), 100u);
}

TEST(TracingSessionTest, EmitCounters) {
    TracingSession session;
    TracingConfig config;
    session.start(config);
    
    // Emit counters
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(session.emitCounter("GPU Bandwidth", 400.0 + i));
    }
    
    session.stop();
    
    const auto& stats = session.getStatistics();
    EXPECT_EQ(stats.counters_emitted, 50u);
    
    const auto& counters = session.getCounters();
    EXPECT_EQ(counters.size(), 50u);
}

TEST(TracingSessionTest, EmitWhileStopped) {
    TracingSession session;
    
    TraceEvent event(EventType::KernelLaunch);
    EXPECT_FALSE(session.emit(event));
    EXPECT_FALSE(session.emitCounter("test", 1.0));
}

TEST(TracingSessionTest, Statistics) {
    TracingSession session;
    TracingConfig config;
    
    session.start(config);
    
    for (int i = 0; i < 10; ++i) {
        TraceEvent event(EventType::KernelLaunch);
        session.emit(std::move(event));
        session.emitCounter("metric", static_cast<double>(i));
    }
    
    session.stop();
    
    const auto& stats = session.getStatistics();
    EXPECT_EQ(stats.events_emitted, 10u);
    EXPECT_EQ(stats.counters_emitted, 10u);
    EXPECT_GT(stats.start_time, 0u);
    EXPECT_GT(stats.stop_time, stats.start_time);
    EXPECT_GE(stats.duration_ms(), 0.0);
}

TEST(TracingSessionTest, Clear) {
    TracingSession session;
    TracingConfig config;
    
    session.start(config);
    session.emit(TraceEvent(EventType::KernelLaunch));
    session.stop();
    
    EXPECT_EQ(session.getEvents().size(), 1u);
    
    session.clear();
    
    EXPECT_EQ(session.getEvents().size(), 0u);
    EXPECT_EQ(session.getStatistics().events_emitted, 0u);
}

TEST(TracingSessionTest, ThreadSafetyBasic) {
    TracingSession session(8192);
    TracingConfig config;
    session.start(config);
    
    std::atomic<int> total_emitted{0};
    const int events_per_thread = 100;
    
    // Note: RingBuffer is SPSC, so we test with single producer
    std::thread producer([&]() {
        for (int i = 0; i < events_per_thread; ++i) {
            TraceEvent event(EventType::KernelLaunch);
            event.name = "thread_event_" + std::to_string(i);
            if (session.emit(std::move(event))) {
                total_emitted++;
            }
        }
    });
    
    producer.join();
    session.stop();
    
    EXPECT_EQ(total_emitted.load(), events_per_thread);
    EXPECT_EQ(session.getEvents().size(), static_cast<size_t>(events_per_thread));
}

// ============================================================
// XRay Importer Tests (v0.4.0)
// ============================================================

TEST(XRayImporterTest, DefaultConstruction) {
    XRayImporter importer;
    
    EXPECT_TRUE(XRayImporter::isAvailable());
    EXPECT_EQ(importer.getStatistics().records_read, 0u);
}

TEST(XRayImporterTest, ConfigSettings) {
    XRayImporter::Config config;
    config.resolve_symbols = true;
    config.filter_short_calls = true;
    config.min_duration_ns = 1000;
    config.symbol_file = "/path/to/symbols";
    
    XRayImporter importer(config);
    importer.setSymbolFile("/new/path");
    
    // Just verify no crash
    EXPECT_TRUE(true);
}

TEST(XRayImporterTest, EmptyFile) {
    XRayImporter importer;
    auto events = importer.importFile("/nonexistent/file.xray");
    
    EXPECT_TRUE(events.empty());
}

TEST(XRayImporterTest, XRayFunctionRecord) {
    XRayFunctionRecord record;
    
    EXPECT_EQ(record.function_id, 0u);
    EXPECT_EQ(record.timestamp, 0u);
    EXPECT_EQ(record.type, XRayEntryType::FunctionEnter);
    EXPECT_EQ(record.thread_id, 0u);
    EXPECT_TRUE(record.function_name.empty());
}

TEST(XRayImporterTest, XRayEntryTypes) {
    EXPECT_EQ(static_cast<uint8_t>(XRayEntryType::FunctionEnter), 0);
    EXPECT_EQ(static_cast<uint8_t>(XRayEntryType::FunctionExit), 1);
    EXPECT_EQ(static_cast<uint8_t>(XRayEntryType::TailExit), 2);
    EXPECT_EQ(static_cast<uint8_t>(XRayEntryType::CustomEvent), 3);
    EXPECT_EQ(static_cast<uint8_t>(XRayEntryType::TypedEvent), 4);
}

// ============================================================
// BPF Types Tests (v0.4.0)
// ============================================================

TEST(BPFTypesTest, EventTypeToString) {
    EXPECT_STREQ(bpfEventTypeToString(BPFEventType::CudaLaunchKernel), "cuda_launch_kernel");
    EXPECT_STREQ(bpfEventTypeToString(BPFEventType::CudaMemcpy), "cuda_memcpy");
    EXPECT_STREQ(bpfEventTypeToString(BPFEventType::UvmFault), "uvm_fault");
    EXPECT_STREQ(bpfEventTypeToString(BPFEventType::HipLaunchKernel), "hip_launch_kernel");
    EXPECT_STREQ(bpfEventTypeToString(BPFEventType::Unknown), "unknown");
}

TEST(BPFTypesTest, BPFEventRecord) {
    BPFEventRecord record;
    
    EXPECT_EQ(record.timestamp_ns, 0u);
    EXPECT_EQ(record.pid, 0u);
    EXPECT_EQ(record.tid, 0u);
    EXPECT_EQ(record.cpu, 0u);
    EXPECT_EQ(record.type, BPFEventType::Unknown);
}

TEST(BPFTypesTest, BPFEventToTraceEvent) {
    BPFEventRecord bpf_event;
    bpf_event.timestamp_ns = 1000000;
    bpf_event.pid = 1234;
    bpf_event.tid = 5678;
    bpf_event.cpu = 0;
    bpf_event.type = BPFEventType::CudaLaunchKernel;
    strncpy(bpf_event.data.kernel.kernel_name, "test_kernel", 64);
    bpf_event.data.kernel.grid_x = 256;
    bpf_event.data.kernel.grid_y = 1;
    bpf_event.data.kernel.grid_z = 1;
    bpf_event.data.kernel.block_x = 128;
    bpf_event.data.kernel.block_y = 1;
    bpf_event.data.kernel.block_z = 1;
    
    TraceEvent event = bpfEventToTraceEvent(bpf_event);
    
    EXPECT_EQ(event.type, EventType::KernelLaunch);
    EXPECT_EQ(event.timestamp, 1000000u);
    EXPECT_EQ(event.thread_id, 5678u);
    EXPECT_EQ(event.name, "test_kernel");
    EXPECT_TRUE(event.kernel_params.has_value());
    EXPECT_EQ(event.kernel_params->grid_x, 256u);
    EXPECT_EQ(event.kernel_params->block_x, 128u);
}

TEST(BPFTypesTest, BPFMemcpyEventConversion) {
    BPFEventRecord bpf_event;
    bpf_event.type = BPFEventType::CudaMemcpy;
    bpf_event.data.memop.direction = 0;  // H2D
    bpf_event.data.memop.size = 1024 * 1024;
    bpf_event.data.memop.src_addr = 0x7fff0000;
    bpf_event.data.memop.dst_addr = 0xb0000000;
    
    TraceEvent event = bpfEventToTraceEvent(bpf_event);
    
    EXPECT_EQ(event.type, EventType::MemcpyH2D);
    EXPECT_TRUE(event.memory_params.has_value());
    EXPECT_EQ(event.memory_params->size_bytes, 1024u * 1024u);
}

TEST(BPFTypesTest, BPFTracerAvailability) {
    // BPF is only available on Linux
#ifdef __linux__
    // On Linux, check if available (may still return false without privileges)
    bool available = BPFTracer::isAvailable();
    (void)available;  // Just verify no crash
#else
    EXPECT_FALSE(BPFTracer::isAvailable());
#endif
}

TEST(BPFTypesTest, BPFProgramInfo) {
    BPFProgramInfo info;
    
    EXPECT_TRUE(info.name.empty());
    EXPECT_EQ(info.id, 0u);
    EXPECT_FALSE(info.loaded);
    EXPECT_FALSE(info.attached);
    EXPECT_TRUE(info.kprobes.empty());
}

// ============================================================
// Frame Capture Tests (v0.5.0 - RenderDoc-inspired)
// ============================================================

#include "tracesmith/replay/frame_capture.hpp"

TEST(FrameCaptureTest, DefaultConstruction) {
    FrameCapture capture;
    
    EXPECT_EQ(capture.getState(), CaptureState::Idle);
    EXPECT_FALSE(capture.isCapturing());
    EXPECT_TRUE(capture.getCapturedFrames().empty());
}

TEST(FrameCaptureTest, ConfigSettings) {
    FrameCaptureConfig config;
    config.capture_buffer_contents = true;
    config.frames_to_capture = 5;
    
    FrameCapture capture(config);
    EXPECT_EQ(capture.getConfig().frames_to_capture, 5u);
    EXPECT_TRUE(capture.getConfig().capture_buffer_contents);
}

TEST(FrameCaptureTest, TriggerCapture) {
    FrameCapture capture;
    
    EXPECT_EQ(capture.getState(), CaptureState::Idle);
    
    capture.triggerCapture();
    EXPECT_EQ(capture.getState(), CaptureState::Armed);
    
    // First frame end starts capture
    capture.onFrameEnd();
    EXPECT_EQ(capture.getState(), CaptureState::Capturing);
    EXPECT_TRUE(capture.isCapturing());
}

TEST(FrameCaptureTest, CaptureFrame) {
    FrameCaptureConfig config;
    config.frames_to_capture = 1;
    FrameCapture capture(config);
    
    capture.triggerCapture();
    capture.onFrameEnd();  // Start capture
    
    // Record some events
    TraceEvent kernel;
    kernel.type = EventType::KernelLaunch;
    kernel.name = "test_kernel";
    capture.recordEvent(kernel);
    
    TraceEvent memcpy_event;
    memcpy_event.type = EventType::MemcpyH2D;
    memcpy_event.name = "memcpy";
    capture.recordEvent(memcpy_event);
    
    capture.onFrameEnd();  // End capture
    
    EXPECT_EQ(capture.getState(), CaptureState::Complete);
    EXPECT_EQ(capture.getCapturedFrames().size(), 1u);
    
    const auto& frame = capture.getCapturedFrames()[0];
    EXPECT_EQ(frame.events.size(), 2u);
}

TEST(FrameCaptureTest, DrawCallRecording) {
    FrameCaptureConfig config;
    config.frames_to_capture = 1;
    FrameCapture capture(config);
    
    capture.triggerCapture();
    capture.onFrameEnd();
    
    DrawCallInfo draw;
    draw.call_id = 1;
    draw.name = "DrawIndexed";
    draw.vertex_count = 3600;
    draw.instance_count = 100;
    capture.recordDrawCall(draw);
    
    capture.onFrameEnd();
    
    const auto& frame = capture.getCapturedFrames()[0];
    EXPECT_EQ(frame.draw_calls.size(), 1u);
    EXPECT_EQ(frame.draw_calls[0].vertex_count, 3600u);
}

TEST(FrameCaptureTest, ResourceTracking) {
    ResourceTracker tracker;
    
    tracker.registerResource(1, ResourceType::Buffer, "VertexBuffer");
    tracker.registerResource(2, ResourceType::Texture2D, "Albedo");
    
    EXPECT_NE(tracker.getResource(1), nullptr);
    EXPECT_EQ(tracker.getResource(1)->type, ResourceType::Buffer);
    EXPECT_EQ(tracker.getResource(2)->name, "Albedo");
    
    tracker.updateResourceBinding(1, 0x1000, 4096);
    EXPECT_EQ(tracker.getResource(1)->address, 0x1000u);
    EXPECT_EQ(tracker.getResource(1)->size, 4096u);
}

TEST(FrameCaptureTest, ResourceModification) {
    ResourceTracker tracker;
    
    tracker.registerResource(1, ResourceType::Buffer);
    Timestamp before = tracker.getResource(1)->last_modified;
    
    // Small delay to ensure different timestamp
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    Timestamp now = getCurrentTimestamp();
    tracker.markModified(1, now);
    
    EXPECT_GE(tracker.getResource(1)->last_modified, before);
}

TEST(FrameCaptureTest, ResourceDestruction) {
    ResourceTracker tracker;
    
    tracker.registerResource(1, ResourceType::Buffer);
    EXPECT_NE(tracker.getResource(1), nullptr);
    
    tracker.destroyResource(1);
    EXPECT_EQ(tracker.getResource(1), nullptr);
}

TEST(FrameCaptureTest, LiveResources) {
    ResourceTracker tracker;
    
    tracker.registerResource(1, ResourceType::Buffer);
    tracker.registerResource(2, ResourceType::Buffer);
    tracker.registerResource(3, ResourceType::Texture2D);
    
    auto live = tracker.getLiveResources();
    EXPECT_EQ(live.size(), 3u);
    
    tracker.destroyResource(2);
    live = tracker.getLiveResources();
    EXPECT_EQ(live.size(), 2u);
}

TEST(FrameCaptureTest, ResourceTypeToString) {
    EXPECT_STREQ(resourceTypeToString(ResourceType::Buffer), "Buffer");
    EXPECT_STREQ(resourceTypeToString(ResourceType::Texture2D), "Texture2D");
    EXPECT_STREQ(resourceTypeToString(ResourceType::Pipeline), "Pipeline");
    EXPECT_STREQ(resourceTypeToString(ResourceType::Unknown), "Unknown");
}

TEST(FrameCaptureTest, CapturedFrameStatistics) {
    FrameCaptureConfig config;
    config.frames_to_capture = 1;
    FrameCapture capture(config);
    
    capture.triggerCapture();
    capture.onFrameEnd();
    
    // Record various events
    for (int i = 0; i < 10; i++) {
        TraceEvent kernel;
        kernel.type = EventType::KernelLaunch;
        capture.recordEvent(kernel);
    }
    
    for (int i = 0; i < 5; i++) {
        TraceEvent memcpy_event;
        memcpy_event.type = EventType::MemcpyH2D;
        capture.recordEvent(memcpy_event);
    }
    
    TraceEvent sync;
    sync.type = EventType::DeviceSync;
    capture.recordEvent(sync);
    
    capture.onFrameEnd();
    
    const auto& frame = capture.getCapturedFrames()[0];
    EXPECT_EQ(frame.total_memory_ops, 5u);
    EXPECT_EQ(frame.total_sync_ops, 1u);
}

TEST(FrameCaptureTest, ClearCapture) {
    FrameCaptureConfig config;
    config.frames_to_capture = 1;
    FrameCapture capture(config);
    
    capture.triggerCapture();
    capture.onFrameEnd();
    capture.onFrameEnd();
    
    EXPECT_EQ(capture.getCapturedFrames().size(), 1u);
    
    capture.clear();
    
    EXPECT_TRUE(capture.getCapturedFrames().empty());
    EXPECT_EQ(capture.getState(), CaptureState::Idle);
}

// ============================================================
// Memory Profiler Tests (v0.6.0)
// ============================================================

#include "tracesmith/capture/memory_profiler.hpp"

TEST(MemoryProfilerTest, DefaultConstruction) {
    MemoryProfiler profiler;
    
    EXPECT_FALSE(profiler.isActive());
    EXPECT_EQ(profiler.getCurrentUsage(), 0u);
    EXPECT_EQ(profiler.getPeakUsage(), 0u);
}

TEST(MemoryProfilerTest, StartStop) {
    MemoryProfiler profiler;
    
    EXPECT_FALSE(profiler.isActive());
    
    profiler.start();
    EXPECT_TRUE(profiler.isActive());
    
    profiler.stop();
    EXPECT_FALSE(profiler.isActive());
}

TEST(MemoryProfilerTest, RecordAllocation) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 4096, 0, "test_allocator");
    
    EXPECT_EQ(profiler.getCurrentUsage(), 4096u);
    EXPECT_EQ(profiler.getPeakUsage(), 4096u);
    EXPECT_EQ(profiler.getLiveAllocationCount(), 1u);
    
    profiler.stop();
}

TEST(MemoryProfilerTest, RecordFree) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 4096, 0);
    EXPECT_EQ(profiler.getCurrentUsage(), 4096u);
    
    profiler.recordFree(0x1000, 0);
    EXPECT_EQ(profiler.getCurrentUsage(), 0u);
    EXPECT_EQ(profiler.getLiveAllocationCount(), 0u);
    
    // Peak should still be 4096
    EXPECT_EQ(profiler.getPeakUsage(), 4096u);
    
    profiler.stop();
}

TEST(MemoryProfilerTest, MultipleAllocations) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 1024, 0);
    profiler.recordAlloc(0x2000, 2048, 0);
    profiler.recordAlloc(0x3000, 4096, 0);
    
    EXPECT_EQ(profiler.getCurrentUsage(), 1024u + 2048u + 4096u);
    EXPECT_EQ(profiler.getLiveAllocationCount(), 3u);
    
    profiler.recordFree(0x2000, 0);
    EXPECT_EQ(profiler.getCurrentUsage(), 1024u + 4096u);
    EXPECT_EQ(profiler.getLiveAllocationCount(), 2u);
    
    profiler.stop();
}

TEST(MemoryProfilerTest, GetLiveAllocations) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 1024, 0, "alloc1", "tag1");
    profiler.recordAlloc(0x2000, 2048, 1, "alloc2", "tag2");
    
    auto live = profiler.getLiveAllocations();
    EXPECT_EQ(live.size(), 2u);
    
    profiler.stop();
}

TEST(MemoryProfilerTest, TakeSnapshot) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 1024, 0);
    profiler.recordAlloc(0x2000, 2048, 0);
    
    auto snapshot = profiler.takeSnapshot();
    
    EXPECT_EQ(snapshot.live_bytes, 1024u + 2048u);
    EXPECT_EQ(snapshot.live_allocations, 2u);
    EXPECT_GT(snapshot.timestamp, 0u);
    
    profiler.stop();
}

TEST(MemoryProfilerTest, GenerateReport) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 1024, 0, "cuda");
    profiler.recordAlloc(0x2000, 2048, 0, "cuda");
    profiler.recordFree(0x1000, 0);
    
    profiler.stop();
    
    auto report = profiler.generateReport();
    
    EXPECT_EQ(report.total_allocations, 2u);
    EXPECT_EQ(report.total_frees, 1u);
    EXPECT_EQ(report.total_bytes_allocated, 1024u + 2048u);
    EXPECT_EQ(report.total_bytes_freed, 1024u);
    EXPECT_EQ(report.current_memory_usage, 2048u);
    EXPECT_EQ(report.peak_memory_usage, 1024u + 2048u);
}

TEST(MemoryProfilerTest, Clear) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 1024, 0);
    EXPECT_EQ(profiler.getCurrentUsage(), 1024u);
    
    profiler.clear();
    
    EXPECT_EQ(profiler.getCurrentUsage(), 0u);
    EXPECT_EQ(profiler.getPeakUsage(), 0u);
    EXPECT_EQ(profiler.getLiveAllocationCount(), 0u);
}

TEST(MemoryProfilerTest, ToCounterEvents) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 1024 * 1024, 0);
    
    profiler.stop();
    
    auto counters = profiler.toCounterEvents();
    
    // Should have at least start and end snapshots
    EXPECT_GE(counters.size(), 2u);
}

TEST(MemoryProfilerTest, FormatBytes) {
    EXPECT_EQ(formatBytes(512), "512.00 B");
    EXPECT_EQ(formatBytes(1024), "1.00 KB");
    EXPECT_EQ(formatBytes(1024 * 1024), "1.00 MB");
    EXPECT_EQ(formatBytes(1024 * 1024 * 1024), "1.00 GB");
}

TEST(MemoryProfilerTest, ReportSummary) {
    MemoryProfiler profiler;
    profiler.start();
    
    profiler.recordAlloc(0x1000, 1024 * 1024, 0, "cuda");
    profiler.stop();
    
    auto report = profiler.generateReport();
    auto summary = report.summary();
    
    EXPECT_FALSE(summary.empty());
    EXPECT_NE(summary.find("GPU Memory Profiler Report"), std::string::npos);
}
