#!/usr/bin/env python3
"""
TraceSmith Python API Example

Demonstrates the complete Python API for GPU profiling:
- Platform detection
- Creating trace events
- SBT file I/O
- Perfetto export
- Memory profiling
- Stack capture
- Timeline analysis
"""

import tracesmith as ts
import time
import random
import os
import tempfile

def print_section(title):
    """Print a formatted section header."""
    print(f"\n{'=' * 60}")
    print(f"  {title}")
    print('=' * 60)


def demo_platform_detection():
    """Demonstrate platform detection capabilities."""
    print_section("Platform Detection")
    
    print(f"TraceSmith Version: {ts.__version__}")
    print(f"Detected Platform: {ts.detect_platform()}")
    print()
    
    # Check CUDA availability
    cuda_available = ts.is_cuda_available()
    print(f"CUDA Available: {cuda_available}")
    if cuda_available:
        print(f"  Device Count: {ts.get_cuda_device_count()}")
        print(f"  Driver Version: {ts.get_cuda_driver_version()}")
    
    # Check Metal availability
    metal_available = ts.is_metal_available()
    print(f"Metal Available: {metal_available}")
    if metal_available:
        print(f"  Device Count: {ts.get_metal_device_count()}")


def demo_trace_events():
    """Demonstrate creating and manipulating trace events."""
    print_section("Trace Events")
    
    events = []
    base_time = ts.get_current_timestamp()
    current_time = base_time
    
    # Create various event types
    print("Creating sample events...")
    
    # Kernel launch event
    kernel = ts.TraceEvent()
    kernel.name = "matrix_multiply"
    kernel.type = ts.EventType.KernelLaunch
    kernel.timestamp = current_time
    kernel.duration = 500000  # 500 µs
    kernel.device_id = 0
    kernel.stream_id = 0
    kernel.correlation_id = 1
    events.append(kernel)
    current_time += kernel.duration + 10000
    
    # Memory copy event
    memcpy = ts.TraceEvent()
    memcpy.name = "upload_weights"
    memcpy.type = ts.EventType.MemcpyH2D
    memcpy.timestamp = current_time
    memcpy.duration = 100000  # 100 µs
    memcpy.device_id = 0
    memcpy.stream_id = 1
    memcpy.correlation_id = 2
    events.append(memcpy)
    current_time += memcpy.duration + 10000
    
    # Another kernel
    kernel2 = ts.TraceEvent()
    kernel2.name = "relu_activation"
    kernel2.type = ts.EventType.KernelLaunch
    kernel2.timestamp = current_time
    kernel2.duration = 200000  # 200 µs
    kernel2.device_id = 0
    kernel2.stream_id = 0
    kernel2.correlation_id = 3
    events.append(kernel2)
    
    print(f"Created {len(events)} events:")
    for i, e in enumerate(events):
        print(f"  [{i}] {e.name} - {ts.event_type_to_string(e.type)} "
              f"({e.duration / 1000:.1f} µs)")
    
    return events


def demo_sbt_file_io(events):
    """Demonstrate SBT file reading and writing."""
    print_section("SBT File I/O")
    
    # Create temporary file
    with tempfile.NamedTemporaryFile(suffix='.sbt', delete=False) as f:
        filename = f.name
    
    try:
        # Write events
        print(f"Writing to {filename}...")
        writer = ts.SBTWriter(filename)
        
        # Write metadata
        metadata = ts.TraceMetadata()
        metadata.application_name = "python_example"
        metadata.command_line = "python python_example.py"
        metadata.start_time = events[0].timestamp if events else 0
        metadata.end_time = events[-1].timestamp if events else 0
        writer.write_metadata(metadata)
        
        # Write device info
        device = ts.DeviceInfo()
        device.device_id = 0
        device.name = "Example GPU"
        device.vendor = "TraceSmith"
        device.total_memory = 8 * 1024 * 1024 * 1024  # 8GB
        device.multiprocessor_count = 80
        device.compute_major = 8
        device.compute_minor = 6
        writer.write_device_info([device])
        
        # Write events
        for event in events:
            writer.write_event(event)
        writer.finalize()
        
        print(f"  Wrote {writer.event_count()} events")
        
        # Read events back
        print(f"\nReading from {filename}...")
        reader = ts.SBTReader(filename)
        
        if reader.is_valid():
            read_events = reader.read_all()
            print(f"  Read {len(read_events)} events")
            
            # Filter by type manually
            kernel_events = [e for e in read_events if e.type == ts.EventType.KernelLaunch]
            memcpy_events = [e for e in read_events if e.type == ts.EventType.MemcpyH2D]
            print(f"  Kernels: {len(kernel_events)}")
            print(f"  Memory copies: {len(memcpy_events)}")
        else:
            print("  Invalid SBT file")
    finally:
        os.unlink(filename)


