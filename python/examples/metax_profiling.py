#!/usr/bin/env python3
"""
MetaX GPU Profiling Example

This example demonstrates how to use TraceSmith with MetaX GPUs
(C500, C550, etc.) using the MCPTI profiling interface.

Requirements:
    - MetaX GPU with MACA driver
    - TraceSmith built with MACA support
    - PyTorch with MACA support (optional, for deep learning profiling)

Usage:
    python metax_profiling.py
    python metax_profiling.py --device maca
"""

import os
import sys
import time
import argparse
from datetime import datetime

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

try:
    import tracesmith as ts
except ImportError:
    print("TraceSmith not found. Please install it first:")
    print("  pip install tracesmith")
    sys.exit(1)


def print_separator(title: str):
    """Print a section separator."""
    print(f"\n{'=' * 60}")
    print(f"{title}")
    print('=' * 60)


def check_metax_availability():
    """Check if MetaX GPU is available."""
    print_separator("MetaX GPU Detection")
    
    is_available = ts.is_maca_available()
    print(f"MetaX MACA Available: {is_available}")
    
    if is_available:
        device_count = ts.get_maca_device_count()
        driver_version = ts.get_maca_driver_version()
        print(f"Device Count: {device_count}")
        print(f"Driver Version: {driver_version}")
        return True
    else:
        print("\nMetaX GPU not detected. Possible reasons:")
        print("  1. No MetaX GPU installed")
        print("  2. MACA driver not loaded (run 'mx-smi' to check)")
        print("  3. TraceSmith not built with MACA support")
        return False


def profile_pytorch_metax():
    """Profile PyTorch operations on MetaX GPU."""
    print_separator("PyTorch MetaX Profiling")
    
    try:
        import torch
    except ImportError:
        print("PyTorch not available. Skipping PyTorch profiling.")
        return None
    
    # Check for MACA/HIP backend (MetaX uses HIP-compatible interface)
    if not hasattr(torch, 'maca') and not hasattr(torch.cuda, 'is_available'):
        print("PyTorch MACA backend not available.")
        return None
    
    # Try to use MACA device
    try:
        # MetaX may use 'cuda' or 'maca' device string depending on PyTorch build
        if hasattr(torch, 'maca'):
            device = torch.device('maca:0')
        else:
            device = torch.device('cuda:0')
        
        # Warm up
        x = torch.randn(1000, 1000, device=device)
        y = torch.randn(1000, 1000, device=device)
        _ = torch.mm(x, y)
        torch.cuda.synchronize() if hasattr(torch.cuda, 'synchronize') else None
        
        print(f"Using device: {device}")
        print(f"Device name: {torch.cuda.get_device_name(0) if hasattr(torch.cuda, 'get_device_name') else 'Unknown'}")
        
    except Exception as e:
        print(f"Failed to initialize MACA device: {e}")
        return None
    
    # Create profiler
    try:
        profiler = ts.create_profiler(ts.PlatformType.MACA)
        if profiler is None:
            print("Failed to create MACA profiler")
            return None
    except Exception as e:
        print(f"Failed to create profiler: {e}")
        return None
    
    # Configure
    config = ts.ProfilerConfig()
    config.capture_kernels = True
    config.capture_memcpy = True
    profiler.initialize(config)
    
    print("\nStarting capture...")
    profiler.start_capture()
    
    # Run workload
    print("Running matrix operations...")
    
    sizes = [512, 1024, 2048]
    for size in sizes:
        x = torch.randn(size, size, device=device)
        y = torch.randn(size, size, device=device)
        
        # Matrix multiplication
        z = torch.mm(x, y)
        
        # Element-wise operations
        z = z + x
        z = z * 2.0
        z = torch.relu(z)
        
        # Reduction
        result = z.sum()
        
        torch.cuda.synchronize() if hasattr(torch.cuda, 'synchronize') else None
        print(f"  Matrix size {size}x{size}: sum = {result.item():.4f}")
    
    profiler.stop_capture()
    print("Capture stopped.")
    
    # Get events
    events = profiler.get_events()
    return events


