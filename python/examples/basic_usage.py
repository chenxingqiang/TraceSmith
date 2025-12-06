#!/usr/bin/env python3
"""
TraceSmith Python Example - Real GPU Profiling

Demonstrates basic usage of TraceSmith Python bindings for:
- Real GPU event capture (CUDA/ROCm/Metal)
- Timeline building
- Perfetto export
- Trace replay

Supports: CUDA, ROCm, Metal (MPS), CPU fallback
"""

import argparse
import tracesmith as ts

# Import device utilities for cross-platform support
try:
    from device_utils import DeviceManager, get_device_manager, print_device_info
    DEVICE_UTILS_AVAILABLE = True
except ImportError:
    DEVICE_UTILS_AVAILABLE = False


def main(device: str = None):
    print("╔═══════════════════════════════════════════════════════════╗")
    print("║  TraceSmith Python Example - Real GPU Profiling           ║")
    print("╚═══════════════════════════════════════════════════════════╝")
    print(f"\nVersion: {ts.__version__}")
    print()

    # 1. Detect GPU platform with device preference
    print("1. Detecting GPU platform...")
    
    if DEVICE_UTILS_AVAILABLE:
        dm = DeviceManager(prefer_device=device)
        platform = dm.get_tracesmith_platform() or ts.detect_platform()
        print(f"   Device: {dm.get_device_name()}")
    else:
        platform = ts.detect_platform()
    
    print(f"   Platform: {ts.platform_type_to_string(platform)}")

    if platform == ts.PlatformType.Unknown:
        print("   ⚠ No GPU detected, creating sample events for demo...")
        events = create_sample_events()
    else:
        # Real GPU profiling
        print(f"   ✓ Found {ts.platform_type_to_string(platform)} GPU")

        # Check specific platform
        if ts.is_cuda_available():
            print(f"   CUDA devices: {ts.get_cuda_device_count()}")
            print(f"   CUDA driver: {ts.get_cuda_driver_version()}")
        elif ts.is_metal_available():
            print(f"   Metal devices: {ts.get_metal_device_count()}")

        print("\n2. Creating profiler for real GPU...")
        profiler = ts.create_profiler(platform)

        config = ts.ProfilerConfig()
        config.buffer_size = 100000
        config.capture_kernels = True
        config.capture_memcpy = True

        if not profiler.initialize(config):
            print("   ✗ Failed to initialize profiler")
            events = create_sample_events()
        else:
            print("   ✓ Profiler initialized")

            print("\n3. Capturing GPU events...")
            profiler.start_capture()

            # Generate some GPU activity if PyTorch is available
            if DEVICE_UTILS_AVAILABLE:
                print("   Running GPU workload...")
                run_gpu_workload(dm)
            else:
                import time
                time.sleep(0.1)  # Brief capture window

            profiler.stop_capture()
            events = profiler.get_events()
            print(f"   Captured {len(events)} events")
            print(f"   Events dropped: {profiler.events_dropped()}")

            if len(events) == 0:
                print("   ⚠ No GPU activity detected, using sample events...")
                events = create_sample_events()

    # Show first few events
    if events:
        print("\n   Sample events:")
        for i, event in enumerate(events[:5]):
            print(f"     [{i}] {event.name} (type={event.type}, stream={event.stream_id})")
    print()

    # 4. Build timeline
    print("4. Building timeline...")
    timeline = ts.build_timeline(events)
    print(f"   Total duration: {timeline.total_duration / 1e6:.2f} ms")
    print(f"   GPU utilization: {timeline.gpu_utilization * 100:.1f}%")
    print(f"   Max concurrent ops: {timeline.max_concurrent_ops}")
    print()

    # 5. Export to Perfetto
    print("5. Exporting to Perfetto format...")
    if ts.export_perfetto(events, "python_trace.json"):
        print("   ✓ Saved: python_trace.json")
        print("   View at: https://ui.perfetto.dev/")
    print()

    # 6. Save to SBT format
    print("6. Saving trace to SBT format...")
    writer = ts.SBTWriter("python_trace.sbt")

    metadata = ts.TraceMetadata()
    metadata.application_name = "PythonExample"
    if events:
        metadata.start_time = events[0].timestamp
        metadata.end_time = events[-1].timestamp
    writer.write_metadata(metadata)

    device = ts.DeviceInfo()
    device.device_id = 0
    device.name = ts.platform_type_to_string(platform) + " GPU"
    device.vendor = "TraceSmith"
    writer.write_device_info([device])

    writer.write_events(events)
    writer.finalize()
    import os
    file_size = os.path.getsize("python_trace.sbt") if os.path.exists("python_trace.sbt") else 0
    print(f"   ✓ Saved: python_trace.sbt ({file_size} bytes)")

    # 7. Load and verify
    print("\n7. Loading trace from file...")
    reader = ts.SBTReader("python_trace.sbt")
    if reader.is_valid():
        loaded_events = reader.read_all()
        print(f"   ✓ Loaded {len(loaded_events)} events")
    print()

    print("═" * 60)
    print("Python Example Complete!")
    print()
    print("Generated files:")
    print("  - python_trace.json (Perfetto format)")
    print("  - python_trace.sbt (TraceSmith binary format)")


def create_sample_events():
    """Create sample events when no GPU is available."""
    events = []
    base_time = ts.get_current_timestamp()

    kernel_names = ["matmul_kernel", "conv2d_forward", "relu_activation",
                    "batch_norm", "softmax_kernel"]

    for i in range(20):
        event = ts.TraceEvent()
        event.type = ts.EventType.KernelLaunch
        event.name = kernel_names[i % len(kernel_names)]
        event.timestamp = base_time + i * 100000  # 100us intervals
        event.duration = 50000 + (i * 1000)  # 50-70us
        event.stream_id = i % 2
        event.device_id = 0
        event.correlation_id = i
        events.append(event)

    return events


def run_gpu_workload(dm):
    """Run a simple GPU workload to generate events."""
    try:
        import torch
        
        # Matrix multiplication workload
        size = 1000
        a = dm.randn(size, size)
        b = dm.randn(size, size)
        
        for _ in range(10):
            c = torch.mm(a, b)
            a = c
        
        dm.synchronize()
    except ImportError:
        import time
        time.sleep(0.1)
    except Exception as e:
        print(f"   Warning: GPU workload failed: {e}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="TraceSmith Basic Usage Example")
    parser.add_argument(
        "--device", "-d",
        choices=["cuda", "mps", "rocm", "cpu"],
        default=None,
        help="Preferred device (default: auto-detect)"
    )
    args = parser.parse_args()
    
    main(device=args.device)
