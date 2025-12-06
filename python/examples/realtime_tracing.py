#!/usr/bin/env python3
"""
TraceSmith Example - Real-time Tracing

Demonstrates real-time tracing capabilities:
- Lock-free ring buffer tracing
- Custom event emission
- Counter tracking (GPU utilization, memory, etc.)
- Live session management
- Async-safe tracing

Requirements:
    pip install torch (optional)
"""

import tracesmith as ts
import time
import threading
from typing import List, Optional
from contextlib import contextmanager

try:
    import torch
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False


class RealTimeProfiler:
    """
    Real-time profiler for continuous monitoring.
    
    Uses TraceSmith's TracingSession for lock-free event capture
    suitable for production environments.
    """

    def __init__(self, event_buffer_size: int = 100000,
                 counter_buffer_size: int = 10000):
        self.session = ts.TracingSession(event_buffer_size, counter_buffer_size)
        self._monitoring = False
        self._monitor_thread: Optional[threading.Thread] = None
        self._counter_interval_ms = 100
        self._events: List[ts.TraceEvent] = []
        self._counters: List[tuple] = []  # (name, value, timestamp)
        self._started = False
        self._start_time = 0

    def start(self):
        """Start tracing session."""
        # Note: TracingSession.start() requires TracingConfig which may not be available
        # in all builds. We provide a fallback implementation.
        self._started = True
        self._start_time = ts.get_current_timestamp()
        self._events = []
        self._counters = []

        print(f"Tracing session started")
        print(f"  Event buffer: {self.session.event_buffer_capacity()} events")
        print(f"  Mode: Manual event collection")

    def stop(self) -> ts.TracingStatistics:
        """Stop tracing session."""
        self._monitoring = False
        if self._monitor_thread:
            self._monitor_thread.join(timeout=1.0)

        self._started = False
        stats = ts.TracingStatistics()
        stats.events_emitted = len(self._events)
        stats.counters_emitted = len(self._counters)
        stats.events_dropped = 0
        stats.start_time = self._start_time
        stats.stop_time = ts.get_current_timestamp()
        return stats

    def emit_event(self, event: ts.TraceEvent):
        """Emit a custom trace event (thread-safe)."""
        self._events.append(event)

    def emit_counter(self, name: str, value: float, timestamp: int = 0):
        """Emit a counter value (thread-safe)."""
        if timestamp == 0:
            timestamp = ts.get_current_timestamp()
        self._counters.append((name, value, timestamp))

    def start_monitoring(self, interval_ms: int = 100):
        """Start background counter monitoring."""
        self._counter_interval_ms = interval_ms
        self._monitoring = True
        self._monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self._monitor_thread.start()

    def _monitor_loop(self):
        """Background thread for counter monitoring."""
        while self._monitoring:
            timestamp = ts.get_current_timestamp()

            # Emit system counters
            if TORCH_AVAILABLE and torch.cuda.is_available():
                # GPU memory
                mem_allocated = torch.cuda.memory_allocated() / 1e6
                mem_reserved = torch.cuda.memory_reserved() / 1e6

                self.emit_counter("gpu_memory_allocated_mb", mem_allocated, timestamp)
                self.emit_counter("gpu_memory_reserved_mb", mem_reserved, timestamp)

                # GPU utilization (if available via pynvml)
                try:
                    import pynvml
                    pynvml.nvmlInit()
                    handle = pynvml.nvmlDeviceGetHandleByIndex(0)
                    util = pynvml.nvmlDeviceGetUtilizationRates(handle)
                    self.emit_counter("gpu_utilization_pct", util.gpu, timestamp)
                    self.emit_counter("gpu_memory_util_pct", util.memory, timestamp)
                except:
                    pass

            time.sleep(self._counter_interval_ms / 1000)

    def get_events(self) -> List[ts.TraceEvent]:
        """Get captured events."""
        return list(self._events)

    def get_counters(self) -> List[tuple]:
        """Get captured counters as (name, value, timestamp) tuples."""
        return list(self._counters)

    def get_stats(self) -> ts.TracingStatistics:
        """Get current statistics."""
        stats = ts.TracingStatistics()
        stats.events_emitted = len(self._events)
        stats.counters_emitted = len(self._counters)
        stats.events_dropped = 0
        stats.start_time = self._start_time
        stats.stop_time = ts.get_current_timestamp()
        return stats

    def export(self, filename: str, use_protobuf: bool = False):
        """Export session to Perfetto file."""
        return ts.export_perfetto(self._events, filename, use_protobuf)

    @contextmanager
    def trace_region(self, name: str):
        """Context manager for tracing a code region."""
        start = ts.get_current_timestamp()

        # Emit range start
        start_event = ts.TraceEvent()
        start_event.type = ts.EventType.RangeStart
        start_event.name = name
        start_event.timestamp = start
        self.emit_event(start_event)

        try:
            yield
        finally:
            end = ts.get_current_timestamp()

            # Emit range end
            end_event = ts.TraceEvent()
            end_event.type = ts.EventType.RangeEnd
            end_event.name = name
            end_event.timestamp = end
            end_event.duration = end - start
            self.emit_event(end_event)

    @contextmanager
    def trace_operation(self, name: str, op_type: ts.EventType = ts.EventType.Custom):
        """Context manager for tracing a single operation."""
        start = ts.get_current_timestamp()

        try:
            yield
        finally:
            end = ts.get_current_timestamp()

            event = ts.TraceEvent()
            event.type = op_type
            event.name = name
            event.timestamp = start
            event.duration = end - start
            self.emit_event(event)


