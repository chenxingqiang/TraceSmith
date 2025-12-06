#!/usr/bin/env python3
"""
TraceSmith Example - Kernel Execution Time Statistics

Demonstrates how to capture and analyze kernel execution times:
- Per-kernel timing statistics
- Aggregated statistics by kernel name
- Histogram and percentile analysis
- CSV/JSON export for further analysis
"""

import tracesmith as ts
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional
import json
import statistics


@dataclass
class KernelStats:
    """Statistics for a specific kernel."""
    name: str
    count: int = 0
    total_duration_ns: int = 0
    min_duration_ns: int = 0
    max_duration_ns: int = 0
    durations: List[int] = field(default_factory=list)

    @property
    def avg_duration_ns(self) -> float:
        return self.total_duration_ns / self.count if self.count > 0 else 0

    @property
    def avg_duration_us(self) -> float:
        return self.avg_duration_ns / 1000

    @property
    def avg_duration_ms(self) -> float:
        return self.avg_duration_ns / 1_000_000

    @property
    def percentile_50(self) -> float:
        if not self.durations:
            return 0
        sorted_d = sorted(self.durations)
        idx = len(sorted_d) // 2
        return sorted_d[idx]

    @property
    def percentile_95(self) -> float:
        if not self.durations:
            return 0
        sorted_d = sorted(self.durations)
        idx = int(len(sorted_d) * 0.95)
        return sorted_d[min(idx, len(sorted_d) - 1)]

    @property
    def percentile_99(self) -> float:
        if not self.durations:
            return 0
        sorted_d = sorted(self.durations)
        idx = int(len(sorted_d) * 0.99)
        return sorted_d[min(idx, len(sorted_d) - 1)]

    @property
    def std_dev(self) -> float:
        if len(self.durations) < 2:
            return 0
        return statistics.stdev(self.durations)


