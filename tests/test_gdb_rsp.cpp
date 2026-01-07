/**
 * @file test_gdb_rsp.cpp
 * @brief Unit tests for GDB RSP protocol implementation
 */

#include <gtest/gtest.h>
#include <tracesmith/gdb/rsp_packet.hpp>
#include <tracesmith/gdb/gdb_types.hpp>
#include <tracesmith/common/types.hpp>
#include <vector>
#include <string>

using namespace tracesmith;
using namespace tracesmith::gdb;

// Helper function to check string prefix (C++17 compatible)
static bool startsWith(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

// ============================================================
// RSP Packet Encoding Tests
// ============================================================

TEST(RSPPacketTest, EncodeBasic) {
    std::string result = RSPPacket::encode("OK");
    EXPECT_EQ(result, "$OK#9a");
}

TEST(RSPPacketTest, EncodeEmpty) {
    std::string result = RSPPacket::encode("");
    EXPECT_EQ(result, "$#00");
}

TEST(RSPPacketTest, EncodeSpecialChars) {
    std::string result = RSPPacket::encode("test$value#end");
    EXPECT_NE(result.find('}'), std::string::npos);
}

TEST(RSPPacketTest, EncodeLongData) {
    std::string data = "g0000000000000000";
    std::string result = RSPPacket::encode(data);
    EXPECT_TRUE(startsWith(result, "$"));
    EXPECT_NE(result.find('#'), std::string::npos);
    EXPECT_EQ(result.size(), data.size() + 4);
}

// ============================================================
// RSP Packet Decoding Tests
// ============================================================

TEST(RSPPacketTest, DecodeBasic) {
    auto result = RSPPacket::decode("$OK#9a");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "OK");
}

TEST(RSPPacketTest, DecodeEmpty) {
    auto result = RSPPacket::decode("$#00");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "");
}

TEST(RSPPacketTest, DecodeInvalidNoStart) {
    auto result = RSPPacket::decode("OK#9a");
    EXPECT_FALSE(result.has_value());
}

TEST(RSPPacketTest, DecodeInvalidNoHash) {
    auto result = RSPPacket::decode("$OK9a");
    EXPECT_FALSE(result.has_value());
}

TEST(RSPPacketTest, DecodeInvalidTooShort) {
    auto result = RSPPacket::decode("$#0");
    EXPECT_FALSE(result.has_value());
}

TEST(RSPPacketTest, DecodeChecksumMismatch) {
    auto result = RSPPacket::decode("$OK#00");
    EXPECT_FALSE(result.has_value());
}

// ============================================================
// Checksum Tests
// ============================================================

TEST(RSPPacketTest, ChecksumEmpty) {
    uint8_t cs = RSPPacket::checksum("");
    EXPECT_EQ(cs, 0x00);
}

TEST(RSPPacketTest, ChecksumOK) {
    uint8_t cs = RSPPacket::checksum("OK");
    EXPECT_EQ(cs, 0x9a);
}

// ============================================================
// Hex Conversion Tests
// ============================================================

TEST(RSPPacketTest, ToHexBytes) {
    std::vector<uint8_t> data = {0x12, 0x34, 0xab, 0xcd};
    std::string hex = RSPPacket::toHex(data);
    EXPECT_EQ(hex, "1234abcd");
}

TEST(RSPPacketTest, ToHexBytesEmpty) {
    std::vector<uint8_t> data;
    std::string hex = RSPPacket::toHex(data);
    EXPECT_EQ(hex, "");
}

TEST(RSPPacketTest, ToHexString) {
    std::string str = "Hello";
    std::string hex = RSPPacket::toHex(str);
    EXPECT_EQ(hex, "48656c6c6f");
}

TEST(RSPPacketTest, FromHex) {
    std::string hex = "1234abcd";
    std::vector<uint8_t> data = RSPPacket::fromHex(hex);
    ASSERT_EQ(data.size(), 4u);
    EXPECT_EQ(data[0], 0x12);
    EXPECT_EQ(data[1], 0x34);
    EXPECT_EQ(data[2], 0xab);
    EXPECT_EQ(data[3], 0xcd);
}

TEST(RSPPacketTest, FromHexEmpty) {
    std::vector<uint8_t> data = RSPPacket::fromHex("");
    EXPECT_TRUE(data.empty());
}

TEST(RSPPacketTest, HexToUint64) {
    uint64_t val = RSPPacket::hexToUint64("deadbeef");
    EXPECT_EQ(val, 0xdeadbeef);
}

