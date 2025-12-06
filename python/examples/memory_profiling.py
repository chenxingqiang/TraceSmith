#!/usr/bin/env python3
"""
TraceSmith Example - GPU Memory Profiling

Demonstrates memory profiling capabilities:
- Track GPU memory allocations/deallocations
- Detect memory leaks
- Generate memory usage timeline
- Memory waterfall visualization
- Integration with PyTorch memory allocator

Requirements:
    pip install torch (optional, for PyTorch integration)
"""

import tracesmith as ts
import time
from typing import List, Dict, Optional
from dataclasses import dataclass
from contextlib import contextmanager

try:
    import torch
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False


@dataclass
class MemoryStats:
    """Memory statistics at a point in time."""
    timestamp_ms: float
    allocated_mb: float
    reserved_mb: float
    peak_allocated_mb: float
    peak_reserved_mb: float


class GPUMemoryTracker:
    """
    Track GPU memory usage during model execution.
    
    Combines TraceSmith memory profiler with PyTorch's memory tracking
    for comprehensive memory analysis.
    """

    def __init__(self, snapshot_interval_ms: int = 10):
        self.memory_profiler = None
        self.snapshots: List[MemoryStats] = []
        self.snapshot_interval_ms = snapshot_interval_ms
        self._init_profiler()

    def _init_profiler(self):
        """Initialize TraceSmith memory profiler."""
        config = ts.MemoryProfilerConfig()
        config.snapshot_interval_ms = self.snapshot_interval_ms
        config.leak_threshold_ns = 5_000_000_000  # 5 seconds
        config.detect_double_free = True
        config.max_timeline_samples = 10000

        self.memory_profiler = ts.MemoryProfiler(config)

    def start(self):
        """Start memory tracking."""
        self.memory_profiler.start()
        self.snapshots.clear()
        self._record_initial_state()

    def stop(self):
        """Stop memory tracking."""
        self.memory_profiler.stop()

    def _record_initial_state(self):
        """Record initial memory state."""
        if TORCH_AVAILABLE and torch.cuda.is_available():
            self.snapshots.append(MemoryStats(
                timestamp_ms=0,
                allocated_mb=torch.cuda.memory_allocated() / 1e6,
                reserved_mb=torch.cuda.memory_reserved() / 1e6,
                peak_allocated_mb=torch.cuda.max_memory_allocated() / 1e6,
                peak_reserved_mb=torch.cuda.max_memory_reserved() / 1e6
            ))

    def record_allocation(self, ptr: int, size: int, tag: str = ""):
        """Record a memory allocation."""
        self.memory_profiler.record_alloc(ptr, size, 0, "pytorch", tag)

    def record_free(self, ptr: int):
        """Record a memory deallocation."""
        self.memory_profiler.record_free(ptr, 0)

    def take_snapshot(self, label: str = "") -> MemoryStats:
        """Take a memory snapshot."""
        self.memory_profiler.take_snapshot()

        if TORCH_AVAILABLE and torch.cuda.is_available():
            stats = MemoryStats(
                timestamp_ms=time.perf_counter() * 1000,
                allocated_mb=torch.cuda.memory_allocated() / 1e6,
                reserved_mb=torch.cuda.memory_reserved() / 1e6,
                peak_allocated_mb=torch.cuda.max_memory_allocated() / 1e6,
                peak_reserved_mb=torch.cuda.max_memory_reserved() / 1e6
            )
            self.snapshots.append(stats)
            return stats
        return None

    @contextmanager
    def track(self, label: str = ""):
        """Context manager for tracking memory in a code block."""
        self.take_snapshot(f"{label}_start")
        yield
        if TORCH_AVAILABLE and torch.cuda.is_available():
            torch.cuda.synchronize()
        self.take_snapshot(f"{label}_end")

    def get_report(self) -> ts.MemoryReport:
        """Generate memory report."""
        return self.memory_profiler.generate_report()

    def detect_leaks(self) -> List[ts.MemoryLeak]:
        """Detect potential memory leaks."""
        return self.memory_profiler.detect_leaks()

    def get_current_usage(self) -> Dict:
        """Get current memory usage."""
        result = {
            "tracesmith_live_bytes": self.memory_profiler.get_current_usage(),
            "tracesmith_peak_bytes": self.memory_profiler.get_peak_usage(),
            "tracesmith_live_count": self.memory_profiler.get_live_allocation_count(),
        }

        if TORCH_AVAILABLE and torch.cuda.is_available():
            result.update({
                "torch_allocated_mb": torch.cuda.memory_allocated() / 1e6,
                "torch_reserved_mb": torch.cuda.memory_reserved() / 1e6,
                "torch_peak_allocated_mb": torch.cuda.max_memory_allocated() / 1e6,
                "torch_peak_reserved_mb": torch.cuda.max_memory_reserved() / 1e6,
            })

        return result

    def print_summary(self):
        """Print memory profiling summary."""
        report = self.get_report()

        print("\n" + "=" * 70)
        print("GPU MEMORY PROFILING SUMMARY")
        print("=" * 70)
        print(report.summary())

        # PyTorch specific stats
        if TORCH_AVAILABLE and torch.cuda.is_available():
            print("\nPyTorch Memory Stats:")
            print(f"  Current Allocated: {torch.cuda.memory_allocated() / 1e6:.2f} MB")
            print(f"  Current Reserved:  {torch.cuda.memory_reserved() / 1e6:.2f} MB")
            print(f"  Peak Allocated:    {torch.cuda.max_memory_allocated() / 1e6:.2f} MB")
            print(f"  Peak Reserved:     {torch.cuda.max_memory_reserved() / 1e6:.2f} MB")

        # Leak detection
        leaks = self.detect_leaks()
        if leaks:
            print(f"\n⚠️  Potential Leaks Detected: {len(leaks)}")
            for leak in leaks[:5]:
                print(f"    - {ts.format_bytes(leak.size)} at 0x{leak.ptr:x} "
                      f"({leak.allocator}, age: {ts.format_duration(leak.lifetime_ns)})")
        else:
            print("\n✓ No memory leaks detected")

        print("=" * 70)

    def export_timeline(self, filename: str):
        """Export memory timeline to JSON."""
        counter_events = self.memory_profiler.to_counter_events()
        memory_events = self.memory_profiler.to_memory_events()

        # Convert to trace events for Perfetto
        trace_events = []
        for me in memory_events:
            event = ts.TraceEvent()
            event.type = ts.EventType.MemAlloc if me.is_allocation else ts.EventType.MemFree
            event.name = f"{'Alloc' if me.is_allocation else 'Free'} {ts.format_bytes(me.bytes)}"
            event.timestamp = me.timestamp
            event.duration = 1000  # 1µs marker
            event.device_id = me.device_id
            trace_events.append(event)

        ts.export_perfetto(trace_events, filename)
        print(f"Exported memory timeline to {filename}")