def demo_perfetto_export(events):
    """Demonstrate Perfetto format export."""
    print_section("Perfetto Export")
    
    # Create temporary file
    with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as f:
        filename = f.name
    
    try:
        exporter = ts.PerfettoExporter()
        exporter.set_enable_gpu_tracks(True)
        exporter.set_enable_flow_events(True)
        
        if exporter.export_to_file(events, filename):
            print(f"✓ Exported to {filename}")
            file_size = os.path.getsize(filename)
            print(f"  File size: {file_size} bytes")
            print(f"  View at: https://ui.perfetto.dev")
        else:
            print("✗ Export failed")
    finally:
        os.unlink(filename)


def demo_memory_profiler():
    """Demonstrate memory profiling capabilities."""
    print_section("Memory Profiler")
    
    # Configure profiler
    config = ts.MemoryProfilerConfig()
    config.track_call_stacks = False
    config.detect_double_free = True
    config.snapshot_interval_ms = 100
    
    profiler = ts.MemoryProfiler(config)
    profiler.start()
    
    # Simulate allocations
    print("Simulating memory operations...")
    
    addresses = []
    
    for i in range(5):
        addr = 0x10000000 + i * 0x100000
        size = (1 + i) * 1024 * 1024  # 1-5 MB
        
        profiler.record_alloc(addr, size, 0)
        addresses.append(addr)
        print(f"  Allocated: 0x{addr:x} ({size // 1024 // 1024} MB)")
    
    # Take snapshot
    snapshot = profiler.take_snapshot()
    print(f"\nSnapshot: {snapshot.live_allocations} live allocations")
    print(f"  Live bytes: {snapshot.live_bytes // 1024 // 1024} MB")
    print(f"  Total allocated: {snapshot.total_allocated // 1024 // 1024} MB")
    
    # Free some memory
    for addr in addresses[:2]:
        profiler.record_free(addr)
        print(f"  Freed: 0x{addr:x}")
    
    profiler.stop()
    
    # Generate report
    report = profiler.generate_report()
    print(f"\nMemory Report:")
    print(f"  Current usage: {report.current_memory_usage // 1024 // 1024} MB")
    print(f"  Peak usage: {report.peak_memory_usage // 1024 // 1024} MB")
    print(f"  Total allocations: {report.total_allocations}")
    print(f"  Total frees: {report.total_frees}")
    
    # Detect leaks (remaining allocations)
    leaks = profiler.detect_leaks()
    if leaks:
        print(f"\n  Potential leaks: {len(leaks)}")


def demo_stack_capture():
    """Demonstrate stack capture capabilities."""
    print_section("Stack Capture")
    
    if not ts.StackCapture.is_available():
        print("Stack capture not available on this platform")
        return
    
    print(f"Current thread ID: {ts.StackCapture.get_current_thread_id()}")
    
    # Configure capture
    config = ts.StackCaptureConfig()
    config.max_depth = 32
    config.resolve_symbols = True
    config.demangle = True
    
    # Create capturer
    capture = ts.StackCapture(config)
    
    # Capture call stack
    def inner_function():
        return capture.capture()
    
    def outer_function():
        return inner_function()
    
    call_stack = outer_function()
    
    print(f"\nCaptured {len(call_stack.frames)} frames:")
    for i, frame in enumerate(call_stack.frames[:8]):
        func_name = frame.function_name or "<unknown>"
        if len(func_name) > 50:
            func_name = func_name[:47] + "..."
        print(f"  [{i}] 0x{frame.address:012x} {func_name}")
    
    if len(call_stack.frames) > 8:
        print(f"  ... and {len(call_stack.frames) - 8} more frames")
    
    # Performance test
    print("\nPerformance test (100 captures)...")
    start = time.time()
    for _ in range(100):
        capture.capture()
    elapsed = time.time() - start
    print(f"  Average: {elapsed * 10:.2f} ms/capture")


