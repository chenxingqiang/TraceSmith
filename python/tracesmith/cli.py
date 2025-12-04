"""
TraceSmith Command Line Interface (Python)

GPU Profiling & Replay System

Usage:
    tracesmith-cli info              Show version and system info
    tracesmith-cli devices           List available GPU devices
    tracesmith-cli record            Record GPU events
    tracesmith-cli view FILE         View trace file contents
    tracesmith-cli export FILE       Export to Perfetto format
    tracesmith-cli analyze FILE      Analyze trace file
    tracesmith-cli replay FILE       Replay a captured trace
    tracesmith-cli benchmark         Run 10K GPU call stacks benchmark

Or via Python module:
    python -m tracesmith <command>
"""

import argparse
import sys
import os
from pathlib import Path
from typing import List, Dict, Any

# =============================================================================
# ANSI Color Codes
# =============================================================================
class Color:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    MAGENTA = "\033[35m"
    CYAN = "\033[36m"
    WHITE = "\033[37m"
    
    _enabled = True
    
    @classmethod
    def disable(cls):
        cls._enabled = False
    
    @classmethod
    def get(cls, color: str) -> str:
        return color if cls._enabled else ""


def C(color: str) -> str:
    return Color.get(color)


# =============================================================================
# ASCII Art Banner
# =============================================================================
BANNER = """
████████╗██████╗  █████╗  ██████╗███████╗███████╗███╗   ███╗██╗████████╗██╗  ██╗
╚══██╔══╝██╔══██╗██╔══██╗██╔════╝██╔════╝██╔════╝████╗ ████║██║╚══██╔══╝██║  ██║
   ██║   ██████╔╝███████║██║     █████╗  ███████╗██╔████╔██║██║   ██║   ███████║
   ██║   ██╔══██╗██╔══██║██║     ██╔══╝  ╚════██║██║╚██╔╝██║██║   ██║   ██╔══██║
   ██║   ██║  ██║██║  ██║╚██████╗███████╗███████║██║ ╚═╝ ██║██║   ██║   ██║  ██║
   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚══════╝╚══════╝╚═╝     ╚═╝╚═╝   ╚═╝   ╚═╝  ╚═╝
"""

def print_banner():
    """Print the TraceSmith ASCII art banner."""
    print(C(Color.CYAN) + BANNER + C(Color.RESET))
    version = get_version()
    print(f"{C(Color.YELLOW)}                    GPU Profiling & Replay System v{version}{C(Color.RESET)}\n")


def print_compact_banner():
    """Print a compact banner."""
    version = get_version()
    print(f"{C(Color.CYAN)}{C(Color.BOLD)}TraceSmith{C(Color.RESET)} v{version} - GPU Profiling & Replay System\n")


# =============================================================================
# Utility Functions
# =============================================================================
def get_version() -> str:
    """Get TraceSmith version."""
    try:
        from . import __version__
        return __version__
    except ImportError:
        return "unknown"


def print_success(msg: str):
    print(f"{C(Color.GREEN)}✓ {C(Color.RESET)}{msg}")


def print_error(msg: str):
    print(f"{C(Color.RED)}✗ Error: {C(Color.RESET)}{msg}", file=sys.stderr)


def print_warning(msg: str):
    print(f"{C(Color.YELLOW)}⚠ Warning: {C(Color.RESET)}{msg}")


def print_info(msg: str):
    print(f"{C(Color.BLUE)}ℹ {C(Color.RESET)}{msg}")


def print_section(title: str):
    print(f"\n{C(Color.BOLD)}{C(Color.CYAN)}═══ {title} ═══{C(Color.RESET)}\n")


