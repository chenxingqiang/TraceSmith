#!/usr/bin/env python3
"""
TraceSmith Example - Multi-GPU Profiling

Demonstrates multi-GPU profiling capabilities:
- GPU topology discovery (NVLink, PCIe)
- Multi-GPU event capture
- NVLink transfer tracking
- Cross-GPU synchronization analysis
- Data parallel training profiling

Requirements:
    pip install torch (optional, for PyTorch integration)
"""

import tracesmith as ts
import time
from typing import List, Dict, Optional
from dataclasses import dataclass

try:
    import torch
    import torch.nn as nn
    import torch.distributed as dist
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False


def discover_gpu_topology():
    """Discover and display GPU topology."""
    print("\n" + "=" * 70)
    print("GPU TOPOLOGY DISCOVERY")
    print("=" * 70)

    if not ts.is_cluster_available():
        print("\n⚠ Cluster module not available.")
        print("  Multi-GPU profiling requires TraceSmith built with cluster support.")
        print("  Rebuild with: cmake .. -DTRACESMITH_BUILD_CLUSTER=ON")
        return None

    topology = ts.GPUTopology()

    if not topology.discover():
        print("Could not discover GPU topology")
        return None

    info = topology.get_topology()
    print(f"\nGPU Count: {info.gpu_count}")
    print(f"Has NVSwitch: {info.has_nvswitch}")

    print("\nDevices:")
    for dev in info.devices:
        print(f"  GPU {dev.gpu_id}: {dev.name}")
        print(f"    PCI Bus: {dev.pci_bus_id}")
        print(f"    NUMA Node: {dev.numa_node}")
        if dev.has_nvlink:
            print(f"    NVLink Count: {dev.nvlink_count}")

    print("\nLinks:")
    for link in info.links:
        link_type = ts.link_type_to_string(link.type)
        print(f"  GPU {link.gpu_a} <-> GPU {link.gpu_b}: "
              f"{link_type} ({link.bandwidth_gbps:.1f} GB/s)")

    # ASCII visualization
    print("\nTopology Diagram:")
    print(topology.to_ascii())

    return topology


def profile_multi_gpu():
    """Profile multi-GPU operations."""
    print("\n" + "=" * 70)
    print("MULTI-GPU PROFILING")
    print("=" * 70)

    if not ts.is_cluster_available():
        print("\n⚠ Cluster module not available.")
        print("  Skipping multi-GPU profiling demo.")
        return None

    # Create configuration
    config = ts.MultiGPUConfig()
    config.per_gpu_buffer_size = 100000
    config.enable_nvlink_tracking = True
    config.enable_peer_access_tracking = True
    config.unified_timestamps = True
    config.capture_topology = True

    profiler = ts.MultiGPUProfiler(config)

    if not profiler.initialize():
        print("Could not initialize multi-GPU profiler")
        return None

    gpu_count = profiler.get_available_gpu_count()
    print(f"\nAvailable GPUs: {gpu_count}")

    if gpu_count < 2:
        print("Multi-GPU profiling requires at least 2 GPUs")
        profiler.finalize()
        return None

    # Add GPUs to profile
    for gpu_id in range(min(gpu_count, 4)):  # Profile up to 4 GPUs
        profiler.add_gpu(gpu_id)
        info = profiler.get_device_info(gpu_id)
        print(f"  GPU {gpu_id}: {info.name}")

    # Start capture
    print("\nStarting capture...")
    profiler.start_capture()

    # Run some operations (would be actual GPU work in real usage)
    time.sleep(0.5)

    profiler.stop_capture()

    # Get statistics
    stats = profiler.get_statistics()
    print(f"\nCapture Statistics:")
    print(f"  Total Events: {stats.total_events}")
    print(f"  Total Dropped: {stats.total_dropped}")
    print(f"  NVLink Transfers: {stats.nvlink_transfers}")
    print(f"  NVLink Bytes: {ts.format_bytes(stats.nvlink_bytes)}")
    print(f"  Peer Accesses: {stats.peer_accesses}")
    print(f"  Capture Duration: {stats.capture_duration_ms:.2f} ms")

    # Get events per GPU
    print("\nEvents per GPU:")
    for gpu_id, count in stats.events_per_gpu.items():
        print(f"  GPU {gpu_id}: {count} events")

    # Get all events
    all_events = profiler.get_events()
    print(f"\nTotal captured events: {len(all_events)}")

    # Export
    if all_events:
        ts.export_perfetto(all_events, "multi_gpu_trace.json")
        print("✓ Exported: multi_gpu_trace.json")

    profiler.finalize()
    return stats