// ============================================================
// Packet Type Parsing Tests
// ============================================================

TEST(RSPPacketTest, ParseTypeReadRegisters) {
    EXPECT_EQ(RSPPacket::parseType("g"), RSPPacketType::ReadRegisters);
}

TEST(RSPPacketTest, ParseTypeWriteRegisters) {
    EXPECT_EQ(RSPPacket::parseType("G0011223344"), RSPPacketType::WriteRegisters);
}

TEST(RSPPacketTest, ParseTypeReadMemory) {
    EXPECT_EQ(RSPPacket::parseType("m1000,100"), RSPPacketType::ReadMemory);
}

TEST(RSPPacketTest, ParseTypeContinue) {
    EXPECT_EQ(RSPPacket::parseType("c"), RSPPacketType::Continue);
}

TEST(RSPPacketTest, ParseTypeStep) {
    EXPECT_EQ(RSPPacket::parseType("s"), RSPPacketType::Step);
}

TEST(RSPPacketTest, ParseTypeKill) {
    EXPECT_EQ(RSPPacket::parseType("k"), RSPPacketType::Kill);
}

TEST(RSPPacketTest, ParseTypeQuery) {
    EXPECT_EQ(RSPPacket::parseType("qSupported"), RSPPacketType::Query);
}

// ============================================================
// Response Encoding Tests
// ============================================================

TEST(RSPPacketTest, ResponseOK) {
    std::string resp = RSPPacket::ok();
    auto decoded = RSPPacket::decode(resp);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), "OK");
}

TEST(RSPPacketTest, ResponseError) {
    std::string resp = RSPPacket::error(1);
    auto decoded = RSPPacket::decode(resp);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), "E01");
}

TEST(RSPPacketTest, ResponseStopReply) {
    std::string resp = RSPPacket::stopReply(5);
    auto decoded = RSPPacket::decode(resp);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), "S05");
}

TEST(RSPPacketTest, ResponseStopReplyThread) {
    std::string resp = RSPPacket::stopReplyThread(5, 1234);
    auto decoded = RSPPacket::decode(resp);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(startsWith(decoded.value(), "T05"));
    EXPECT_NE(decoded.value().find("thread:"), std::string::npos);
}

TEST(RSPPacketTest, ResponseExitReply) {
    std::string resp = RSPPacket::exitReply(0);
    auto decoded = RSPPacket::decode(resp);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), "W00");
}

// ============================================================
// RSP Query Parser Tests
// ============================================================

TEST(RSPQueryTest, ParseSimple) {
    RSPQuery q = RSPQuery::parse("Supported");
    EXPECT_EQ(q.name, "Supported");
    EXPECT_TRUE(q.args.empty());
}

TEST(RSPQueryTest, ParseWithArgs) {
    RSPQuery q = RSPQuery::parse("Xfer:features:read:target.xml:0,1000");
    EXPECT_EQ(q.name, "Xfer");
    ASSERT_EQ(q.args.size(), 4u);
    EXPECT_EQ(q.args[0], "features");
    EXPECT_EQ(q.args[1], "read");
}

// ============================================================
// GDB Types Tests
// ============================================================

TEST(GDBTypesTest, GPUBreakpointDefault) {
    GPUBreakpoint bp;
    EXPECT_EQ(bp.id, -1);
    EXPECT_EQ(bp.type, GPUBreakpointType::KernelLaunch);
    EXPECT_TRUE(bp.kernel_pattern.empty());
    EXPECT_EQ(bp.device_id, -1);
    EXPECT_TRUE(bp.enabled);
    EXPECT_EQ(bp.hit_count, 0u);
}

TEST(GDBTypesTest, GPUBreakpointMatchesExact) {
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    bp.kernel_pattern = "matmul_kernel";
    
    TraceEvent event(EventType::KernelLaunch);
    event.name = "matmul_kernel";
    EXPECT_TRUE(bp.matches(event));
    
    event.name = "other_kernel";
    EXPECT_FALSE(bp.matches(event));
}

TEST(GDBTypesTest, GPUBreakpointMatchesWildcard) {
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    bp.kernel_pattern = "matmul*";
    
    TraceEvent event(EventType::KernelLaunch);
    event.name = "matmul_f32";
    EXPECT_TRUE(bp.matches(event));
    
    event.name = "conv2d";
    EXPECT_FALSE(bp.matches(event));
}