def profile_native_maca():
    """Profile native MACA operations (without PyTorch)."""
    print_separator("Native MACA Profiling")
    
    # Create profiler
    try:
        profiler = ts.create_profiler(ts.PlatformType.MACA)
        if profiler is None:
            print("Failed to create MACA profiler")
            print("Note: Native MACA profiling requires the MACA SDK.")
            return None
    except Exception as e:
        print(f"Failed to create profiler: {e}")
        return None
    
    # Get device info
    devices = profiler.get_device_info()
    print(f"Detected {len(devices)} device(s):")
    for dev in devices:
        print(f"  Device {dev.device_id}: {dev.name}")
        print(f"    Vendor: {dev.vendor}")
        print(f"    Memory: {dev.total_memory / (1024**3):.2f} GB")
    
    # Configure profiler
    config = ts.ProfilerConfig()
    config.capture_kernels = True
    config.capture_memcpy = True
    config.capture_memset = True
    config.capture_sync = True
    
    if not profiler.initialize(config):
        print("Failed to initialize profiler")
        return None
    
    print("\nStarting capture...")
    profiler.start_capture()
    
    # Note: Without direct MACA API access from Python,
    # we can only capture events from external MACA programs.
    # The profiler will capture any MACA activity on the system.
    
    print("Waiting for GPU activity (2 seconds)...")
    time.sleep(2)
    
    profiler.stop_capture()
    print("Capture stopped.")
    
    # Get events
    events = profiler.get_events()
    return events


def analyze_events(events):
    """Analyze captured events."""
    print_separator("Event Analysis")
    
    if not events:
        print("No events captured.")
        return
    
    print(f"Total events: {len(events)}")
    
    # Count by type
    type_counts = {}
    for event in events:
        event_type = str(event.type).split('.')[-1]
        type_counts[event_type] = type_counts.get(event_type, 0) + 1
    
    print("\nEvents by type:")
    for event_type, count in sorted(type_counts.items()):
        print(f"  {event_type}: {count}")
    
    # Calculate statistics
    kernel_events = [e for e in events if 'Kernel' in str(e.type)]
    if kernel_events:
        print(f"\nKernel Statistics:")
        print(f"  Total kernels: {len(kernel_events)}")
        
        # Group by name
        kernel_names = {}
        for e in kernel_events:
            name = e.name if hasattr(e, 'name') else 'unknown'
            kernel_names[name] = kernel_names.get(name, 0) + 1
        
        print("  Top kernels:")
        for name, count in sorted(kernel_names.items(), key=lambda x: -x[1])[:5]:
            print(f"    {name}: {count}")
    
    # Memory events
    memcpy_events = [e for e in events if 'Memcpy' in str(e.type)]
    if memcpy_events:
        print(f"\nMemory Transfer Statistics:")
        print(f"  Total transfers: {len(memcpy_events)}")


def save_results(events, output_prefix: str = "metax_trace"):
    """Save profiling results."""
    print_separator("Saving Results")
    
    if not events:
        print("No events to save.")
        return
    
    # Save to SBT format
    sbt_file = f"{output_prefix}.sbt"
    writer = ts.SBTWriter(sbt_file)
    writer.write_events(events)
    writer.finalize()
    print(f"SBT trace saved: {sbt_file}")
    
    # Export to Perfetto JSON
    json_file = f"{output_prefix}.json"
    exporter = ts.PerfettoExporter()
    exporter.export_events(events, json_file)
    print(f"Perfetto JSON saved: {json_file}")
    print(f"View at: https://ui.perfetto.dev")


def main():
    parser = argparse.ArgumentParser(
        description="MetaX GPU Profiling Example"
    )
    parser.add_argument(
        '--mode', '-m',
        choices=['pytorch', 'native', 'auto'],
        default='auto',
        help='Profiling mode (default: auto)'
    )
    parser.add_argument(
        '--output', '-o',
        default='metax_trace',
        help='Output file prefix (default: metax_trace)'
    )
    args = parser.parse_args()
    
    print("TraceSmith MetaX GPU Profiling Example")
    print(f"Version: {ts.__version__}")
    print(f"Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print('=' * 60)
    
    # Check MetaX availability
    if not check_metax_availability():
        print("\nRunning in demo mode (no actual GPU profiling)...")
        return 1
    
    # Profile based on mode
    events = None
    
    if args.mode in ['pytorch', 'auto']:
        events = profile_pytorch_metax()
        if events is None and args.mode == 'pytorch':
            print("PyTorch profiling failed.")
            return 1
    
    if events is None and args.mode in ['native', 'auto']:
        events = profile_native_maca()
    
    if events:
        analyze_events(events)
        save_results(events, args.output)
    else:
        print("\nNo events captured. This could be because:")
        print("  1. No GPU activity during capture")
        print("  2. Profiler initialization failed")
        print("  3. Permission issues with GPU access")
    
    print_separator("Example Complete")
    return 0


if __name__ == '__main__':
    sys.exit(main())