def profile_memory_intensive_operation():
    """Profile a memory-intensive PyTorch operation."""
    if not TORCH_AVAILABLE or not torch.cuda.is_available():
        print("PyTorch with CUDA required for this example")
        return

    device = torch.device("cuda")

    print("\n" + "-" * 60)
    print("Memory-Intensive Operation Profile")
    print("-" * 60)

    tracker = GPUMemoryTracker(snapshot_interval_ms=5)
    tracker.start()

    # Reset peak stats
    torch.cuda.reset_peak_memory_stats()

    print("\nPhase 1: Allocating large tensors...")
    with tracker.track("large_tensors"):
        tensors = []
        for i in range(5):
            # Create 100MB tensors
            t = torch.randn(25 * 1024 * 1024, device=device)  # ~100MB
            tensors.append(t)
            usage = tracker.get_current_usage()
            print(f"  Tensor {i+1}: Allocated {usage['torch_allocated_mb']:.1f} MB")

    print("\nPhase 2: Matrix operations (intermediate memory)...")
    with tracker.track("matmul"):
        a = torch.randn(4096, 4096, device=device)
        b = torch.randn(4096, 4096, device=device)
        c = torch.matmul(a, b)
        usage = tracker.get_current_usage()
        print(f"  After matmul: {usage['torch_allocated_mb']:.1f} MB")

    print("\nPhase 3: Freeing tensors...")
    with tracker.track("free"):
        del tensors
        del a, b, c
        torch.cuda.empty_cache()
        usage = tracker.get_current_usage()
        print(f"  After cleanup: {usage['torch_allocated_mb']:.1f} MB")

    tracker.stop()
    tracker.print_summary()
    tracker.export_timeline("memory_timeline.json")


def profile_model_memory(model_fn, input_fn, name: str = "Model"):
    """Profile memory usage during model forward/backward pass."""
    if not TORCH_AVAILABLE or not torch.cuda.is_available():
        print("PyTorch with CUDA required")
        return

    device = torch.device("cuda")

    print(f"\n" + "-" * 60)
    print(f"Memory Profile: {name}")
    print("-" * 60)

    # Create model and input
    model = model_fn().to(device)
    input_data = input_fn().to(device)

    tracker = GPUMemoryTracker(snapshot_interval_ms=1)

    # Reset memory stats
    torch.cuda.reset_peak_memory_stats()
    torch.cuda.empty_cache()

    tracker.start()

    print("\nStep 1: Model loaded")
    tracker.take_snapshot("model_loaded")
    usage = tracker.get_current_usage()
    print(f"  Memory: {usage['torch_allocated_mb']:.1f} MB")

    print("\nStep 2: Forward pass")
    with tracker.track("forward"):
        output = model(input_data)
    usage = tracker.get_current_usage()
    print(f"  Memory: {usage['torch_allocated_mb']:.1f} MB "
          f"(peak: {usage['torch_peak_allocated_mb']:.1f} MB)")

    print("\nStep 3: Backward pass")
    with tracker.track("backward"):
        loss = output.sum()
        loss.backward()
    usage = tracker.get_current_usage()
    print(f"  Memory: {usage['torch_allocated_mb']:.1f} MB "
          f"(peak: {usage['torch_peak_allocated_mb']:.1f} MB)")

    print("\nStep 4: Cleanup")
    with tracker.track("cleanup"):
        del output, loss
        model.zero_grad()
        torch.cuda.empty_cache()
    usage = tracker.get_current_usage()
    print(f"  Memory: {usage['torch_allocated_mb']:.1f} MB")

    tracker.stop()
    tracker.print_summary()

    return tracker


