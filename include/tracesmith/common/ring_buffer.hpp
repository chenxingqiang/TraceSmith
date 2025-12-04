#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace tracesmith {

/// Overflow handling policy for the ring buffer
enum class OverflowPolicy {
    DropOldest,  // Overwrite oldest data when full
    DropNewest,  // Discard new data when full
    Block        // Block producer until space is available
};

/**
 * Lock-free Single Producer Single Consumer (SPSC) ring buffer.
 * 
 * This implementation is optimized for GPU profiling where:
 * - One thread (profiler callback) produces events
 * - Another thread (writer) consumes and persists events
 * 
 * Memory ordering guarantees thread-safety without locks.
 */
template<typename T>
class RingBuffer {
public:
    /**
     * Construct a ring buffer with the specified capacity.
     * @param capacity Number of elements (must be power of 2 for efficiency)
     * @param policy How to handle buffer overflow
     */
    explicit RingBuffer(size_t capacity, OverflowPolicy policy = OverflowPolicy::DropOldest)
        : capacity_(nextPowerOf2(capacity))
        , mask_(capacity_ - 1)
        , policy_(policy)
        , buffer_(std::make_unique<T[]>(capacity_))
        , head_(0)
        , tail_(0)
        , dropped_count_(0) {
    }
    
    /// Non-copyable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    /// Movable
    RingBuffer(RingBuffer&&) = default;
    RingBuffer& operator=(RingBuffer&&) = default;
    
    /**
     * Push an element into the buffer.
     * @param item Item to push
     * @return true if successful, false if dropped (for non-blocking policies)
     */
    bool push(const T& item) {
        return pushImpl(item);
    }
    
    bool push(T&& item) {
        return pushImpl(std::move(item));
    }
    
    /**
     * Try to pop an element from the buffer.
     * @return The element if available, std::nullopt otherwise
     */
    std::optional<T> pop() {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t head = head_.load(std::memory_order_acquire);
        
        if (tail == head) {
            // Buffer is empty
            return std::nullopt;
        }
        
        T item = std::move(buffer_[tail & mask_]);
        tail_.store(tail + 1, std::memory_order_release);
        
        return item;
    }
    
    /**
     * Pop multiple elements at once for efficiency.
     * @param out Vector to append elements to
     * @param max_count Maximum number of elements to pop
     * @return Number of elements actually popped
     */
    size_t popBatch(std::vector<T>& out, size_t max_count) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t head = head_.load(std::memory_order_acquire);
        
        size_t available = head - tail;
        size_t count = std::min(available, max_count);
        
        if (count == 0) {
            return 0;
        }
        
        out.reserve(out.size() + count);
        
        for (size_t i = 0; i < count; ++i) {
            out.push_back(std::move(buffer_[(tail + i) & mask_]));
        }
        
        tail_.store(tail + count, std::memory_order_release);
        
        return count;
    }
    
    /**
     * Check if the buffer is empty.
     */
    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    /**
     * Get the current number of elements in the buffer.
     */
    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }
    
    /**
     * Get the buffer capacity.
     */
    size_t capacity() const {
        return capacity_;
    }
    
    /**
     * Get the number of dropped elements (due to overflow).
     */
    uint64_t droppedCount() const {
        return dropped_count_.load(std::memory_order_relaxed);
    }
    
    /**
     * Reset the buffer (not thread-safe, only call when idle).
     */
    void reset() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        dropped_count_.store(0, std::memory_order_relaxed);
    }

private:
    template<typename U>
    bool pushImpl(U&& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_acquire);
        
        size_t next_head = head + 1;
        
        // Check if buffer is full
        if (next_head - tail > capacity_) {
            switch (policy_) {
                case OverflowPolicy::DropNewest:
                    dropped_count_.fetch_add(1, std::memory_order_relaxed);
                    return false;
                    
                case OverflowPolicy::DropOldest:
                    // Advance tail to make room
                    tail_.store(tail + 1, std::memory_order_release);
                    dropped_count_.fetch_add(1, std::memory_order_relaxed);
                    break;
                    
                case OverflowPolicy::Block:
                    // Spin wait (not ideal, but simple)
                    while (next_head - tail_.load(std::memory_order_acquire) > capacity_) {
                        // Could add a yield or sleep here
                    }
                    break;
            }
        }
        
        buffer_[head & mask_] = std::forward<U>(item);
        head_.store(next_head, std::memory_order_release);
        
        return true;
    }
    
    static size_t nextPowerOf2(size_t n) {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        n++;
        return n;
    }
    
    const size_t capacity_;
    const size_t mask_;
    const OverflowPolicy policy_;
    
    std::unique_ptr<T[]> buffer_;
    
    // Cache line padding to prevent false sharing
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    alignas(64) std::atomic<uint64_t> dropped_count_;
};

} // namespace tracesmith