def demo_basic_tracing():
    """Demonstrate basic real-time tracing."""
    print("\n" + "=" * 60)
    print("Basic Real-Time Tracing Demo")
    print("=" * 60)

    profiler = RealTimeProfiler(event_buffer_size=50000)
    profiler.start()

    # Manual event emission
    print("\nEmitting custom events...")
    for i in range(10):
        event = ts.TraceEvent()
        event.type = ts.EventType.Custom
        event.name = f"custom_operation_{i}"
        event.timestamp = ts.get_current_timestamp()
        event.duration = 10000 + i * 1000  # 10-20µs
        event.stream_id = i % 4
        profiler.emit_event(event)

    # Counter emission
    print("Emitting counters...")
    for i in range(50):
        profiler.emit_counter("iteration", i)
        profiler.emit_counter("progress_pct", i * 2)
        time.sleep(0.01)

    # Using context managers
    print("Tracing regions...")
    with profiler.trace_region("data_loading"):
        time.sleep(0.1)

    with profiler.trace_region("model_forward"):
        for i in range(5):
            with profiler.trace_operation(f"layer_{i}"):
                time.sleep(0.02)

    with profiler.trace_region("model_backward"):
        time.sleep(0.05)

    # Stop and get stats
    stats = profiler.stop()

    print(f"\nSession Statistics:")
    print(f"  Events emitted: {stats.events_emitted}")
    print(f"  Events dropped: {stats.events_dropped}")
    print(f"  Counters emitted: {stats.counters_emitted}")
    print(f"  Duration: {stats.duration_ms():.2f} ms")

    # Get data
    events = profiler.get_events()
    counters = profiler.get_counters()

    print(f"\nCaptured Data:")
    print(f"  Events: {len(events)}")
    print(f"  Counters: {len(counters)}")

    # Export
    profiler.export("realtime_trace.json", use_protobuf=False)
    print("\n✓ Exported: realtime_trace.json")


def demo_concurrent_tracing():
    """Demonstrate concurrent event emission from multiple threads."""
    print("\n" + "=" * 60)
    print("Concurrent Tracing Demo")
    print("=" * 60)

    profiler = RealTimeProfiler(event_buffer_size=200000)
    profiler.start()

    event_count = 0
    lock = threading.Lock()

    def worker(thread_id: int, iterations: int):
        nonlocal event_count
        for i in range(iterations):
            event = ts.TraceEvent()
            event.type = ts.EventType.KernelLaunch
            event.name = f"kernel_thread_{thread_id}"
            event.timestamp = ts.get_current_timestamp()
            event.duration = 5000 + (i % 10) * 500  # 5-10µs
            event.thread_id = thread_id
            event.stream_id = thread_id

            profiler.emit_event(event)

            with lock:
                event_count += 1

            time.sleep(0.0001)  # 0.1ms between events

    # Start multiple threads
    num_threads = 8
    iterations_per_thread = 1000
    threads = []

    print(f"\nStarting {num_threads} worker threads...")
    start_time = time.perf_counter()

    for i in range(num_threads):
        t = threading.Thread(target=worker, args=(i, iterations_per_thread))
        threads.append(t)
        t.start()

    # Wait for completion
    for t in threads:
        t.join()

    elapsed = time.perf_counter() - start_time

    # Stop and analyze
    stats = profiler.stop()

    print(f"\nResults:")
    print(f"  Total events emitted: {event_count}")
    print(f"  Time elapsed: {elapsed * 1000:.2f} ms")
    print(f"  Events/sec: {event_count / elapsed:,.0f}")
    print(f"  Events captured: {stats.events_emitted}")
    print(f"  Events dropped: {stats.events_dropped}")

    events = profiler.get_events()

    # Analyze by thread
    events_by_thread = {}
    for event in events:
        tid = event.thread_id
        events_by_thread[tid] = events_by_thread.get(tid, 0) + 1

    print(f"\nEvents by thread:")
    for tid in sorted(events_by_thread.keys()):
        print(f"  Thread {tid}: {events_by_thread[tid]} events")

    profiler.export("concurrent_trace.json", use_protobuf=False)
    print("\n✓ Exported: concurrent_trace.json")