def create_sample_memory_events() -> List[ts.MemoryEvent]:
    """Create sample memory events for demonstration."""
    events = []
    base_time = ts.get_current_timestamp()

    # Simulate allocation pattern
    allocations = [
        ("weights_layer1", 4 * 1024 * 1024),    # 4MB
        ("weights_layer2", 16 * 1024 * 1024),   # 16MB
        ("activations", 32 * 1024 * 1024),      # 32MB
        ("gradients", 32 * 1024 * 1024),        # 32MB
        ("optimizer_state", 16 * 1024 * 1024),  # 16MB
    ]

    ptr = 0x1000000
    current_time = base_time

    for name, size in allocations:
        event = ts.MemoryEvent()
        event.timestamp = current_time
        event.device_id = 0
        event.bytes = size
        event.ptr = ptr
        event.is_allocation = True
        event.allocator_name = "pytorch"
        event.category = ts.MemoryCategory.Activation
        events.append(event)

        ptr += size + 0x1000
        current_time += 100000  # 100µs

    return events


def main():
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith - GPU Memory Profiling                        ║")
    print("╚═══════════════════════════════════════════════════════════╝\n")

    platform = ts.detect_platform()
    print(f"Platform: {ts.platform_type_to_string(platform)}")

    if TORCH_AVAILABLE:
        print(f"PyTorch: {torch.__version__}")
        if torch.cuda.is_available():
            print(f"CUDA Device: {torch.cuda.get_device_name(0)}")
            print(f"CUDA Memory: {torch.cuda.get_device_properties(0).total_memory / 1e9:.1f} GB")
    else:
        print("PyTorch not available")

    # Example 1: Basic memory tracking
    print("\n" + "=" * 60)
    print("Example 1: Basic Memory Tracking with TraceSmith")
    print("=" * 60)

    config = ts.MemoryProfilerConfig()
    config.snapshot_interval_ms = 10
    config.leak_threshold_ns = 5_000_000_000

    profiler = ts.MemoryProfiler(config)
    profiler.start()

    # Simulate allocations
    base_ptr = 0x1000000
    profiler.record_alloc(base_ptr, 1024 * 1024, 0, "test", "tensor_a")
    profiler.record_alloc(base_ptr + 0x100000, 2 * 1024 * 1024, 0, "test", "tensor_b")
    profiler.record_alloc(base_ptr + 0x300000, 4 * 1024 * 1024, 0, "test", "tensor_c")

    print(f"Current usage: {ts.format_bytes(profiler.get_current_usage())}")
    print(f"Peak usage: {ts.format_bytes(profiler.get_peak_usage())}")
    print(f"Live allocations: {profiler.get_live_allocation_count()}")

    # Free some allocations
    profiler.record_free(base_ptr, 0)
    profiler.record_free(base_ptr + 0x100000, 0)

    print(f"\nAfter freeing 2 tensors:")
    print(f"Current usage: {ts.format_bytes(profiler.get_current_usage())}")
    print(f"Live allocations: {profiler.get_live_allocation_count()}")

    profiler.stop()
    report = profiler.generate_report()
    print(f"\nReport:\n{report.summary()}")

    # Example 2: Memory-intensive operations (if PyTorch available)
    if TORCH_AVAILABLE and torch.cuda.is_available():
        profile_memory_intensive_operation()

        # Example 3: Model memory profile
        print("\n" + "=" * 60)
        print("Example 3: Model Memory Profile")
        print("=" * 60)

        import torch.nn as nn

        def simple_cnn():
            return nn.Sequential(
                nn.Conv2d(3, 64, 3, padding=1),
                nn.BatchNorm2d(64),
                nn.ReLU(),
                nn.Conv2d(64, 128, 3, padding=1),
                nn.BatchNorm2d(128),
                nn.ReLU(),
                nn.AdaptiveAvgPool2d(1),
                nn.Flatten(),
                nn.Linear(128, 10)
            )

        def sample_input():
            return torch.randn(32, 3, 64, 64)

        profile_model_memory(simple_cnn, sample_input, "SimpleCNN")

    print("\n" + "=" * 60)
    print("Memory Profiling Complete!")
    print("=" * 60)


if __name__ == "__main__":
    main()
