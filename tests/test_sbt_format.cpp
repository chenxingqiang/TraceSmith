#include <gtest/gtest.h>
#include <tracesmith/format/sbt_format.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace tracesmith;

class SBTFormatTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate unique filename to avoid conflicts in parallel test runs
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        std::ostringstream oss;
        oss << "test_trace_" << dis(gen) << "_" << ::testing::UnitTest::GetInstance()->current_test_info()->name() << ".sbt";
        test_file_ = std::filesystem::temp_directory_path() / oss.str();
    }
    
    void TearDown() override {
        // Clean up test file
        std::error_code ec;
        if (std::filesystem::exists(test_file_, ec)) {
            std::filesystem::remove(test_file_, ec);
        }
    }
    
    std::filesystem::path test_file_;
};

TEST_F(SBTFormatTest, WriteAndReadEmpty) {
    {
        SBTWriter writer(test_file_.string());
        ASSERT_TRUE(writer.isOpen());
        writer.finalize();
    }
    
    {
        SBTReader reader(test_file_.string());
        ASSERT_TRUE(reader.isOpen());
        ASSERT_TRUE(reader.isValid());
        EXPECT_EQ(reader.eventCount(), 0u);
    }
}

TEST_F(SBTFormatTest, WriteAndReadEvents) {
    // Write
    {
        SBTWriter writer(test_file_.string());
        ASSERT_TRUE(writer.isOpen());
        
        TraceEvent event1(EventType::KernelLaunch, 1000000);
        event1.name = "test_kernel";
        event1.stream_id = 0;
        event1.device_id = 0;
        event1.duration = 50000;
        
        TraceEvent event2(EventType::MemcpyH2D, 1100000);
        event2.name = "cudaMemcpyHostToDevice";
        event2.stream_id = 0;
        event2.device_id = 0;
        event2.duration = 10000;
        
        MemoryParams mp;
        mp.size_bytes = 1024;
        event2.memory_params = mp;
        
        writer.writeEvent(event1);
        writer.writeEvent(event2);
        writer.finalize();
        
        EXPECT_EQ(writer.eventCount(), 2u);
    }
    
    // Read
    {
        SBTReader reader(test_file_.string());
        ASSERT_TRUE(reader.isOpen());
        ASSERT_TRUE(reader.isValid());
        
        TraceRecord record;
        auto result = reader.readAll(record);
        ASSERT_TRUE(result);
        
        EXPECT_EQ(record.size(), 2u);
        
        const auto& events = record.events();
        EXPECT_EQ(events[0].type, EventType::KernelLaunch);
        EXPECT_EQ(events[0].name, "test_kernel");
        EXPECT_EQ(events[0].duration, 50000u);
        
        EXPECT_EQ(events[1].type, EventType::MemcpyH2D);
        EXPECT_TRUE(events[1].memory_params.has_value());
        EXPECT_EQ(events[1].memory_params->size_bytes, 1024u);
    }
}

TEST_F(SBTFormatTest, WriteAndReadMetadata) {
    // Write
    {
        SBTWriter writer(test_file_.string());
        ASSERT_TRUE(writer.isOpen());
        
        TraceMetadata metadata;
        metadata.application_name = "test_app";
        metadata.command_line = "test --flag";
        metadata.hostname = "localhost";
        metadata.process_id = 12345;
        metadata.start_time = 1000000000;
        
        writer.writeMetadata(metadata);
        writer.finalize();
    }
    
    // Read
    {
        SBTReader reader(test_file_.string());
        ASSERT_TRUE(reader.isOpen());
        
        TraceRecord record;
        auto result = reader.readAll(record);
        ASSERT_TRUE(result);
        
        const auto& meta = record.metadata();
        EXPECT_EQ(meta.application_name, "test_app");
        EXPECT_EQ(meta.command_line, "test --flag");
        EXPECT_EQ(meta.hostname, "localhost");
        EXPECT_EQ(meta.process_id, 12345u);
    }
}

