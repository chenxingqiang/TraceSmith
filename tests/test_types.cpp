#include <gtest/gtest.h>
#include <tracesmith/types.hpp>

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