def profile_data_parallel_training():
    """Profile PyTorch DataParallel training."""
    if not TORCH_AVAILABLE:
        print("\nPyTorch not available for DataParallel example")
        return

    if not torch.cuda.is_available():
        print("\nCUDA not available for DataParallel example")
        return

    gpu_count = torch.cuda.device_count()
    if gpu_count < 2:
        print(f"\nOnly {gpu_count} GPU available, need 2+ for DataParallel")
        return

    print("\n" + "=" * 70)
    print("DATA PARALLEL TRAINING PROFILING")
    print("=" * 70)

    print(f"\nUsing {gpu_count} GPUs")

    # Create model
    class SimpleCNN(nn.Module):
        def __init__(self):
            super().__init__()
            self.conv1 = nn.Conv2d(3, 64, 3, padding=1)
            self.conv2 = nn.Conv2d(64, 128, 3, padding=1)
            self.pool = nn.AdaptiveAvgPool2d(1)
            self.fc = nn.Linear(128, 10)

        def forward(self, x):
            x = torch.relu(self.conv1(x))
            x = torch.relu(self.conv2(x))
            x = self.pool(x)
            x = x.view(x.size(0), -1)
            return self.fc(x)

    # Setup multi-GPU model
    model = SimpleCNN()
    model = nn.DataParallel(model)
    model = model.cuda()

    optimizer = torch.optim.SGD(model.parameters(), lr=0.01)
    criterion = nn.CrossEntropyLoss()

    # Create batch (larger to benefit from multi-GPU)
    batch_size = 128 * gpu_count
    input_data = torch.randn(batch_size, 3, 32, 32).cuda()
    target = torch.randint(0, 10, (batch_size,)).cuda()

    # Setup profiler
    platform = ts.detect_platform()
    profiler = ts.create_profiler(platform)

    config = ts.ProfilerConfig()
    config.buffer_size = 200000
    config.capture_kernels = True
    config.capture_memcpy = True

    if not profiler.initialize(config):
        print("Could not initialize profiler")
        return

    # Warmup
    print("\nWarming up...")
    for _ in range(5):
        optimizer.zero_grad()
        output = model(input_data)
        loss = criterion(output, target)
        loss.backward()
        optimizer.step()
        torch.cuda.synchronize()

    # Profile
    print("Profiling DataParallel training...")
    torch.cuda.synchronize()
    profiler.start_capture()
    start_time = time.perf_counter()

    for _ in range(10):
        optimizer.zero_grad()
        output = model(input_data)
        loss = criterion(output, target)
        loss.backward()
        optimizer.step()

    torch.cuda.synchronize()
    end_time = time.perf_counter()
    profiler.stop_capture()

    events = profiler.get_events()
    wall_time = (end_time - start_time) * 1000

    print(f"\nResults:")
    print(f"  Wall time: {wall_time:.2f} ms for 10 iterations")
    print(f"  Per iteration: {wall_time / 10:.2f} ms")
    print(f"  Throughput: {batch_size * 10 / (wall_time / 1000):.0f} samples/sec")
    print(f"  Events captured: {len(events)}")

    # Analyze events by GPU
    events_by_device: Dict[int, List[ts.TraceEvent]] = {}
    for event in events:
        device_id = event.device_id
        if device_id not in events_by_device:
            events_by_device[device_id] = []
        events_by_device[device_id].append(event)

    print(f"\nEvents by GPU:")
    for device_id, device_events in sorted(events_by_device.items()):
        kernel_count = sum(1 for e in device_events 
                          if e.type == ts.EventType.KernelLaunch)
        memcpy_count = sum(1 for e in device_events 
                          if e.type in [ts.EventType.MemcpyH2D, 
                                        ts.EventType.MemcpyD2H,
                                        ts.EventType.MemcpyD2D])
        print(f"  GPU {device_id}: {len(device_events)} events "
              f"({kernel_count} kernels, {memcpy_count} memcpy)")

    # Export
    if events:
        ts.export_perfetto(events, "dataparallel_trace.json")
        print("\n✓ Exported: dataparallel_trace.json")

    profiler.finalize()