TEST_F(SBTFormatTest, WriteAndReadKernelParams) {
    // Write
    {
        SBTWriter writer(test_file_.string());
        ASSERT_TRUE(writer.isOpen());
        
        TraceEvent event(EventType::KernelLaunch);
        event.name = "matmul_kernel";
        
        KernelParams kp;
        kp.grid_x = 128;
        kp.grid_y = 64;
        kp.grid_z = 1;
        kp.block_x = 256;
        kp.block_y = 1;
        kp.block_z = 1;
        kp.shared_mem_bytes = 4096;
        kp.registers_per_thread = 32;
        event.kernel_params = kp;
        
        writer.writeEvent(event);
        writer.finalize();
    }
    
    // Read
    {
        SBTReader reader(test_file_.string());
        TraceRecord record;
        reader.readAll(record);
        
        ASSERT_EQ(record.size(), 1u);
        const auto& event = record.events()[0];
        
        ASSERT_TRUE(event.kernel_params.has_value());
        const auto& kp = event.kernel_params.value();
        
        EXPECT_EQ(kp.grid_x, 128u);
        EXPECT_EQ(kp.grid_y, 64u);
        EXPECT_EQ(kp.grid_z, 1u);
        EXPECT_EQ(kp.block_x, 256u);
        EXPECT_EQ(kp.shared_mem_bytes, 4096u);
        EXPECT_EQ(kp.registers_per_thread, 32u);
    }
}

TEST_F(SBTFormatTest, WriteAndReadCallStack) {
    // Write
    {
        SBTWriter writer(test_file_.string());
        ASSERT_TRUE(writer.isOpen());
        
        TraceEvent event(EventType::KernelLaunch);
        event.name = "test_kernel";
        
        CallStack cs;
        cs.thread_id = 12345;
        
        StackFrame frame1;
        frame1.address = 0x7fff12345678;
        frame1.function_name = "main";
        frame1.file_name = "main.cpp";
        frame1.line_number = 42;
        cs.frames.push_back(frame1);
        
        StackFrame frame2;
        frame2.address = 0x7fff12345000;
        frame2.function_name = "launch_kernel";
        frame2.file_name = "kernels.cpp";
        frame2.line_number = 100;
        cs.frames.push_back(frame2);
        
        event.call_stack = cs;
        
        writer.writeEvent(event);
        writer.finalize();
    }
    
    // Read
    {
        SBTReader reader(test_file_.string());
        TraceRecord record;
        reader.readAll(record);
        
        ASSERT_EQ(record.size(), 1u);
        const auto& event = record.events()[0];
        
        ASSERT_TRUE(event.call_stack.has_value());
        const auto& cs = event.call_stack.value();
        
        EXPECT_EQ(cs.thread_id, 12345u);
        EXPECT_EQ(cs.frames.size(), 2u);
        EXPECT_EQ(cs.frames[0].function_name, "main");
        EXPECT_EQ(cs.frames[0].line_number, 42u);
        EXPECT_EQ(cs.frames[1].function_name, "launch_kernel");
    }
}

TEST_F(SBTFormatTest, ManyEvents) {
    const size_t num_events = 10000;
    
    // Write
    {
        SBTWriter writer(test_file_.string());
        ASSERT_TRUE(writer.isOpen()) << "Failed to open file for writing: " << test_file_;
        
        for (size_t i = 0; i < num_events; ++i) {
            TraceEvent event(EventType::KernelLaunch, i * 1000);
            event.name = "kernel_" + std::to_string(i % 10);
            event.stream_id = i % 4;
            event.device_id = 0;
            event.duration = 1000 + (i % 100);
            writer.writeEvent(event);
        }
        
        writer.finalize();
        EXPECT_EQ(writer.eventCount(), num_events);
    }
    
    // Verify file exists and has content
    ASSERT_TRUE(std::filesystem::exists(test_file_)) << "File not created: " << test_file_;
    auto file_size = std::filesystem::file_size(test_file_);
    ASSERT_GT(file_size, 0u) << "File is empty: " << test_file_;
    
    // Read
    {
        SBTReader reader(test_file_.string());
        ASSERT_TRUE(reader.isOpen()) << "Failed to open file for reading: " << test_file_;
        ASSERT_TRUE(reader.isValid()) << "File is not a valid SBT file: " << test_file_;
        
        TraceRecord record;
        auto result = reader.readAll(record);
        
        ASSERT_TRUE(result) << "Failed to read events from: " << test_file_;
        EXPECT_EQ(record.size(), num_events) << "Event count mismatch";
    }
}

TEST_F(SBTFormatTest, HeaderValidation) {
    // Create an invalid file
    {
        std::ofstream file(test_file_, std::ios::binary);
        file << "INVALID";
    }
    
    {
        SBTReader reader(test_file_.string());
        EXPECT_TRUE(reader.isOpen());
        EXPECT_FALSE(reader.isValid());
    }
}
