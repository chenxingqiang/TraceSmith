#!/usr/bin/env python3
"""
TraceSmith MetaX GPU Profiling Example

Demonstrates how to use TraceSmith Python bindings with MetaX GPUs (C500, C550)
using the MCPTI (MACA Profiling Tools Interface) backend.

This example shows:
- MetaX GPU detection
- Device information retrieval
- GPU event profiling with torch_maca (PyTorch for MACA)
- Exporting traces to various formats

Requirements:
- TraceSmith built with MACA support
- MetaX MACA SDK installed
- Optional: torch_maca for GPU compute examples
"""

import sys
import os
import time

# Add TraceSmith to path if needed
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

try:
    import tracesmith as ts
except ImportError:
    print("Error: TraceSmith Python bindings not found")
    print("Build with: cmake -DTRACESMITH_BUILD_PYTHON=ON ..")
    sys.exit(1)


def print_separator(title=""):
    """Print a section separator"""
    print()
    if title:
        print(f"=== {title} ===")
    print("-" * 50)


def print_device_info(info):
    """Print device information"""
    print(f"  Device ID:     {info.device_id}")
    print(f"  Name:          {info.name}")
    print(f"  Vendor:        {info.vendor}")
    print(f"  Compute:       {info.compute_major}.{info.compute_minor}")
    print(f"  Memory:        {info.total_memory // (1024*1024)} MB")
    print(f"  CUs:           {info.multiprocessor_count}")
    print(f"  Clock:         {info.clock_rate // 1000} MHz")


def check_maca_available():
    """Check if MetaX MACA is available"""
    print_separator("Platform Detection")
    
    if ts.is_maca_available():
        print("MetaX GPU: DETECTED")
        print(f"Driver version: {ts.get_maca_driver_version()}")
        print(f"Device count: {ts.get_maca_device_count()}")
        return True
    else:
        print("MetaX GPU: NOT DETECTED")
        print("Make sure MetaX driver is loaded")
        return False


def run_maca_profiling():
    """Run profiling on MetaX GPU"""
    print_separator("Initialize MCPTI Profiler")
    
    # Create profiler for MACA platform
    profiler = ts.create_profiler(ts.PlatformType.MACA)
    if profiler is None:
        print("Failed to create MCPTI profiler")
        return
    
    print(f"Platform: {profiler.platform_type()}")
    
    # Get device info
    devices = profiler.get_device_info()
    print(f"\nFound {len(devices)} MetaX GPU(s):")
    for dev in devices:
        print_device_info(dev)
    
    # Configure profiler
    config = ts.ProfilerConfig()
    config.capture_kernels = True
    config.capture_memcpy = True
    config.capture_memset = True
    config.capture_sync = True
    config.buffer_size = 1024 * 1024
    
    if not profiler.initialize(config):
        print("Failed to initialize profiler")
        return
    
    print("\nProfiler initialized successfully")
    
    # Start capture
    print_separator("Capture GPU Events")
    print("Starting capture...")
    
    if not profiler.start_capture():
        print("Failed to start capture")
        return
    
    # Run GPU workload
    print("\nRunning GPU workload...")
    run_gpu_workload()
    
    # Stop capture
    print("\nStopping capture...")
    profiler.stop_capture()
    
    print(f"Events captured: {profiler.events_captured()}")
    print(f"Events dropped: {profiler.events_dropped()}")
    
    # Retrieve events
    print_separator("Analyze Captured Events")
    events = profiler.get_events()
    print(f"Retrieved {len(events)} events")
    
    # Count by type
    type_counts = {}
    for ev in events:
        event_type = str(ev.type)
        type_counts[event_type] = type_counts.get(event_type, 0) + 1
    
    print("\nEvents by type:")
    for event_type, count in sorted(type_counts.items()):
        print(f"  {event_type:20s}: {count}")
    
    # Print first few events
    print("\nFirst 10 events:")
    for i, ev in enumerate(events[:10]):
        print(f"  [{i:3d}] {str(ev.type):20s} | {ev.name}")
    
    # Export to files
    print_separator("Export Trace Files")
    
    # Export to SBT format
    writer = ts.SBTWriter("metax_trace.sbt")
    writer.write_header()
    for ev in events:
        writer.write_event(ev)
    writer.finalize()
    print("Saved to metax_trace.sbt")
    
    # Export to Perfetto JSON
    exporter = ts.PerfettoExporter()
    exporter.export_to_file(events, "metax_trace.json")
    print("Saved to metax_trace.json")
    print("View at: https://ui.perfetto.dev")
    
    # Cleanup
    profiler.finalize()


def run_gpu_workload():
    """Run a simple GPU workload using torch_maca or MACA runtime"""
    try:
        # Try torch_maca (PyTorch for MACA)
        import torch
        
        # Check if MACA backend is available
        if hasattr(torch, 'maca') and torch.maca.is_available():
            print("  Using torch_maca backend")
            device = torch.device('maca')
            
            # Matrix multiplication
            size = 1024
            a = torch.randn(size, size, device=device)
            b = torch.randn(size, size, device=device)
            
            print(f"  Created {size}x{size} matrices on GPU")
            
            # Perform operations
            for i in range(5):
                c = torch.matmul(a, b)
                torch.maca.synchronize()
            
            print("  Performed 5 matrix multiplications")
            
            # Memory operations
            d = torch.zeros(size, size, device=device)
            e = d.to('cpu')
            print("  Performed memory operations")
            
        else:
            print("  torch_maca not available, using simulated workload")
            simulate_workload()
            
    except ImportError:
        print("  PyTorch not available, using simulated workload")
        simulate_workload()


def simulate_workload():
    """Simulate GPU workload with synthetic events"""
    print("  Simulating GPU operations...")
    
    # Simulate some GPU-like delays
    for i in range(5):
        time.sleep(0.01)  # 10ms per "kernel"
    
    print("  Simulated 5 kernel executions")


def main():
    """Main entry point"""
    print("TraceSmith MetaX GPU Profiling Example")
    print(f"Version: {ts.__version__}")
    
    # Check MACA availability
    if not check_maca_available():
        print("\nRunning in simulation mode (no MetaX GPU detected)")
        print("To use real GPU profiling, run on a MetaX system")
        
    # Run profiling
    try:
        run_maca_profiling()
    except Exception as e:
        print(f"\nError during profiling: {e}")
        import traceback
        traceback.print_exc()
    
    print_separator("Example Complete")


if __name__ == "__main__":
    main()