class KernelTimingAnalyzer:
    """Analyzes kernel execution times from trace events."""

    def __init__(self, events: List[ts.TraceEvent] = None):
        self.events: List[ts.TraceEvent] = events or []
        self.kernel_stats: Dict[str, KernelStats] = {}
        self._analyzed = False

    def add_events(self, events: List[ts.TraceEvent]):
        """Add events to analyze."""
        self.events.extend(events)
        self._analyzed = False

    def analyze(self) -> Dict[str, KernelStats]:
        """Analyze all kernel events and compute statistics."""
        self.kernel_stats = {}

        for event in self.events:
            # Only analyze kernel launch events
            if event.type != ts.EventType.KernelLaunch:
                continue

            name = event.name
            duration = event.duration

            if name not in self.kernel_stats:
                self.kernel_stats[name] = KernelStats(
                    name=name,
                    min_duration_ns=duration,
                    max_duration_ns=duration
                )

            stats = self.kernel_stats[name]
            stats.count += 1
            stats.total_duration_ns += duration
            stats.durations.append(duration)
            stats.min_duration_ns = min(stats.min_duration_ns, duration)
            stats.max_duration_ns = max(stats.max_duration_ns, duration)

        self._analyzed = True
        return self.kernel_stats

    def print_summary(self, top_n: int = 20, sort_by: str = "total"):
        """Print a formatted summary of kernel statistics.
        
        Args:
            top_n: Number of top kernels to show
            sort_by: Sort criteria - 'total', 'avg', 'count', 'max'
        """
        if not self._analyzed:
            self.analyze()

        if not self.kernel_stats:
            print("No kernel events found.")
            return

        # Sort kernels
        if sort_by == "total":
            sorted_stats = sorted(
                self.kernel_stats.values(),
                key=lambda x: x.total_duration_ns,
                reverse=True
            )
        elif sort_by == "avg":
            sorted_stats = sorted(
                self.kernel_stats.values(),
                key=lambda x: x.avg_duration_ns,
                reverse=True
            )
        elif sort_by == "count":
            sorted_stats = sorted(
                self.kernel_stats.values(),
                key=lambda x: x.count,
                reverse=True
            )
        elif sort_by == "max":
            sorted_stats = sorted(
                self.kernel_stats.values(),
                key=lambda x: x.max_duration_ns,
                reverse=True
            )
        else:
            sorted_stats = list(self.kernel_stats.values())

        # Calculate totals
        total_kernel_time = sum(s.total_duration_ns for s in sorted_stats)
        total_kernel_count = sum(s.count for s in sorted_stats)

        print("\n" + "=" * 100)
        print("KERNEL EXECUTION TIME STATISTICS")
        print("=" * 100)
        print(f"Total Kernels: {len(sorted_stats)} unique, {total_kernel_count} invocations")
        print(f"Total Kernel Time: {total_kernel_time / 1e6:.2f} ms")
        print("-" * 100)

        # Table header
        print(f"{'Kernel Name':<40} {'Count':>8} {'Total(ms)':>12} {'Avg(µs)':>10} "
              f"{'Min(µs)':>10} {'Max(µs)':>10} {'P95(µs)':>10} {'%Time':>7}")
        print("-" * 100)

        for stats in sorted_stats[:top_n]:
            pct = (stats.total_duration_ns / total_kernel_time * 100) if total_kernel_time > 0 else 0
            name = stats.name[:38] + ".." if len(stats.name) > 40 else stats.name

            print(f"{name:<40} {stats.count:>8} {stats.total_duration_ns/1e6:>12.3f} "
                  f"{stats.avg_duration_us:>10.2f} {stats.min_duration_ns/1e3:>10.2f} "
                  f"{stats.max_duration_ns/1e3:>10.2f} {stats.percentile_95/1e3:>10.2f} {pct:>6.1f}%")

        if len(sorted_stats) > top_n:
            remaining = sorted_stats[top_n:]
            remaining_time = sum(s.total_duration_ns for s in remaining)
            remaining_count = sum(s.count for s in remaining)
            remaining_pct = (remaining_time / total_kernel_time * 100) if total_kernel_time > 0 else 0
            print(f"... and {len(remaining)} more kernels ({remaining_count} invocations, "
                  f"{remaining_time/1e6:.3f} ms, {remaining_pct:.1f}%)")

        print("=" * 100)

    def print_per_invocation_report(self, kernel_name: str = None, limit: int = 50):
        """Print per-invocation timing for specific kernel(s)."""
        kernel_events = []
        for event in self.events:
            if event.type != ts.EventType.KernelLaunch:
                continue
            if kernel_name and event.name != kernel_name:
                continue
            kernel_events.append(event)

        if not kernel_events:
            print(f"No kernel events found" + (f" for '{kernel_name}'" if kernel_name else ""))
            return

        print(f"\nPer-Invocation Timing Report" + (f" for '{kernel_name}'" if kernel_name else ""))
        print("-" * 80)
        print(f"{'#':>5} {'Kernel Name':<35} {'Stream':>6} {'Duration(µs)':>12} {'Timestamp(ms)':>15}")
        print("-" * 80)

        base_time = kernel_events[0].timestamp if kernel_events else 0
        for i, event in enumerate(kernel_events[:limit]):
            name = event.name[:33] + ".." if len(event.name) > 35 else event.name
            relative_time = (event.timestamp - base_time) / 1e6
            print(f"{i+1:>5} {name:<35} {event.stream_id:>6} "
                  f"{event.duration/1e3:>12.2f} {relative_time:>15.3f}")

        if len(kernel_events) > limit:
            print(f"... ({len(kernel_events) - limit} more invocations)")

    def export_to_csv(self, filename: str):
        """Export kernel statistics to CSV."""
        if not self._analyzed:
            self.analyze()

        with open(filename, 'w') as f:
            f.write("kernel_name,count,total_ns,avg_ns,min_ns,max_ns,p50_ns,p95_ns,p99_ns,std_dev\n")
            for name, stats in self.kernel_stats.items():
                f.write(f'"{name}",{stats.count},{stats.total_duration_ns},'
                        f'{stats.avg_duration_ns:.2f},{stats.min_duration_ns},'
                        f'{stats.max_duration_ns},{stats.percentile_50},'
                        f'{stats.percentile_95},{stats.percentile_99},{stats.std_dev:.2f}\n')

        print(f"Exported to {filename}")

    def export_to_json(self, filename: str):
        """Export kernel statistics to JSON."""
        if not self._analyzed:
            self.analyze()

        data = {
            "summary": {
                "unique_kernels": len(self.kernel_stats),
                "total_invocations": sum(s.count for s in self.kernel_stats.values()),
                "total_time_ns": sum(s.total_duration_ns for s in self.kernel_stats.values())
            },
            "kernels": {}
        }

        for name, stats in self.kernel_stats.items():
            data["kernels"][name] = {
                "count": stats.count,
                "total_ns": stats.total_duration_ns,
                "avg_ns": stats.avg_duration_ns,
                "min_ns": stats.min_duration_ns,
                "max_ns": stats.max_duration_ns,
                "percentiles": {
                    "p50_ns": stats.percentile_50,
                    "p95_ns": stats.percentile_95,
                    "p99_ns": stats.percentile_99
                },
                "std_dev_ns": stats.std_dev
            }

        with open(filename, 'w') as f:
            json.dump(data, f, indent=2)

        print(f"Exported to {filename}")

    def get_slowest_kernels(self, n: int = 10) -> List[KernelStats]:
        """Get the N slowest kernels by average duration."""
        if not self._analyzed:
            self.analyze()
        return sorted(
            self.kernel_stats.values(),
            key=lambda x: x.avg_duration_ns,
            reverse=True
        )[:n]

    def get_most_frequent_kernels(self, n: int = 10) -> List[KernelStats]:
        """Get the N most frequently called kernels."""
        if not self._analyzed:
            self.analyze()
        return sorted(
            self.kernel_stats.values(),
            key=lambda x: x.count,
            reverse=True
        )[:n]