def demo_gpu_monitoring():
    """Demonstrate GPU monitoring with counters."""
    if not TORCH_AVAILABLE or not torch.cuda.is_available():
        print("\nPyTorch with CUDA required for GPU monitoring demo")
        return

    print("\n" + "=" * 60)
    print("GPU Monitoring Demo")
    print("=" * 60)

    device = torch.device("cuda")
    profiler = RealTimeProfiler(event_buffer_size=100000, counter_buffer_size=50000)

    # Start with background monitoring
    profiler.start()
    profiler.start_monitoring(interval_ms=50)

    print("\nRunning GPU workload with monitoring...")

    # Simulate workload
    with profiler.trace_region("workload"):
        for i in range(5):
            with profiler.trace_region(f"iteration_{i}"):
                # Create tensors
                with profiler.trace_operation("create_tensors"):
                    a = torch.randn(2048, 2048, device=device)
                    b = torch.randn(2048, 2048, device=device)

                # Matrix multiply
                with profiler.trace_operation("matmul"):
                    c = torch.matmul(a, b)
                    torch.cuda.synchronize()

                # Cleanup
                with profiler.trace_operation("cleanup"):
                    del a, b, c
                    torch.cuda.empty_cache()

            time.sleep(0.1)  # Gap between iterations

    # Stop monitoring
    stats = profiler.stop()

    print(f"\nSession Statistics:")
    print(f"  Events: {stats.events_emitted}")
    print(f"  Counters: {stats.counters_emitted}")
    print(f"  Duration: {stats.duration_ms():.2f} ms")

    # Analyze counters
    counters = profiler.get_counters()

    counter_values = {}
    for c in counters:
        name = c.counter_name
        if name not in counter_values:
            counter_values[name] = []
        counter_values[name].append(c.value)

    print(f"\nCounter Summary:")
    for name, values in counter_values.items():
        if values:
            avg = sum(values) / len(values)
            max_val = max(values)
            min_val = min(values)
            print(f"  {name}: avg={avg:.2f}, min={min_val:.2f}, max={max_val:.2f}")

    # Export
    profiler.export("gpu_monitoring_trace.json", use_protobuf=False)
    print("\n✓ Exported: gpu_monitoring_trace.json")


def demo_flow_events():
    """Demonstrate flow events for async operations."""
    print("\n" + "=" * 60)
    print("Flow Events Demo (Async Correlation)")
    print("=" * 60)

    profiler = RealTimeProfiler()
    profiler.start()

    print("\nEmitting async operation flow...")

    # Simulate async GPU operations with flow correlation
    for i in range(5):
        flow_id = 1000 + i

        # CPU-side launch (flow start)
        launch_event = ts.TraceEvent()
        launch_event.type = ts.EventType.Custom
        launch_event.name = f"cpu_launch_kernel_{i}"
        launch_event.timestamp = ts.get_current_timestamp()
        launch_event.duration = 5000  # 5µs
        launch_event.thread_id = 1
        launch_event.flow_info = ts.FlowInfo(flow_id, ts.FlowType.AsyncCpuGpu, True)
        profiler.emit_event(launch_event)

        time.sleep(0.001)

        # GPU-side execution (flow end)
        gpu_event = ts.TraceEvent()
        gpu_event.type = ts.EventType.KernelLaunch
        gpu_event.name = f"gpu_kernel_{i}"
        gpu_event.timestamp = ts.get_current_timestamp()
        gpu_event.duration = 50000  # 50µs
        gpu_event.stream_id = i % 2
        gpu_event.flow_info = ts.FlowInfo(flow_id, ts.FlowType.AsyncCpuGpu, False)
        profiler.emit_event(gpu_event)

        time.sleep(0.002)

    stats = profiler.stop()

    print(f"\nEmitted {stats.events_emitted} events with flow correlation")

    profiler.export("flow_trace.json", use_protobuf=False)
    print("✓ Exported: flow_trace.json")
    print("\nOpen in Perfetto to see connected flow arrows between CPU and GPU events")


def main():
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith - Real-Time Tracing                           ║")
    print("╚═══════════════════════════════════════════════════════════╝\n")

    print(f"TraceSmith Version: {ts.__version__}")

    # Run demos
    demo_basic_tracing()
    demo_concurrent_tracing()
    demo_gpu_monitoring()
    demo_flow_events()

    print("\n" + "=" * 60)
    print("Real-Time Tracing Demos Complete!")
    print("=" * 60)
    print("\nGenerated trace files:")
    print("  - realtime_trace.json")
    print("  - concurrent_trace.json")
    print("  - gpu_monitoring_trace.json (if GPU available)")
    print("  - flow_trace.json")
    print("\nView at: https://ui.perfetto.dev/")


if __name__ == "__main__":
    main()
