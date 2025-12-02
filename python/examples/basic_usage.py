#!/usr/bin/env python3
"""
TraceSmith Python Example

Demonstrates basic usage of TraceSmith Python bindings for:
- Event capture
- Timeline building
- Perfetto export
- Trace replay
"""

import tracesmith as ts
import time


def main():
    print("TraceSmith Python Example")
    print("=" * 40)
    print(f"Version: {ts.__version__}")
    print()
    
    # 1. Capture events using simulation profiler
    print("1. Capturing events...")
    profiler = ts.SimulationProfiler()
    config = ts.ProfilerConfig()
    config.capture_callstacks = False
    profiler.initialize(config)
    profiler.start_capture()
    
    # Let the simulation run
    time.sleep(0.5)
    
    profiler.stop_capture()
    events = profiler.get_events()
    print(f"   Captured {len(events)} events")
    
    # Show first few events
    print("\n   Sample events:")
    for event in events[:5]:
        print(f"     - {event}")
    print()
    
    # 2. Build timeline
    print("2. Building timeline...")
    timeline = ts.build_timeline(events)
    print(f"   Total duration: {timeline.total_duration / 1e6:.2f} ms")
    print(f"   GPU utilization: {timeline.gpu_utilization * 100:.1f}%")
    print(f"   Max concurrent ops: {timeline.max_concurrent_ops}")
    print()
    
    # 3. Export to Perfetto
    print("3. Exporting to Perfetto format...")
    if ts.export_perfetto(events, "python_trace.json"):
        print("   Saved: python_trace.json")
        print("   View at: chrome://tracing")
    print()
    
    # 4. Replay trace
    print("4. Replaying trace...")
    result = ts.replay_trace(events, ts.ReplayMode.Full)
    print(f"   Success: {result.success}")
    print(f"   Operations: {result.operations_executed}/{result.operations_total}")
    print(f"   Deterministic: {result.deterministic}")
    print()
    
    # 5. Dry-run replay
    print("5. Dry-run replay (no execution)...")
    result_dry = ts.replay_trace(events, ts.ReplayMode.DryRun)
    print(f"   Replay duration: {result_dry.replay_duration / 1000:.1f} µs")
    print()
    
    # 6. Save and load trace
    print("6. Saving trace to SBT format...")
    writer = ts.SBTWriter("python_trace.sbt")
    
    metadata = ts.TraceMetadata()
    metadata.application_name = "PythonExample"
    if events:
        metadata.start_time = events[0].timestamp
        metadata.end_time = events[-1].timestamp
    writer.write_metadata(metadata)
    
    devices = [ts.DeviceInfo()]
    devices[0].device_id = 0
    devices[0].name = "Simulation GPU"
    devices[0].vendor = "TraceSmith"
    writer.write_device_info(devices)
    
    writer.write_events(events)
    writer.finalize()
    print("   Saved: python_trace.sbt")
    
    # Load and verify
    print("\n7. Loading trace from file...")
    reader = ts.SBTReader("python_trace.sbt")
    if reader.is_valid():
        loaded_metadata, loaded_events = reader.read_all()
        print(f"   Loaded {len(loaded_events)} events")
        print(f"   Application: {loaded_metadata.application_name}")
    print()
    
    print("=" * 40)
    print("Python Example Complete!")
    print()
    print("Generated files:")
    print("  - python_trace.json (Perfetto format)")
    print("  - python_trace.sbt (TraceSmith format)")


if __name__ == "__main__":
    main()