def demo_timeline_analysis(events):
    """Demonstrate timeline building and analysis."""
    print_section("Timeline Analysis")
    
    # Build timeline
    builder = ts.TimelineBuilder()
    for event in events:
        builder.add_event(event)
    
    timeline = builder.build()
    
    print(f"Timeline Statistics:")
    print(f"  Spans: {len(timeline.spans)}")
    print(f"  Total duration: {timeline.total_duration / 1000:.1f} µs")
    print(f"  GPU utilization: {timeline.gpu_utilization * 100:.1f}%")
    print(f"  Max concurrent ops: {timeline.max_concurrent_ops}")
    
    # ASCII visualization
    print("\nTimeline Viewer:")
    view_config = ts.TimelineViewConfig()
    view_config.width = 50
    view_config.max_rows = 8
    
    viewer = ts.TimelineViewer(view_config)
    print(viewer.render(timeline))


def demo_state_machine(events):
    """Demonstrate GPU state machine analysis."""
    print_section("GPU State Machine")
    
    state_machine = ts.GPUStateMachine()
    
    # Process events
    for event in events:
        state_machine.process_event(event)
    
    # Get statistics
    stats = state_machine.get_statistics()
    print(f"State Machine Statistics:")
    print(f"  Total events: {stats.total_events}")
    print(f"  Total transitions: {stats.total_transitions}")
    print(f"  GPU utilization: {stats.overall_utilization * 100:.1f}%")
    
    # Show stream states
    print("\nStream States:")
    for device_id, stream_id in state_machine.get_all_streams():
        stream_state = state_machine.get_stream_state(device_id, stream_id)
        if stream_state:
            state = stream_state.current_state()
            state_name = str(state).split('.')[-1]  # GPUState.Idle -> Idle
            utilization = min(stream_state.utilization() * 100, 100.0)  # Cap at 100%
            print(f"  Device {device_id}, Stream {stream_id}: {state_name} "
                  f"({utilization:.1f}% utilized)")


def demo_counter_events():
    """Demonstrate counter events for metrics."""
    print_section("Counter Events (Metrics)")
    
    # Create counter events
    counters = []
    base_time = ts.get_current_timestamp()
    
    metrics = [
        ("GPU Memory (GB)", 4.2, "GB"),
        ("SM Occupancy", 78.5, "%"),
        ("Power (W)", 185.0, "W"),
        ("Temperature", 67.0, "°C"),
    ]
    
    for i in range(5):
        for name, base_value, unit in metrics:
            counter = ts.CounterEvent()
            counter.counter_name = name
            counter.value = base_value + random.uniform(-5, 5)
            counter.timestamp = base_time + i * 100000000  # 100ms intervals
            counter.unit = unit
            counters.append(counter)
    
    print(f"Created {len(counters)} counter samples")
    print("\nSample values:")
    for metric_name, _, unit in metrics:
        values = [c.value for c in counters if c.counter_name == metric_name]
        avg = sum(values) / len(values)
        print(f"  {metric_name}: {avg:.2f} {unit} (avg)")
    
    return counters


def main():
    """Main entry point."""
    print("\n" + "=" * 60)
    print("  TraceSmith Python API Example")
    print("=" * 60)
    print(f"\nVersion: {ts.__version__}")
    
    # Run demonstrations
    demo_platform_detection()
    events = demo_trace_events()
    demo_sbt_file_io(events)
    demo_perfetto_export(events)
    demo_memory_profiler()
    demo_stack_capture()
    demo_timeline_analysis(events)
    demo_state_machine(events)
    counters = demo_counter_events()
    
    # Summary
    print_section("Summary")
    print("""
Features Demonstrated:
  ✓ Platform detection (CUDA, Metal, ROCm)
  ✓ Creating trace events
  ✓ SBT file reading and writing
  ✓ Perfetto JSON export
  ✓ Memory profiling
  ✓ Stack capture
  ✓ Timeline building and visualization
  ✓ GPU state machine analysis
  ✓ Counter events for metrics

Next Steps:
  - Use with real GPU profilers (CUPTI, Metal)
  - Integrate with PyTorch/TensorFlow profiling
  - Export traces for analysis in Perfetto UI
  - Build custom analysis tools

Documentation:
  https://github.com/chenxingqiang/TraceSmith
""")


if __name__ == "__main__":
    main()