def create_sample_trace_events() -> List[ts.TraceEvent]:
    """Create sample events simulating a neural network forward pass."""
    events = []
    base_time = ts.get_current_timestamp()

    # Simulate typical DL kernel patterns
    kernel_patterns = [
        # (name, base_duration_ns, variation, count)
        ("void gemm_kernel<float>", 500000, 50000, 10),     # Matrix multiply
        ("void conv2d_forward_kernel", 800000, 100000, 8),  # Convolution
        ("void batch_norm_kernel", 50000, 5000, 16),        # BatchNorm
        ("void relu_activation", 20000, 2000, 16),          # ReLU
        ("void add_bias_kernel", 30000, 3000, 8),           # Bias add
        ("void softmax_kernel", 100000, 10000, 4),          # Softmax
        ("void cross_entropy_loss", 80000, 8000, 2),        # Loss
        ("void adam_update_kernel", 150000, 15000, 20),     # Optimizer
        ("void dropout_forward", 40000, 4000, 8),           # Dropout
        ("void layernorm_kernel", 60000, 6000, 12),         # LayerNorm
    ]

    current_time = base_time
    event_id = 0

    # Simulate 5 training iterations
    for iteration in range(5):
        for kernel_name, base_duration, variation, count in kernel_patterns:
            for i in range(count):
                import random
                duration = base_duration + random.randint(-variation, variation)
                
                event = ts.TraceEvent()
                event.type = ts.EventType.KernelLaunch
                event.name = kernel_name
                event.timestamp = current_time
                event.duration = duration
                event.stream_id = i % 4  # Simulate 4 streams
                event.device_id = 0
                event.correlation_id = event_id

                events.append(event)
                current_time += duration + 1000  # 1µs gap between kernels
                event_id += 1

    return events


def main():
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith - Kernel Execution Time Statistics            ║")
    print("╚═══════════════════════════════════════════════════════════╝\n")

    # Check for GPU
    platform = ts.detect_platform()
    print(f"Platform: {ts.platform_type_to_string(platform)}")

    events = []

    if platform != ts.PlatformType.Unknown:
        # Real GPU profiling
        print("\nAttempting real GPU profiling...")
        profiler = ts.create_profiler(platform)

        config = ts.ProfilerConfig()
        config.buffer_size = 100000
        config.capture_kernels = True
        config.capture_memcpy = True

        if profiler.initialize(config):
            print("Profiler initialized. Capturing events...")
            profiler.start_capture()

            import time
            time.sleep(0.5)  # Capture window

            profiler.stop_capture()
            events = profiler.get_events()
            print(f"Captured {len(events)} events")

    # Use sample events if no real events captured
    if not events:
        print("\nUsing simulated training trace for demonstration...")
        events = create_sample_trace_events()
        print(f"Generated {len(events)} simulated events")

    # Analyze kernel timing
    analyzer = KernelTimingAnalyzer(events)
    analyzer.analyze()

    # Print summary sorted by total time
    print("\n[Sorted by Total Time]")
    analyzer.print_summary(top_n=15, sort_by="total")

    # Print summary sorted by average time
    print("\n[Sorted by Average Time]")
    analyzer.print_summary(top_n=10, sort_by="avg")

    # Per-invocation report for slowest kernel
    slowest = analyzer.get_slowest_kernels(1)
    if slowest:
        print(f"\nDetailed timing for slowest kernel: {slowest[0].name}")
        analyzer.print_per_invocation_report(slowest[0].name, limit=10)

    # Export results
    analyzer.export_to_csv("kernel_stats.csv")
    analyzer.export_to_json("kernel_stats.json")

    # Also export to Perfetto for visualization
    if ts.export_perfetto(events, "kernel_trace.json"):
        print("\nExported trace to kernel_trace.json")
        print("View at: https://ui.perfetto.dev/")

    print("\n" + "=" * 60)
    print("Analysis Complete!")


if __name__ == "__main__":
    main()
