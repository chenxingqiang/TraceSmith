#include <gtest/gtest.h>
#include <tracesmith/common/ring_buffer.hpp>
#include <thread>
#include <vector>

using namespace tracesmith;

TEST(RingBufferTest, BasicPushPop) {
    RingBuffer<int> buffer(16);
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
    
    buffer.push(42);
    EXPECT_FALSE(buffer.empty());
    EXPECT_EQ(buffer.size(), 1u);
    
    auto value = buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 42);
    EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, MultiplePushPop) {
    RingBuffer<int> buffer(64);
    
    for (int i = 0; i < 50; ++i) {
        buffer.push(i);
    }
    
    EXPECT_EQ(buffer.size(), 50u);
    
    for (int i = 0; i < 50; ++i) {
        auto value = buffer.pop();
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, i);
    }
    
    EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, PopBatch) {
    RingBuffer<int> buffer(64);
    
    for (int i = 0; i < 30; ++i) {
        buffer.push(i);
    }
    
    std::vector<int> batch;
    size_t count = buffer.popBatch(batch, 10);
    
    EXPECT_EQ(count, 10u);
    EXPECT_EQ(batch.size(), 10u);
    EXPECT_EQ(buffer.size(), 20u);
    
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(batch[i], i);
    }
}

TEST(RingBufferTest, OverflowDropOldest) {
    RingBuffer<int> buffer(8, OverflowPolicy::DropOldest);
    
    for (int i = 0; i < 20; ++i) {
        buffer.push(i);
    }
    
    EXPECT_EQ(buffer.capacity(), 8u);
    EXPECT_GT(buffer.droppedCount(), 0u);
    
    // The newest values should still be in the buffer
    auto value = buffer.pop();
    ASSERT_TRUE(value.has_value());
}

TEST(RingBufferTest, OverflowDropNewest) {
    RingBuffer<int> buffer(8, OverflowPolicy::DropNewest);
    
    for (int i = 0; i < 20; ++i) {
        buffer.push(i);
    }
    
    EXPECT_GT(buffer.droppedCount(), 0u);
    
    // The oldest values should be in the buffer
    auto value = buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 0);
}

TEST(RingBufferTest, PowerOfTwo) {
    // Test that capacity is rounded up to power of 2
    RingBuffer<int> buffer(10);
    EXPECT_EQ(buffer.capacity(), 16u);
    
    RingBuffer<int> buffer2(17);
    EXPECT_EQ(buffer2.capacity(), 32u);
}

TEST(RingBufferTest, ConcurrentAccess) {
    // Buffer must be large enough to hold all items to avoid infinite loop
    // when using DropOldest policy (items dropped = consumer never reaches target)
    const int num_items = 10000;
    RingBuffer<int> buffer(num_items * 2);  // 2x capacity to avoid overflow
    std::atomic<int> consumed{0};
    std::atomic<bool> producer_done{false};
    
    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_items; ++i) {
            buffer.push(i);
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        while (consumed.load(std::memory_order_relaxed) < num_items) {
            auto value = buffer.pop();
            if (value.has_value()) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            } else if (producer_done.load(std::memory_order_acquire)) {
                // Producer finished but buffer empty - check if we got all items
                break;
            } else {
                std::this_thread::yield();
            }
        }
    });
    
    producer.join();
    consumer.join();
    
    EXPECT_EQ(consumed.load(), num_items);
}

TEST(RingBufferTest, Reset) {
    RingBuffer<int> buffer(16);
    
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    
    EXPECT_EQ(buffer.size(), 3u);
    
    buffer.reset();
    
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.droppedCount(), 0u);
}

TEST(RingBufferTest, MoveSemantics) {
    RingBuffer<std::string> buffer(16);
    
    std::string s = "hello world";
    buffer.push(std::move(s));
    
    auto value = buffer.pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "hello world");
}