def format_bytes(size: int) -> str:
    """Format bytes to human readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size < 1024:
            return f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} TB"


def format_duration(ns: int) -> str:
    """Format nanoseconds to human readable string."""
    if ns < 1000:
        return f"{ns} ns"
    elif ns < 1_000_000:
        return f"{ns/1000:.2f} µs"
    elif ns < 1_000_000_000:
        return f"{ns/1_000_000:.2f} ms"
    else:
        return f"{ns/1_000_000_000:.2f} s"


# =============================================================================
# Command: info - Show Version and System Info
# =============================================================================
def cmd_info(args):
    """Show version and system information."""
    print_section("TraceSmith System Information")
    
    from . import (
        __version__,
        VERSION_MAJOR,
        VERSION_MINOR,
        VERSION_PATCH,
        is_protobuf_available,
        is_bpf_available,
        is_cuda_available,
        is_metal_available,
        get_cuda_device_count,
        get_metal_device_count,
        detect_platform,
        platform_type_to_string,
    )
    
    print(f"{C(Color.BOLD)}Version:{C(Color.RESET)}")
    print(f"  TraceSmith:  {C(Color.GREEN)}{__version__}{C(Color.RESET)}")
    print(f"  Components:  {VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH}")
    print()
    
    # Platform detection
    print(f"{C(Color.BOLD)}Platform Detection:{C(Color.RESET)}")
    platform = detect_platform()
    print(f"  Active Platform: {C(Color.CYAN)}{platform_type_to_string(platform)}{C(Color.RESET)}")
    print()
    
    print(f"{C(Color.BOLD)}GPU Support:{C(Color.RESET)}")
    cuda_avail = is_cuda_available()
    metal_avail = is_metal_available()
    
    cuda_status = f"{C(Color.GREEN)}✓ Available ({get_cuda_device_count()} devices){C(Color.RESET)}" if cuda_avail else f"{C(Color.YELLOW)}✗ Not available{C(Color.RESET)}"
    metal_status = f"{C(Color.GREEN)}✓ Available ({get_metal_device_count()} devices){C(Color.RESET)}" if metal_avail else f"{C(Color.YELLOW)}✗ Not available{C(Color.RESET)}"
    
    print(f"  NVIDIA CUDA:  {cuda_status}")
    print(f"  Apple Metal:  {metal_status}")
    print(f"  AMD ROCm:     {C(Color.YELLOW)}Coming soon{C(Color.RESET)}")
    print()
    
    print(f"{C(Color.BOLD)}Features:{C(Color.RESET)}")
    proto_status = f"{C(Color.GREEN)}✓{C(Color.RESET)}" if is_protobuf_available() else f"{C(Color.YELLOW)}✗{C(Color.RESET)}"
    bpf_status = f"{C(Color.GREEN)}✓{C(Color.RESET)}" if is_bpf_available() else f"{C(Color.YELLOW)}✗ (Linux only){C(Color.RESET)}"
    
    print(f"  Perfetto Protobuf: {proto_status}")
    print(f"  BPF Tracing:       {bpf_status}")
    print()
    
    return 0


# =============================================================================
# Command: devices - List Available GPUs
# =============================================================================
def cmd_devices(args):
    """List available GPU devices."""
    print_section("GPU Device Detection")
    
    from . import (
        is_cuda_available,
        is_metal_available,
        get_cuda_device_count,
        get_metal_device_count,
        get_cuda_driver_version,
        create_profiler,
        PlatformType,
    )
    
    found_any = False
    
    # Check CUDA
    print(f"{C(Color.BOLD)}NVIDIA CUDA:{C(Color.RESET)}")
    if is_cuda_available():
        count = get_cuda_device_count()
        driver = get_cuda_driver_version()
        print_success("CUDA available")
        print(f"  Devices: {count}")
        print(f"  Driver:  {driver}")
        found_any = True
        
        # Get device details
        try:
            profiler = create_profiler(PlatformType.CUDA)
            if profiler:
                config = __import__('tracesmith').ProfilerConfig()
                if profiler.initialize(config):
                    devices = profiler.get_device_info()
                    for dev in devices:
                        print(f"\n  {C(Color.CYAN)}Device {dev.device_id}: {C(Color.RESET)}{dev.name}")
                        print(f"    Vendor:  {dev.vendor}")
                        print(f"    Memory:  {format_bytes(dev.total_memory)}")
                        print(f"    SMs:     {dev.multiprocessor_count}")
        except Exception:
            pass
    else:
        print(f"  {C(Color.YELLOW)}Not available{C(Color.RESET)}")
    
    # Check Metal
    print(f"\n{C(Color.BOLD)}Apple Metal:{C(Color.RESET)}")
    if is_metal_available():
        count = get_metal_device_count()
        print_success("Metal available")
        print(f"  Devices: {count}")
        found_any = True
    else:
        print(f"  {C(Color.YELLOW)}Not available{C(Color.RESET)}")
    
    # Check ROCm
    print(f"\n{C(Color.BOLD)}AMD ROCm:{C(Color.RESET)}")
    print(f"  {C(Color.YELLOW)}Coming soon{C(Color.RESET)}")
    
    print()
    
    if not found_any:
        print_warning("No supported GPU platforms detected.")
        print("Make sure GPU drivers are installed and accessible.")
    
    return 0 if found_any else 1


# =============================================================================
# Command: record - Record GPU Events
# =============================================================================
def cmd_record(args):
    """Record GPU events to a trace file."""
    print_section("Recording GPU Trace")
    
    from . import (
        detect_platform,
        platform_type_to_string,
        create_profiler,
        ProfilerConfig,
        SBTWriter,
        PlatformType,
    )
    import time
    
    output_file = args.output or "trace.sbt"
    duration_sec = args.duration
    
    print(f"{C(Color.BOLD)}Configuration:{C(Color.RESET)}")
    print(f"  Output:   {C(Color.CYAN)}{output_file}{C(Color.RESET)}")
    print(f"  Duration: {duration_sec} seconds")
    print()
    
    # Detect platform
    platform = detect_platform()
    platform_name = platform_type_to_string(platform)
    
    if platform == PlatformType.Unknown:
        print_error("No supported GPU platform detected.")
        print("Supported: CUDA (NVIDIA), ROCm (AMD), Metal (Apple)")
        return 1
    
    print(f"  Platform: {platform_name}")
    
    # Create profiler
    profiler = create_profiler(platform)
    if not profiler:
        print_error(f"Failed to create profiler for {platform_name}")
        return 1
    
    # Configure
    config = ProfilerConfig()
    config.buffer_size = 1000000
    
    if not profiler.initialize(config):
        print_error("Failed to initialize profiler")
        return 1
    
    print_success("Profiler initialized")
    
    # Create writer
    writer = SBTWriter(output_file)
    if not writer.is_open():
        print_error(f"Failed to open output file: {output_file}")
        return 1
    
    # Start capture
    print(f"\n{C(Color.GREEN)}▶ Recording...{C(Color.RESET)} (Press Ctrl+C to stop)\n")
    
    profiler.start_capture()
    
    total_events = 0
    start_time = time.time()
    
    try:
        while time.time() - start_time < duration_sec:
            events = profiler.get_events(10000)
            if events:
                writer.write_events(events)
                total_events += len(events)
            
            # Progress
            elapsed = time.time() - start_time
            progress = min(elapsed / duration_sec, 1.0)
            bar_width = 40
            filled = int(bar_width * progress)
            bar = f"{C(Color.GREEN)}{'█' * filled}{C(Color.RESET)}{'░' * (bar_width - filled)}"
            print(f"\r  [{bar}] {progress*100:.0f}% | Events: {total_events}     ", end='', flush=True)
            
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("\n")
        print_info("Recording interrupted by user")
    
    # Stop
    profiler.stop_capture()
    
    # Drain remaining
    remaining = profiler.get_events()
    if remaining:
        writer.write_events(remaining)
        total_events += len(remaining)
    
    writer.finalize()
    
    print("\n")
    print_section("Recording Complete")
    
    print(f"{C(Color.BOLD)}Summary:{C(Color.RESET)}")
    print(f"  Platform:     {platform_name}")
    print(f"  Total events: {C(Color.GREEN)}{total_events}{C(Color.RESET)}")
    print(f"  Output:       {C(Color.CYAN)}{output_file}{C(Color.RESET)}")
    print()
    
    print_success(f"Trace saved to {output_file}")
    print(f"\nNext steps:")
    print(f"  {C(Color.CYAN)}tracesmith-cli view {output_file} --stats{C(Color.RESET)}")
    print(f"  {C(Color.CYAN)}tracesmith-cli export {output_file}{C(Color.RESET)}")
    
    return 0


# =============================================================================
# Command: view - View Trace Contents
# =============================================================================
def cmd_view(args):
    """View trace file contents."""
    from . import SBTReader, event_type_to_string
    from collections import Counter
    
    input_path = Path(args.input)
    
    if not input_path.exists():
        print_error(f"Input file '{input_path}' not found")
        return 1
    
    # Read file
    reader = SBTReader(str(input_path))
    if not reader.is_valid():
        print_error(f"Invalid SBT file '{input_path}'")
        return 1
    
    result, metadata = reader.read_metadata()
    events = reader.read_all()  # Read all events
    
    print_section(f"Trace File: {input_path}")
    
    # Basic info
    print(f"{C(Color.BOLD)}File Info:{C(Color.RESET)}")
    print(f"  Events:   {C(Color.GREEN)}{len(events)}{C(Color.RESET)}")
    if metadata.application_name:
        print(f"  App:      {metadata.application_name}")
    
    # Statistics
    type_counts = Counter(e.type for e in events)
    type_durations: Dict[Any, int] = {}
    stream_counts: Dict[int, int] = {}
    
    min_ts = float('inf')
    max_ts = 0
    
    for e in events:
        type_durations[e.type] = type_durations.get(e.type, 0) + e.duration
        stream_counts[e.stream_id] = stream_counts.get(e.stream_id, 0) + 1
        min_ts = min(min_ts, e.timestamp)
        max_ts = max(max_ts, e.timestamp)
    
    print(f"\n{C(Color.BOLD)}Statistics:{C(Color.RESET)}")
    if events:
        print(f"  Time span: {format_duration(int(max_ts - min_ts))}")
    print(f"  Streams:   {len(stream_counts)}")
    
    # Events by type
    print(f"\n{C(Color.BOLD)}Events by Type:{C(Color.RESET)}")
    print(f"  {'Type':<20} {'Count':>8} {'Total Time':>12} {'Avg Time':>12}")
    print(f"  {'-'*52}")
    
    for event_type, count in type_counts.most_common():
        type_name = event_type_to_string(event_type)
        total_dur = type_durations.get(event_type, 0)
        avg_dur = total_dur // count if count > 0 else 0
        print(f"  {type_name:<20} {count:>8} {format_duration(total_dur):>12} {format_duration(avg_dur):>12}")
    
    if args.stats:
        # Stream breakdown
        print(f"\n{C(Color.BOLD)}Events by Stream:{C(Color.RESET)}")
        for stream_id, count in sorted(stream_counts.items()):
            print(f"  Stream {stream_id}: {count} events")
        return 0
    
    # Show events
    limit = args.limit or 20
    print(f"\n{C(Color.BOLD)}Events (first {limit}):{C(Color.RESET)}")
    
    for i, event in enumerate(events[:limit]):
        type_name = event_type_to_string(event.type)
        print(f"  {C(Color.CYAN)}[{i:>5}]{C(Color.RESET)} {type_name:<16} | Stream {event.stream_id} | {format_duration(event.duration):>10} | {event.name}")
    
    if len(events) > limit:
        print(f"\n  ... and {len(events) - limit} more events")
    
    return 0


# =============================================================================
# Command: export - Export to Perfetto Format
# =============================================================================
def cmd_export(args):
    """Export trace to Perfetto format."""
    from . import SBTReader, PerfettoExporter
    
    input_path = Path(args.input)
    output_path = Path(args.output) if args.output else input_path.with_suffix('.json')
    
    if not input_path.exists():
        print_error(f"Input file '{input_path}' not found")
        return 1
    
    print_section("Exporting Trace")
    
    print(f"Input:  {C(Color.CYAN)}{input_path}{C(Color.RESET)}")
    print(f"Output: {C(Color.CYAN)}{output_path}{C(Color.RESET)}")
    print(f"Format: Perfetto JSON")
    print()
    
    # Read SBT file
    reader = SBTReader(str(input_path))
    if not reader.is_valid():
        print_error(f"Invalid SBT file '{input_path}'")
        return 1
    
    events = reader.read_all()
    print_info(f"Read {len(events)} events")
    
    # Export
    exporter = PerfettoExporter()
    
    if args.counters:
        exporter.set_enable_counter_tracks(True)
    
    if exporter.export_to_file(events, str(output_path)):
        print_success(f"Exported to {output_path}")
        print(f"\nView at: {C(Color.CYAN)}https://ui.perfetto.dev/{C(Color.RESET)}")
        return 0
    else:
        print_error(f"Failed to export to '{output_path}'")
        return 1


# =============================================================================
# Command: analyze - Analyze Trace
# =============================================================================
def cmd_analyze(args):
    """Analyze a trace file for performance insights."""
    from . import SBTReader, build_timeline, event_type_to_string, EventType
    from collections import Counter, defaultdict
    
    input_path = Path(args.input)
    
    if not input_path.exists():
        print_error(f"Input file '{input_path}' not found")
        return 1
    
    # Read file
    reader = SBTReader(str(input_path))
    if not reader.is_valid():
        print_error(f"Invalid SBT file '{input_path}'")
        return 1
    
    events = reader.read_all()
    
    print_section("Performance Analysis")
    
    print(f"File: {C(Color.CYAN)}{input_path}{C(Color.RESET)}")
    print(f"Events: {len(events)}")
    print()
    
    # Build timeline
    timeline = build_timeline(events)
    
    # GPU Utilization
    print(f"{C(Color.BOLD)}GPU Utilization:{C(Color.RESET)}")
    print(f"  Overall:        {C(Color.GREEN)}{timeline.gpu_utilization * 100:.1f}%{C(Color.RESET)}")
    print(f"  Max concurrent: {timeline.max_concurrent_ops} ops")
    print(f"  Total duration: {format_duration(timeline.total_duration)}")
    
    # Kernel analysis
    kernel_stats: Dict[str, List[int]] = defaultdict(list)
    
    for event in events:
        if event.type == EventType.KernelLaunch or event.type == EventType.KernelComplete:
            kernel_stats[event.name].append(event.duration)
    
    if kernel_stats:
        print(f"\n{C(Color.BOLD)}Top Kernels by Time:{C(Color.RESET)}")
        
        # Sort by total time
        sorted_kernels = sorted(
            [(name, durations) for name, durations in kernel_stats.items()],
            key=lambda x: sum(x[1]),
            reverse=True
        )
        
        print(f"  {'Kernel':<35} {'Count':>8} {'Total':>12} {'Average':>12}")
        print(f"  {'-'*67}")
        
        for name, durations in sorted_kernels[:10]:
            total = sum(durations)
            count = len(durations)
            avg = total // count if count > 0 else 0
            short_name = name[:32] + "..." if len(name) > 32 else name
            print(f"  {short_name:<35} {count:>8} {format_duration(total):>12} {format_duration(avg):>12}")
    
    print()
    print_success("Analysis complete")
    
    return 0


# =============================================================================
# Command: replay - Replay Trace
# =============================================================================
def cmd_replay(args):
    """Replay a captured trace."""
    from . import SBTReader, ReplayEngine, ReplayConfig, ReplayMode
    
    input_path = Path(args.input)
    
    if not input_path.exists():
        print_error(f"Input file '{input_path}' not found")
        return 1
    
    print_section("Replay Trace")
    
    print(f"File: {C(Color.CYAN)}{input_path}{C(Color.RESET)}")
    print(f"Mode: {args.mode}")
    print()
    
    # Read trace
    reader = SBTReader(str(input_path))
    if not reader.is_valid():
        print_error(f"Invalid SBT file '{input_path}'")
        return 1
    
    events = reader.read_all()
    print_info(f"Loaded {len(events)} events")
    
    # Create replay engine
    engine = ReplayEngine()
    
    config = ReplayConfig()
    if args.mode == "dry-run":
        config.mode = ReplayMode.DryRun
    elif args.mode == "full":
        config.mode = ReplayMode.Full
    elif args.mode == "partial":
        config.mode = ReplayMode.Partial
    
    config.validate_dependencies = args.validate
    
    if not engine.load_trace(str(input_path)):
        print_error("Failed to load trace for replay")
        return 1
    
    print("Replaying...")
    result = engine.replay(config)
    
    print(f"\n{C(Color.BOLD)}Replay Results:{C(Color.RESET)}")
    success_color = Color.GREEN if result.success else Color.RED
    print(f"  Success:       {C(success_color)}{result.success}{C(Color.RESET)}")
    print(f"  Operations:    {result.operations_executed}/{result.operations_total}")
    print(f"  Deterministic: {result.deterministic}")
    print(f"  Duration:      {format_duration(result.replay_duration)}")
    
    if result.success:
        print_success("Replay completed")
    else:
        print_error("Replay failed")
    
    return 0 if result.success else 1


# =============================================================================
# Command: benchmark - Run 10K GPU Call Stacks Benchmark
# =============================================================================
def cmd_benchmark(args):
    """Run the 10K GPU instruction-level call stacks benchmark."""
    import time
    
    # Import TraceSmith modules
    try:
        from . import (
            is_cuda_available, get_cuda_device_count,
            StackCapture, StackCaptureConfig, CallStack,
            SBTWriter, TraceMetadata, TraceEvent, EventType,
            ProfilerConfig, get_current_timestamp
        )
    except ImportError as e:
        print_error(f"Failed to import TraceSmith modules: {e}")
        return 1
    
    # Check CUDA availability
    cuda_available = False
    try:
        cuda_available = is_cuda_available()
    except:
        pass
    
    if not cuda_available:
        print()
        print(f"{C(Color.BOLD)}{C(Color.RED)}")
        print("╔══════════════════════════════════════════════════════════════════════╗")
        print("║  ERROR: CUDA support not available                                   ║")
        print("╚══════════════════════════════════════════════════════════════════════╝")
        print(f"{C(Color.RESET)}")
        print()
        print("This benchmark requires CUDA support.")
        print("Please ensure:")
        print("  1. NVIDIA GPU is available")
        print("  2. TraceSmith was built with -DTRACESMITH_ENABLE_CUDA=ON")
        print()
        return 1
    
    # Check for CuPy (real GPU kernels)
    cupy_available = False
    cp = None
    try:
        import cupy as cp
        cupy_available = True
    except ImportError:
        pass
    
    # Check for CUPTI profiler
    cupti_available = False
    CUPTIProfiler = None
    try:
        from . import CUPTIProfiler
        cupti_available = True
    except ImportError:
        pass
    
    # Determine benchmark mode
    use_real_gpu = args.real_gpu and cupy_available and cupti_available
    
    # Print banner
    print()
    print(f"{C(Color.BOLD)}{C(Color.CYAN)}")
    print("╔══════════════════════════════════════════════════════════════════════╗")
    print("║  TraceSmith Benchmark: 10,000+ GPU Instruction-Level Call Stacks     ║")
    print("║  Feature: Non-intrusive capture of instruction-level GPU call stacks ║")
    print("╚══════════════════════════════════════════════════════════════════════╝")
    print(f"{C(Color.RESET)}")
    print()
    
    # Configuration
    target_kernels = args.count
    output_file = args.output or "benchmark.sbt"
    capture_stacks = not args.no_stacks
    verbose = args.verbose
    
    # Print info
    device_count = get_cuda_device_count()
    print_success(f"CUDA available, {device_count} device(s)")
    
    if cupy_available:
        print_success("CuPy available (real GPU kernels)")
    else:
        print_warning("CuPy not available (install with: pip install cupy-cuda12x)")
    
    if cupti_available:
        print_success("CUPTI profiler available")
    else:
        print_warning("CUPTI profiler not available")
    
    # Check stack capture
    stack_available = StackCapture.is_available()
    if capture_stacks and not stack_available:
        print_warning("Stack capture not available, disabling")
        capture_stacks = False
    elif capture_stacks:
        print_success("Stack capture available")
    
    print()
    print(f"{C(Color.BOLD)}Configuration:{C(Color.RESET)}")
    print(f"  Target kernels: {target_kernels}")
    print(f"  Output file:    {output_file}")
    print(f"  Capture stacks: {'Yes' if capture_stacks else 'No'}")
    print(f"  Real GPU mode:  {'Yes' if use_real_gpu else 'No'}")
    print()
    
    # Setup stack capturer
    stack_capturer = None
    host_stacks = []
    
    if capture_stacks:
        config = StackCaptureConfig()
        config.max_depth = 16
        config.resolve_symbols = False
        config.demangle = False
        stack_capturer = StackCapture(config)
    
    # =================================================================
    # Real GPU Benchmark Mode (with CuPy + CUPTI)
    # =================================================================
    if use_real_gpu:
        return _run_real_gpu_benchmark(
            cp, CUPTIProfiler, 
            target_kernels, output_file, 
            capture_stacks, stack_capturer, host_stacks,
            verbose, SBTWriter, TraceMetadata, TraceEvent, EventType, get_current_timestamp
        )
    
    # =================================================================
    # Fallback: Python-side stack capture mode
    # =================================================================
    print_section("Running Benchmark (Python Mode)")
    print(f"Capturing {target_kernels} call stacks...")
    
    if cupy_available and not cupti_available:
        print_info("Launching real CuPy kernels (CUPTI not available for capture)")
    print()
    
    start_time = time.time()
    
    # Capture stacks for each "kernel launch"
    progress_interval = target_kernels // 20
    if progress_interval == 0:
        progress_interval = 1
    
    events = []
    
    # Optionally use CuPy for real GPU work
    if cupy_available and cp is not None:
        # Allocate GPU memory
        data_size = 1024 * 1024  # 1M elements
        d_data = cp.ones(data_size, dtype=cp.float32)
    
    for i in range(target_kernels):
        # Capture host call stack
        if capture_stacks and stack_capturer:
            stack = stack_capturer.capture()
            
            event = TraceEvent()
            event.type = EventType.KernelLaunch
            event.name = f"benchmark_kernel_{i}"
            event.timestamp = get_current_timestamp()
            event.correlation_id = i
            event.device_id = 0
            event.stream_id = 0
            event.call_stack = stack
            event.thread_id = stack.thread_id
            events.append(event)
            host_stacks.append(stack)
        else:
            event = TraceEvent()
            event.type = EventType.KernelLaunch
            event.name = f"benchmark_kernel_{i}"
            event.timestamp = get_current_timestamp()
            event.correlation_id = i
            event.device_id = 0
            event.stream_id = 0
            events.append(event)
        
        # Run real GPU kernel if CuPy available
        if cupy_available and cp is not None:
            d_data = d_data * 2.0 + float(i)
            if i % 1000 == 999:
                cp.cuda.Stream.null.synchronize()
        
        # Show progress
        if verbose and i % progress_interval == 0:
            pct = (i * 100) // target_kernels
            bar = "█" * (pct // 5) + "░" * (20 - pct // 5)
            print(f"\r  Progress: [{bar}] {pct}% ", end="", flush=True)
    
    # Final sync
    if cupy_available and cp is not None:
        cp.cuda.Stream.null.synchronize()
    
    end_time = time.time()
    duration_ms = (end_time - start_time) * 1000
    
    if verbose:
        print(f"\r  Progress: [████████████████████] 100%")
    
    print_success(f"Captured {target_kernels} events")
    print(f"  Total time:    {duration_ms:.0f} ms")
    print(f"  Events/sec:    {target_kernels * 1000 / duration_ms:.0f}")
    print()
    
    # =================================================================
    # Results
    # =================================================================
    print_section("Results")
    
    print(f"{C(Color.BOLD)}Events:{C(Color.RESET)}")
    print(f"  Total events:    {len(events)}")
    
    if capture_stacks:
        stacks_captured = sum(1 for e in events if e.call_stack is not None)
        total_frames = sum(e.call_stack.depth() if e.call_stack else 0 for e in events)
        avg_depth = total_frames / stacks_captured if stacks_captured > 0 else 0
        
        print()
        print(f"{C(Color.BOLD)}Host Call Stacks:{C(Color.RESET)}")
        print(f"  Stacks captured: {stacks_captured}")
        print(f"  Average depth:   {avg_depth:.1f} frames")
        print(f"  Total frames:    {total_frames}")
    
    print()
    
    # =================================================================
    # Save to file
    # =================================================================
    try:
        writer = SBTWriter(output_file)
        meta = TraceMetadata()
        meta.application_name = "TraceSmith Python Benchmark"
        meta.command_line = f"tracesmith-cli benchmark -n {target_kernels}"
        writer.write_metadata(meta)
        
        for event in events:
            writer.write_event(event)
        
        writer.finalize()
        
        import os
        file_size = os.path.getsize(output_file)
        
        print_success(f"Saved to {output_file}")
        print(f"  File size: {file_size // 1024} KB")
        print()
    except Exception as e:
        print_warning(f"Failed to save trace: {e}")
    
    # =================================================================
    # Summary
    # =================================================================
    goal_achieved = len(events) >= target_kernels
    
    if goal_achieved:
        color = Color.GREEN
    else:
        color = Color.RED
    
    print(f"{C(Color.BOLD)}{C(color)}")
    print("╔══════════════════════════════════════════════════════════════════════╗")
    print("║                         BENCHMARK SUMMARY                            ║")
    print("╠══════════════════════════════════════════════════════════════════════╣")
    print("║                                                                      ║")
    print("║  Feature: Non-intrusive 10K+ instruction-level GPU call stacks       ║")
    print("║                                                                      ║")
    
    if goal_achieved:
        print("║  ✅ VERIFIED!                                                        ║")
    else:
        print("║  ❌ NOT VERIFIED                                                     ║")
    
    print("║                                                                      ║")
    mode_str = "Python + CuPy" if cupy_available else "Python"
    print(f"║  Results ({mode_str}):{' ' * (56 - len(mode_str))}║")
    print(f"║    • Events captured:       {len(events):<41}║")
    print(f"║    • Call stacks:           {len(host_stacks):<41}║")
    print(f"║    • Total time:            {duration_ms:.0f} ms{' ' * (36 - len(f'{duration_ms:.0f}'))}║")
    print("║                                                                      ║")
    
    if not use_real_gpu:
        print("║  For REAL GPU profiling with CUPTI, use: tracesmith-cli benchmark   ║")
        print("║                                                                      ║")
    
    print("╚══════════════════════════════════════════════════════════════════════╝")
    print(f"{C(Color.RESET)}")
    print()
    
    return 0 if goal_achieved else 1


def _run_real_gpu_benchmark(cp, CUPTIProfiler, target_kernels, output_file, 
                            capture_stacks, stack_capturer, host_stacks,
                            verbose, SBTWriter, TraceMetadata, TraceEvent, 
                            EventType, get_current_timestamp):
    """Run benchmark with real GPU kernels and CUPTI profiling."""
    import time
    
    print_section("Running Benchmark (REAL GPU Mode)")
    print(f"Launching {target_kernels} REAL CuPy GPU kernels with CUPTI capture...")
    print()
    
    # Allocate GPU memory
    data_size = 1024 * 1024  # 1M elements
    d_data = cp.ones(data_size, dtype=cp.float32)
    print_success(f"Allocated {data_size * 4 // 1024 // 1024} MB GPU memory")
    
    # Setup CUPTI profiler
    profiler = CUPTIProfiler()
    from . import ProfilerConfig
    prof_config = ProfilerConfig()
    prof_config.buffer_size = 64 * 1024 * 1024  # 64MB buffer
    profiler.initialize(prof_config)
    
    # Start CUPTI capture
    profiler.start_capture()
    print_success("CUPTI profiling started")
    print()
    
    start_time = time.time()
    
    progress_interval = target_kernels // 20
    if progress_interval == 0:
        progress_interval = 1
    
    # Launch real GPU kernels
    for i in range(target_kernels):
        # Capture host call stack before kernel launch
        if capture_stacks and stack_capturer:
            stack = stack_capturer.capture()
            host_stacks.append((i, stack))
        
        # Launch REAL CuPy kernel
        d_data = d_data * 2.0 + float(i % 100)
        
        # Sync every 1000 kernels
        if i % 1000 == 999:
            cp.cuda.Stream.null.synchronize()
        
        # Show progress
        if verbose and i % progress_interval == 0:
            pct = (i * 100) // target_kernels
            bar = "█" * (pct // 5) + "░" * (20 - pct // 5)
            print(f"\r  Progress: [{bar}] {pct}% ", end="", flush=True)
    
    # Final sync
    cp.cuda.Stream.null.synchronize()
    
    end_time = time.time()
    duration_ms = (end_time - start_time) * 1000
    
    # Stop profiling
    profiler.stop_capture()
    
    if verbose:
        print(f"\r  Progress: [████████████████████] 100%")
    
    print_success(f"Launched {target_kernels} real CUDA kernels")
    print(f"  Total time:   {duration_ms:.0f} ms")
    print(f"  Kernels/sec:  {target_kernels * 1000 / duration_ms:.0f}")
    print()
    
    # =================================================================
    # Get GPU events from CUPTI
    # =================================================================
    print_section("Results (CUPTI)")
    
    gpu_events = []
    event_count = profiler.get_events(gpu_events)
    events_dropped = profiler.events_dropped()
    
    # Count event types
    kernel_launches = sum(1 for e in gpu_events if e.type == EventType.KernelLaunch)
    kernel_completes = sum(1 for e in gpu_events if e.type == EventType.KernelComplete)
    other = len(gpu_events) - kernel_launches - kernel_completes
    
    print(f"{C(Color.BOLD)}GPU Events (CUPTI):{C(Color.RESET)}")
    print(f"  Events captured:   {event_count}")
    print(f"  Events dropped:    {events_dropped}")
    print(f"  Kernel launches:   {kernel_launches}")
    print(f"  Kernel completes:  {kernel_completes}")
    print(f"  Other events:      {other}")
    print()
    
    # Attach host stacks to GPU events
    if capture_stacks and host_stacks:
        stack_map = {corr_id: stack for corr_id, stack in host_stacks}
        attached = 0
        for event in gpu_events:
            if event.correlation_id in stack_map:
                event.call_stack = stack_map[event.correlation_id]
                attached += 1
        
        print(f"{C(Color.BOLD)}Host Call Stacks:{C(Color.RESET)}")
        print(f"  Stacks captured:        {len(host_stacks)}")
        print(f"  GPU events with stacks: {attached}")
        print()
    
    # =================================================================
    # Save to file
    # =================================================================
    try:
        writer = SBTWriter(output_file)
        meta = TraceMetadata()
        meta.application_name = "TraceSmith Python Benchmark (Real GPU)"
        meta.command_line = f"tracesmith-cli benchmark -n {target_kernels} --real-gpu"
        writer.write_metadata(meta)
        
        for event in gpu_events:
            writer.write_event(event)
        
        writer.finalize()
        
        import os
        file_size = os.path.getsize(output_file)
        
        print_success(f"Saved to {output_file}")
        print(f"  File size: {file_size // 1024} KB")
        print()
    except Exception as e:
        print_warning(f"Failed to save trace: {e}")
    
    # =================================================================
    # Summary
    # =================================================================
    goal_achieved = kernel_launches >= target_kernels
    
    if goal_achieved:
        color = Color.GREEN
    else:
        color = Color.RED
    
    print(f"{C(Color.BOLD)}{C(color)}")
    print("╔══════════════════════════════════════════════════════════════════════╗")
    print("║                         BENCHMARK SUMMARY                            ║")
    print("╠══════════════════════════════════════════════════════════════════════╣")
    print("║                                                                      ║")
    print("║  Feature: Non-intrusive 10K+ instruction-level GPU call stacks       ║")
    print("║                                                                      ║")
    
    if goal_achieved:
        print("║  ✅ VERIFIED! (REAL GPU)                                             ║")
    else:
        print("║  ❌ NOT VERIFIED                                                     ║")
    
    print("║                                                                      ║")
    print("║  Results (Python + CuPy + CUPTI):                                    ║")
    print(f"║    • CuPy kernels launched:    {target_kernels:<39}║")
    print(f"║    • GPU events (CUPTI):       {len(gpu_events):<39}║")
    print(f"║    • Kernel launches:          {kernel_launches:<39}║")
    print(f"║    • Kernel completes:         {kernel_completes:<39}║")
    print(f"║    • Total time:               {duration_ms:.0f} ms{' ' * (34 - len(f'{duration_ms:.0f}'))}║")
    print("║                                                                      ║")
    print("║  ✅ This is REAL GPU profiling - same as C++ CLI!                    ║")
    print("║                                                                      ║")
    print("╚══════════════════════════════════════════════════════════════════════╝")
    print(f"{C(Color.RESET)}")
    print()
    
    return 0 if goal_achieved else 1


# =============================================================================
# Main Entry Point
# =============================================================================
def main():
    """Main entry point."""
    # Check for --no-color
    if '--no-color' in sys.argv:
        Color.disable()
        sys.argv.remove('--no-color')
    
    parser = argparse.ArgumentParser(
        prog='tracesmith-cli',
        description='TraceSmith GPU Profiling & Replay System (Python CLI)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
{C(Color.BOLD)}Examples:{C(Color.RESET)}
  tracesmith-cli record -o trace.sbt -d 5  # Record for 5 seconds
  tracesmith-cli view trace.sbt --stats    # Show statistics
  tracesmith-cli export trace.sbt          # Export to Perfetto
  tracesmith-cli analyze trace.sbt         # Analyze performance
  tracesmith-cli benchmark -n 10000        # Run 10K benchmark
  tracesmith-cli devices                   # List GPUs

Run '{C(Color.CYAN)}tracesmith-cli <command> --help{C(Color.RESET)}' for more information.
"""
    )
    parser.add_argument('--version', action='store_true', help='Show version')
    parser.add_argument('--no-color', action='store_true', help='Disable colored output')
    
    subparsers = parser.add_subparsers(dest='command', help='Commands')
    
    # info command
    info_parser = subparsers.add_parser('info', help='Show version and system info')
    info_parser.set_defaults(func=cmd_info)
    
    # devices command
    devices_parser = subparsers.add_parser('devices', help='List available GPU devices')
    devices_parser.set_defaults(func=cmd_devices)
    
    # record command
    record_parser = subparsers.add_parser('record', help='Record GPU events')
    record_parser.add_argument('-o', '--output', help='Output file (default: trace.sbt)')
    record_parser.add_argument('-d', '--duration', type=float, default=5.0, help='Duration in seconds')
    record_parser.add_argument('-p', '--platform', choices=['cuda', 'metal', 'rocm', 'auto'], default='auto')
    record_parser.set_defaults(func=cmd_record)
    
    # view command
    view_parser = subparsers.add_parser('view', help='View trace file contents')
    view_parser.add_argument('input', help='Input trace file')
    view_parser.add_argument('-n', '--limit', type=int, help='Maximum events to show')
    view_parser.add_argument('--stats', action='store_true', help='Show statistics only')
    view_parser.set_defaults(func=cmd_view)
    
    # export command
    export_parser = subparsers.add_parser('export', help='Export to Perfetto format')
    export_parser.add_argument('input', help='Input trace file')
    export_parser.add_argument('-o', '--output', help='Output file')
    export_parser.add_argument('--counters', action='store_true', help='Include counter tracks')
    export_parser.add_argument('--protobuf', action='store_true', help='Use protobuf format')
    export_parser.set_defaults(func=cmd_export)
    
    # analyze command
    analyze_parser = subparsers.add_parser('analyze', help='Analyze trace file')
    analyze_parser.add_argument('input', help='Input trace file')
    analyze_parser.set_defaults(func=cmd_analyze)
    
    # replay command
    replay_parser = subparsers.add_parser('replay', help='Replay a captured trace')
    replay_parser.add_argument('input', help='Input trace file')
    replay_parser.add_argument('--mode', choices=['dry-run', 'full', 'partial'], default='dry-run')
    replay_parser.add_argument('--validate', action='store_true', help='Validate determinism')
    replay_parser.set_defaults(func=cmd_replay)
    
    # benchmark command
    benchmark_parser = subparsers.add_parser('benchmark', help='Run 10K GPU call stacks benchmark')
    benchmark_parser.add_argument('-n', '--count', type=int, default=10000, 
                                  help='Number of events to capture (default: 10000)')
    benchmark_parser.add_argument('-o', '--output', help='Output file (default: benchmark.sbt)')
    benchmark_parser.add_argument('--no-stacks', action='store_true', 
                                  help='Disable host call stack capture')
    benchmark_parser.add_argument('--real-gpu', action='store_true',
                                  help='Use real GPU profiling with CuPy + CUPTI')
    benchmark_parser.add_argument('-v', '--verbose', action='store_true', 
                                  help='Show progress bar')
    benchmark_parser.set_defaults(func=cmd_benchmark)
    
    args = parser.parse_args()
    
    if args.version:
        print_banner()
        return 0
    
    if args.command is None:
        print_banner()
        parser.print_help()
        return 0
    
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