TEST(GDBTypesTest, GPUBreakpointMatchesDevice) {
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    bp.device_id = 0;
    
    TraceEvent event(EventType::KernelLaunch);
    event.name = "kernel";
    event.device_id = 0;
    EXPECT_TRUE(bp.matches(event));
    
    event.device_id = 1;
    EXPECT_FALSE(bp.matches(event));
}

TEST(GDBTypesTest, GPUBreakpointDisabledNoMatch) {
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::KernelLaunch;
    bp.enabled = false;
    
    TraceEvent event(EventType::KernelLaunch);
    event.name = "kernel";
    EXPECT_FALSE(bp.matches(event));
}

TEST(GDBTypesTest, GPUBreakpointMatchesMemcpy) {
    GPUBreakpoint bp;
    bp.type = GPUBreakpointType::MemcpyH2D;
    
    TraceEvent event;
    event.type = EventType::MemcpyH2D;
    EXPECT_TRUE(bp.matches(event));
    
    event.type = EventType::MemcpyD2H;
    EXPECT_FALSE(bp.matches(event));
}

TEST(GDBTypesTest, KernelCallInfoDuration) {
    KernelCallInfo info;
    info.launch_time = 1000;
    info.complete_time = 1500;
    
    EXPECT_TRUE(info.isComplete());
    EXPECT_EQ(info.duration(), 500u);
}

TEST(GDBTypesTest, KernelCallInfoRunning) {
    KernelCallInfo info;
    info.launch_time = 1000;
    info.complete_time = 0;
    
    EXPECT_FALSE(info.isComplete());
    EXPECT_EQ(info.duration(), 0u);
}

TEST(GDBTypesTest, StopEventDescription) {
    StopEvent event;
    event.reason = StopReason::Breakpoint;
    event.pc = 0x401234;
    
    std::string desc = event.description();
    EXPECT_FALSE(desc.empty());
}

TEST(GDBTypesTest, StopEventGPU) {
    StopEvent event;
    event.reason = StopReason::GPUBreakpoint;
    
    TraceEvent gpu_event(EventType::KernelLaunch);
    gpu_event.name = "test_kernel";
    event.gpu_event = gpu_event;
    
    GPUBreakpoint bp;
    bp.id = 1;
    bp.type = GPUBreakpointType::KernelLaunch;
    event.gpu_breakpoint = bp;
    
    std::string desc = event.description();
    EXPECT_NE(desc.find("GPU"), std::string::npos);
}

TEST(GDBTypesTest, GPUStateSnapshotDefault) {
    GPUStateSnapshot state;
    EXPECT_EQ(state.timestamp, 0u);
    EXPECT_TRUE(state.devices.empty());
    EXPECT_TRUE(state.memory_states.empty());
}

TEST(GDBTypesTest, ReplayControlCommands) {
    ReplayControl ctrl;
    ctrl.command = ReplayControl::Command::Start;
    EXPECT_EQ(ctrl.command, ReplayControl::Command::Start);
    
    ctrl.command = ReplayControl::Command::GotoTimestamp;
    ctrl.target_timestamp = 1000000;
    EXPECT_EQ(ctrl.target_timestamp, 1000000u);
}

TEST(GDBTypesTest, ReplayStateDefault) {
    ReplayState state;
    EXPECT_FALSE(state.active);
    EXPECT_FALSE(state.paused);
    EXPECT_EQ(state.current_event_index, 0u);
}

// ============================================================
// Signal Enum Tests
// ============================================================

TEST(GDBTypesTest, SignalValues) {
    EXPECT_EQ(static_cast<int>(Signal::None), 0);
    EXPECT_EQ(static_cast<int>(Signal::Sig_TRAP), 5);
    EXPECT_EQ(static_cast<int>(Signal::Sig_SEGV), 11);
    EXPECT_EQ(static_cast<int>(Signal::Sig_STOP), 19);
}

TEST(GDBTypesTest, StopReasonEnum) {
    EXPECT_NE(StopReason::Breakpoint, StopReason::GPUBreakpoint);
    EXPECT_NE(StopReason::Signal, StopReason::Exited);
}

TEST(GDBTypesTest, GPUBreakpointTypeEnum) {
    EXPECT_NE(GPUBreakpointType::KernelLaunch, GPUBreakpointType::KernelComplete);
    EXPECT_NE(GPUBreakpointType::MemAlloc, GPUBreakpointType::MemFree);
}