def analyze_multi_gpu_trace(events: List[ts.TraceEvent]):
    """Analyze multi-GPU trace for load balancing."""
    print("\n" + "-" * 60)
    print("Multi-GPU Load Balance Analysis")
    print("-" * 60)

    # Group events by GPU
    gpu_stats: Dict[int, Dict] = {}

    for event in events:
        gpu_id = event.device_id
        if gpu_id not in gpu_stats:
            gpu_stats[gpu_id] = {
                "kernel_count": 0,
                "kernel_time_ns": 0,
                "memcpy_count": 0,
                "memcpy_time_ns": 0,
                "sync_count": 0
            }

        stats = gpu_stats[gpu_id]

        if event.type == ts.EventType.KernelLaunch:
            stats["kernel_count"] += 1
            stats["kernel_time_ns"] += event.duration
        elif event.type in [ts.EventType.MemcpyH2D, ts.EventType.MemcpyD2H, 
                            ts.EventType.MemcpyD2D]:
            stats["memcpy_count"] += 1
            stats["memcpy_time_ns"] += event.duration
        elif event.type in [ts.EventType.StreamSync, ts.EventType.DeviceSync]:
            stats["sync_count"] += 1

    if not gpu_stats:
        print("No GPU events found")
        return

    # Print table
    print(f"{'GPU':>4} {'Kernels':>10} {'Kernel(ms)':>12} "
          f"{'Memcpy':>8} {'Memcpy(ms)':>12} {'Syncs':>8}")
    print("-" * 60)

    total_kernel_time = sum(s["kernel_time_ns"] for s in gpu_stats.values())

    for gpu_id in sorted(gpu_stats.keys()):
        stats = gpu_stats[gpu_id]
        pct = (stats["kernel_time_ns"] / total_kernel_time * 100 
               if total_kernel_time > 0 else 0)
        print(f"{gpu_id:>4} {stats['kernel_count']:>10} "
              f"{stats['kernel_time_ns']/1e6:>10.2f}ms "
              f"{stats['memcpy_count']:>8} "
              f"{stats['memcpy_time_ns']/1e6:>10.2f}ms "
              f"{stats['sync_count']:>8}")

    # Check load balance
    if len(gpu_stats) > 1:
        kernel_times = [s["kernel_time_ns"] for s in gpu_stats.values()]
        avg_time = sum(kernel_times) / len(kernel_times)
        max_time = max(kernel_times)
        min_time = min(kernel_times)

        imbalance = (max_time - min_time) / avg_time * 100 if avg_time > 0 else 0

        print(f"\nLoad Balance:")
        print(f"  Average kernel time: {avg_time / 1e6:.2f} ms")
        print(f"  Max imbalance: {imbalance:.1f}%")

        if imbalance > 20:
            print("  ⚠️  Significant load imbalance detected!")
        else:
            print("  ✓ Load is reasonably balanced")


def main():
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith - Multi-GPU Profiling                         ║")
    print("╚═══════════════════════════════════════════════════════════╝\n")

    platform = ts.detect_platform()
    print(f"Platform: {ts.platform_type_to_string(platform)}")

    # Check NVML availability
    if ts.is_nvml_available():
        print(f"NVML Version: {ts.get_nvml_version()}")
    else:
        print("NVML not available")

    if TORCH_AVAILABLE:
        print(f"PyTorch: {torch.__version__}")
        if torch.cuda.is_available():
            gpu_count = torch.cuda.device_count()
            print(f"CUDA GPUs: {gpu_count}")
            for i in range(gpu_count):
                print(f"  GPU {i}: {torch.cuda.get_device_name(i)}")
    else:
        print("PyTorch not available")

    # Discover topology
    topology = discover_gpu_topology()

    # Multi-GPU profiling
    profile_multi_gpu()

    # DataParallel training (if PyTorch available with multiple GPUs)
    profile_data_parallel_training()

    print("\n" + "=" * 60)
    print("Multi-GPU Profiling Complete!")
    print("=" * 60)


if __name__ == "__main__":
    main()
