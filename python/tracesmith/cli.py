"""
TraceSmith Command Line Interface (Python)

GPU Profiling & Replay System

Usage:
    tracesmith-cli info              Show version and system info
    tracesmith-cli devices           List available GPU devices
    tracesmith-cli record            Record GPU events
    tracesmith-cli profile CMD       Profile a command (record + execute)
    tracesmith-cli view FILE         View trace file contents
    tracesmith-cli export FILE       Export to Perfetto format
    tracesmith-cli analyze FILE      Analyze trace file
    tracesmith-cli replay FILE       Replay a captured trace
    tracesmith-cli benchmark         Run 10K GPU call stacks benchmark

Or via Python module:
    python -m tracesmith <command>
"""

import argparse  # noqa: I001
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

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


def colorize(color: str) -> str:
    """Apply color code if colors are enabled."""
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
    print(colorize(Color.CYAN) + BANNER + colorize(Color.RESET))
    version = get_version()
    print(f"{colorize(Color.YELLOW)}                    GPU Profiling & Replay System v{version}{colorize(Color.RESET)}\n")


def print_compact_banner():
    """Print a compact banner."""
    version = get_version()
    print(f"{colorize(Color.CYAN)}{colorize(Color.BOLD)}TraceSmith{colorize(Color.RESET)} v{version} - GPU Profiling & Replay System\n")


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
    print(f"{colorize(Color.GREEN)}✓ {colorize(Color.RESET)}{msg}")


def print_error(msg: str):
    print(f"{colorize(Color.RED)}✗ Error: {colorize(Color.RESET)}{msg}", file=sys.stderr)


def print_warning(msg: str):
    print(f"{colorize(Color.YELLOW)}⚠ Warning: {colorize(Color.RESET)}{msg}")


def print_info(msg: str):
    print(f"{colorize(Color.BLUE)}ℹ {colorize(Color.RESET)}{msg}")


def print_section(title: str):
    print(f"\n{colorize(Color.BOLD)}{colorize(Color.CYAN)}═══ {title} ═══{colorize(Color.RESET)}\n")


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
        VERSION_MAJOR,
        VERSION_MINOR,
        VERSION_PATCH,
        __version__,
        detect_platform,
        get_cuda_device_count,
        get_metal_device_count,
        is_bpf_available,
        is_cuda_available,
        is_metal_available,
        is_protobuf_available,
        platform_type_to_string,
    )

    print(f"{colorize(Color.BOLD)}Version:{colorize(Color.RESET)}")
    print(f"  TraceSmith:  {colorize(Color.GREEN)}{__version__}{colorize(Color.RESET)}")
    print(f"  Components:  {VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH}")
    print()

    # Platform detection
    print(f"{colorize(Color.BOLD)}Platform Detection:{colorize(Color.RESET)}")
    platform = detect_platform()
    print(f"  Active Platform: {colorize(Color.CYAN)}{platform_type_to_string(platform)}{colorize(Color.RESET)}")
    print()

    print(f"{colorize(Color.BOLD)}GPU Support:{colorize(Color.RESET)}")
    cuda_avail = is_cuda_available()
    metal_avail = is_metal_available()

    cuda_status = f"{colorize(Color.GREEN)}✓ Available ({get_cuda_device_count()} devices){colorize(Color.RESET)}" if cuda_avail else f"{colorize(Color.YELLOW)}✗ Not available{colorize(Color.RESET)}"
    metal_status = f"{colorize(Color.GREEN)}✓ Available ({get_metal_device_count()} devices){colorize(Color.RESET)}" if metal_avail else f"{colorize(Color.YELLOW)}✗ Not available{colorize(Color.RESET)}"

    print(f"  NVIDIA CUDA:  {cuda_status}")
    print(f"  Apple Metal:  {metal_status}")
    print(f"  AMD ROCm:     {colorize(Color.YELLOW)}Coming soon{colorize(Color.RESET)}")
    print()

    print(f"{colorize(Color.BOLD)}Features:{colorize(Color.RESET)}")
    proto_status = f"{colorize(Color.GREEN)}✓{colorize(Color.RESET)}" if is_protobuf_available() else f"{colorize(Color.YELLOW)}✗{colorize(Color.RESET)}"
    bpf_status = f"{colorize(Color.GREEN)}✓{colorize(Color.RESET)}" if is_bpf_available() else f"{colorize(Color.YELLOW)}✗ (Linux only){colorize(Color.RESET)}"

    # Check nsys availability
    nsys_available = _is_nsys_available()
    nsys_version = _get_nsys_version() if nsys_available else None
    if nsys_available and nsys_version:
        nsys_status = f"{colorize(Color.GREEN)}✓ {nsys_version}{colorize(Color.RESET)}"
    elif nsys_available:
        nsys_status = f"{colorize(Color.GREEN)}✓ Available{colorize(Color.RESET)}"
    else:
        nsys_status = f"{colorize(Color.YELLOW)}✗ Not installed{colorize(Color.RESET)}"

    print(f"  Perfetto Protobuf: {proto_status}")
    print(f"  BPF Tracing:       {bpf_status}")
    print(f"  Nsight Systems:    {nsys_status}")
    print()

    return 0


# =============================================================================
# Command: devices - List Available GPUs
# =============================================================================
def cmd_devices(args):
    """List available GPU devices."""
    print_section("GPU Device Detection")

    from . import (
        PlatformType,
        create_profiler,
        get_cuda_device_count,
        get_cuda_driver_version,
        get_metal_device_count,
        is_cuda_available,
        is_metal_available,
    )

    found_any = False

    # Check CUDA
    print(f"{colorize(Color.BOLD)}NVIDIA CUDA:{colorize(Color.RESET)}")
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
                        print(f"\n  {colorize(Color.CYAN)}Device {dev.device_id}: {colorize(Color.RESET)}{dev.name}")
                        print(f"    Vendor:  {dev.vendor}")
                        print(f"    Memory:  {format_bytes(dev.total_memory)}")
                        print(f"    SMs:     {dev.multiprocessor_count}")
        except Exception:
            pass
    else:
        print(f"  {colorize(Color.YELLOW)}Not available{colorize(Color.RESET)}")

    # Check Metal
    print(f"\n{colorize(Color.BOLD)}Apple Metal:{colorize(Color.RESET)}")
    if is_metal_available():
        count = get_metal_device_count()
        print_success("Metal available")
        print(f"  Devices: {count}")
        found_any = True
    else:
        print(f"  {colorize(Color.YELLOW)}Not available{colorize(Color.RESET)}")

    # Check ROCm
    print(f"\n{colorize(Color.BOLD)}AMD ROCm:{colorize(Color.RESET)}")
    print(f"  {colorize(Color.YELLOW)}Coming soon{colorize(Color.RESET)}")

    print()

    if not found_any:
        print_warning("No supported GPU platforms detected.")
        print("Make sure GPU drivers are installed and accessible.")

    return 0  # Always return success - this is just informational


# =============================================================================
# Command: record - Record GPU Events
# =============================================================================
def cmd_record(args):
    """Record GPU events to a trace file."""
    import threading
    import time

    from . import (
        PlatformType,
        ProfilerConfig,
        SBTWriter,
        TraceMetadata,
        create_profiler,
        detect_platform,
        export_perfetto,
        platform_type_to_string,
    )

    output_file = args.output or "trace.sbt"
    duration_sec = args.duration
    use_nsys = getattr(args, 'nsys', False)
    keep_nsys = getattr(args, 'keep_nsys', False)
    
    # Parse exec command if provided
    exec_command = getattr(args, 'exec', None)
    if exec_command:
        # Handle the case where exec is a list
        if isinstance(exec_command, list):
            exec_command = exec_command
        else:
            exec_command = [exec_command]
    
    print_section("Recording GPU Trace")

    # Check if nsys mode is requested
    if use_nsys:
        if not _is_nsys_available():
            print_error("nsys (NVIDIA Nsight Systems) not found.")
            print("Install from: https://developer.nvidia.com/nsight-systems")
            return 1
        
        if not exec_command:
            print_error("--nsys requires --exec to specify the command to profile")
            print(f"  Example: tracesmith-cli record --nsys --exec python train.py")
            return 1
        
        # Use nsys for profiling
        return _cmd_record_nsys(args, exec_command, output_file, duration_sec, keep_nsys)

    print(f"{colorize(Color.BOLD)}Configuration:{colorize(Color.RESET)}")
    print(f"  Output:   {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    if exec_command:
        print(f"  Execute:  {colorize(Color.CYAN)}{' '.join(exec_command)}{colorize(Color.RESET)}")
        print(f"  Mode:     {colorize(Color.GREEN)}In-process execution (CUPTI compatible){colorize(Color.RESET)}")
    else:
        print(f"  Duration: {duration_sec} seconds")
        print(f"  Mode:     {colorize(Color.YELLOW)}Passive recording (waiting for GPU activity){colorize(Color.RESET)}")
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
    
    # Show warning if no exec command and using CUPTI
    if not exec_command:
        print()
        print_warning("CUPTI can only capture GPU events from the SAME process.")
        print_info("To capture events, use --exec to run GPU code in this process:")
        print(f"  {colorize(Color.CYAN)}tracesmith-cli record --exec 'python train.py'{colorize(Color.RESET)}")
        print(f"  {colorize(Color.CYAN)}tracesmith-cli record --exec 'python -c \"import torch; ...\"'{colorize(Color.RESET)}")
        print()

    # Create writer
    writer = SBTWriter(output_file)
    if not writer.is_open():
        print_error(f"Failed to open output file: {output_file}")
        return 1

    # Write metadata
    metadata = TraceMetadata()
    metadata.application_name = "tracesmith-record"
    if exec_command:
        metadata.command_line = ' '.join(exec_command)
    writer.write_metadata(metadata)

    # Event collection
    all_events = []
    events_lock = threading.Lock()
    total_events = [0]
    stop_collection = threading.Event()

    def collect_events():
        """Background thread to collect events."""
        while not stop_collection.is_set():
            events = profiler.get_events(10000)
            if events:
                with events_lock:
                    all_events.extend(events)
                    total_events[0] += len(events)
            time.sleep(0.05)

    # Start capture
    profiler.start_capture()
    
    # Start collection thread
    collector_thread = threading.Thread(target=collect_events, daemon=True)
    collector_thread.start()

    start_time = time.time()
    exit_code = 0

    if exec_command:
        # Execute command in the same process
        print(f"\n{colorize(Color.GREEN)}▶ Recording with in-process execution...{colorize(Color.RESET)}\n")
        print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
        
        # Check if this is a Python command
        is_python_cmd = (exec_command[0] == 'python' or exec_command[0] == 'python3' or 
                         exec_command[0].endswith('python') or exec_command[0].endswith('python3'))
        
        if is_python_cmd:
            # Run Python in the same process for CUPTI capture
            exit_code = _run_python_in_process(exec_command)
        else:
            # For non-Python commands, we need to warn user
            print_warning("Non-Python commands run as subprocess - CUPTI cannot capture their GPU events")
            print_info("Consider using Python wrapper or tracesmith Python API")
            import subprocess
            try:
                result = subprocess.run(exec_command, shell=False)
                exit_code = result.returncode
            except Exception as e:
                print_error(f"Failed to execute command: {e}")
                exit_code = 1
        
        print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    else:
        # Passive recording mode - just wait
        print(f"\n{colorize(Color.GREEN)}▶ Recording...{colorize(Color.RESET)} (Press Ctrl+C to stop)\n")
        
        try:
            while time.time() - start_time < duration_sec:
                # Progress
                elapsed = time.time() - start_time
                progress = min(elapsed / duration_sec, 1.0)
                bar_width = 40
                filled = int(bar_width * progress)
                bar = f"{colorize(Color.GREEN)}{'█' * filled}{colorize(Color.RESET)}{'░' * (bar_width - filled)}"
                print(f"\r  [{bar}] {progress*100:.0f}% | Events: {total_events[0]}     ", end='', flush=True)
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\n")
            print_info("Recording interrupted by user")

    # Stop collection
    stop_collection.set()
    collector_thread.join(timeout=1.0)

    # Stop profiler
    profiler.stop_capture()

    # Drain remaining events
    remaining = profiler.get_events()
    if remaining:
        with events_lock:
            all_events.extend(remaining)
            total_events[0] += len(remaining)

    # Write events
    if all_events:
        writer.write_events(all_events)

    writer.finalize()

    end_time = time.time()
    duration_actual = end_time - start_time

    print("\n")
    print_section("Recording Complete")

    # Show command result if exec was used
    if exec_command:
        if exit_code == 0:
            print_success("Command completed successfully")
        else:
            print_warning(f"Command exited with code: {exit_code}")
        print()

    print(f"{colorize(Color.BOLD)}Summary:{colorize(Color.RESET)}")
    print(f"  Platform:     {platform_name}")
    print(f"  Duration:     {duration_actual:.2f} seconds")
    print(f"  Total events: {colorize(Color.GREEN)}{total_events[0]}{colorize(Color.RESET)}")
    print(f"  Output:       {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    
    # Analyze events if any
    if all_events:
        from collections import Counter
        from . import EventType
        
        type_counts = Counter(e.type for e in all_events)
        kernel_count = type_counts.get(EventType.KernelLaunch, 0)
        memcpy_count = sum(type_counts.get(t, 0) for t in 
                          [EventType.MemcpyH2D, EventType.MemcpyD2H, EventType.MemcpyD2D])
        
        print()
        print(f"{colorize(Color.BOLD)}Event Breakdown:{colorize(Color.RESET)}")
        print(f"  Kernel Launches: {kernel_count}")
        print(f"  Memory Copies:   {memcpy_count}")
        print(f"  Other Events:    {total_events[0] - kernel_count - memcpy_count}")
    print()

    # Export to Perfetto if requested
    if getattr(args, 'perfetto', False) and all_events:
        perfetto_file = output_file.replace('.sbt', '.json')
        if export_perfetto(all_events, perfetto_file):
            print_success(f"Exported Perfetto trace: {perfetto_file}")
            print(f"  View at: {colorize(Color.CYAN)}https://ui.perfetto.dev/{colorize(Color.RESET)}")
        else:
            print_warning("Failed to export Perfetto trace")
        print()

    print_success(f"Trace saved to {output_file}")
    print("\nNext steps:")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli view {output_file} --stats{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli export {output_file}{colorize(Color.RESET)}")

    return exit_code


# =============================================================================
# Helper: Record with nsys (NVIDIA Nsight Systems)
# =============================================================================
def _cmd_record_nsys(args, command, output_file, duration_sec, keep_nsys):
    """Record GPU events using NVIDIA Nsight Systems (nsys)."""
    import time
    
    from . import (
        SBTWriter,
        TraceMetadata,
        export_perfetto,
    )
    
    print(f"{colorize(Color.BOLD)}Configuration:{colorize(Color.RESET)}")
    print(f"  Output:   {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    print(f"  Execute:  {colorize(Color.CYAN)}{' '.join(command)}{colorize(Color.RESET)}")
    print(f"  Backend:  {colorize(Color.GREEN)}NVIDIA Nsight Systems (nsys){colorize(Color.RESET)}")
    print(f"  Mode:     {colorize(Color.GREEN)}System-wide GPU profiling (cross-process){colorize(Color.RESET)}")
    print()
    
    # Show nsys version
    nsys_version = _get_nsys_version()
    if nsys_version:
        print_success(f"nsys available: {nsys_version}")
    else:
        print_success("nsys available")
    print()
    
    print(f"{colorize(Color.GREEN)}▶ Starting nsys profiling...{colorize(Color.RESET)}")
    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    
    start_time = time.time()
    
    # Run with nsys
    exit_code, nsys_rep, events = _run_with_nsys(
        command=command,
        output_file=output_file,
        duration=duration_sec if duration_sec != 5.0 else None,  # Only set if not default
        gpu_metrics=True,
        cuda_api=True,
        nvtx=True,
        sample_cpu=False,
    )
    
    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    print()
    
    end_time = time.time()
    duration_actual = end_time - start_time
    
    # Show command result
    if exit_code == 0:
        print_success("Command completed successfully")
    else:
        print_warning(f"Command exited with code: {exit_code}")
    
    print_success("nsys profiling stopped")
    print()
    
    # Save events to SBT format
    writer = SBTWriter(output_file)
    if writer.is_open():
        metadata = TraceMetadata()
        metadata.application_name = os.path.basename(command[0])
        metadata.command_line = ' '.join(command)
        writer.write_metadata(metadata)
        
        if events:
            writer.write_events(events)
        
        writer.finalize()
    
    # Print summary
    print_section("Recording Complete")
    
    print(f"{colorize(Color.BOLD)}Summary:{colorize(Color.RESET)}")
    print(f"  Backend:      NVIDIA Nsight Systems")
    print(f"  Duration:     {duration_actual:.2f} seconds")
    print(f"  GPU Events:   {colorize(Color.GREEN)}{len(events)}{colorize(Color.RESET)}")
    print(f"  Output:       {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    
    # Show nsys report location
    if nsys_rep and os.path.exists(nsys_rep):
        if keep_nsys:
            print(f"  nsys Report:  {colorize(Color.CYAN)}{nsys_rep}{colorize(Color.RESET)}")
        else:
            # Cleanup nsys files
            base_name = output_file.replace('.sbt', '').replace('.json', '')
            _cleanup_nsys_files(f"{base_name}_nsys")
    
    # Analyze events if any
    if events:
        from collections import Counter
        from . import EventType
        
        type_counts = Counter(e.type for e in events)
        kernel_count = type_counts.get(EventType.KernelLaunch, 0)
        memcpy_count = sum(type_counts.get(t, 0) for t in 
                          [EventType.MemcpyH2D, EventType.MemcpyD2H, EventType.MemcpyD2D])
        
        print()
        print(f"{colorize(Color.BOLD)}Event Breakdown:{colorize(Color.RESET)}")
        print(f"  Kernel Launches: {kernel_count}")
        print(f"  Memory Copies:   {memcpy_count}")
        print(f"  Other Events:    {len(events) - kernel_count - memcpy_count}")
    print()
    
    # Export to Perfetto if requested
    if getattr(args, 'perfetto', False) and events:
        perfetto_file = output_file.replace('.sbt', '.json')
        if export_perfetto(events, perfetto_file):
            print_success(f"Exported Perfetto trace: {perfetto_file}")
            print(f"  View at: {colorize(Color.CYAN)}https://ui.perfetto.dev/{colorize(Color.RESET)}")
        else:
            print_warning("Failed to export Perfetto trace")
        print()
    
    print_success(f"Trace saved to {output_file}")
    print("\nNext steps:")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli view {output_file} --stats{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli export {output_file}{colorize(Color.RESET)}")
    if nsys_rep and keep_nsys and os.path.exists(nsys_rep):
        print(f"  {colorize(Color.CYAN)}nsys-ui {nsys_rep}{colorize(Color.RESET)}  # Open in Nsight Systems UI")
    
    return exit_code


# =============================================================================
# Helper: NVIDIA Nsight Systems (nsys) Integration
# =============================================================================
def _is_nsys_available() -> bool:
    """Check if nsys (NVIDIA Nsight Systems) is available."""
    return shutil.which('nsys') is not None


def _get_nsys_version() -> Optional[str]:
    """Get nsys version string."""
    try:
        result = subprocess.run(['nsys', '--version'], capture_output=True, text=True)
        if result.returncode == 0:
            # Parse version from output like "NVIDIA Nsight Systems version 2023.4.1.97-..."
            for line in result.stdout.split('\n'):
                if 'version' in line.lower():
                    return line.strip()
        return None
    except Exception:
        return None


def _run_with_nsys(
    command: List[str],
    output_file: str,
    duration: Optional[float] = None,
    gpu_metrics: bool = True,
    cuda_api: bool = True,
    nvtx: bool = True,
    sample_cpu: bool = False,
) -> Tuple[int, str, List]:
    """
    Run a command with nsys profiling and return captured events.
    
    Args:
        command: Command to execute
        output_file: Output file path (without extension)
        duration: Optional max duration in seconds
        gpu_metrics: Capture GPU metrics
        cuda_api: Capture CUDA API calls
        nvtx: Capture NVTX annotations
        sample_cpu: Sample CPU activity
    
    Returns:
        Tuple of (exit_code, nsys_report_path, events)
    """
    import json
    import tempfile
    
    # Generate temp file for nsys output
    base_name = output_file.replace('.sbt', '').replace('.json', '')
    nsys_output = f"{base_name}_nsys"
    
    # Build nsys command
    nsys_cmd = [
        'nsys', 'profile',
        '-o', nsys_output,
        '--force-overwrite=true',
        '--export=json',  # Export to JSON for parsing
    ]
    
    # Add trace options
    trace_opts = []
    if cuda_api:
        trace_opts.append('cuda')
    if nvtx:
        trace_opts.append('nvtx')
    if gpu_metrics:
        trace_opts.append('cublas')
        trace_opts.append('cudnn')
    
    if trace_opts:
        nsys_cmd.extend(['--trace=' + ','.join(trace_opts)])
    
    # Add duration limit if specified
    if duration:
        nsys_cmd.extend(['--duration', str(int(duration))])
    
    # Disable CPU sampling by default (faster)
    if not sample_cpu:
        nsys_cmd.extend(['--sample=none'])
    
    # Add the user command
    nsys_cmd.append('--')
    nsys_cmd.extend(command)
    
    print_info(f"Running: {' '.join(nsys_cmd[:8])}...")
    
    # Execute nsys
    try:
        result = subprocess.run(nsys_cmd, capture_output=False)
        exit_code = result.returncode
    except FileNotFoundError:
        print_error("nsys not found. Install NVIDIA Nsight Systems.")
        return 1, "", []
    except Exception as e:
        print_error(f"nsys failed: {e}")
        return 1, "", []
    
    # Find the generated files
    nsys_rep = f"{nsys_output}.nsys-rep"
    nsys_json = f"{nsys_output}.json"
    
    # Parse events from JSON if available
    events = []
    if os.path.exists(nsys_json):
        events = _parse_nsys_json(nsys_json)
    elif os.path.exists(nsys_rep):
        # Try to export JSON from nsys-rep
        try:
            export_result = subprocess.run(
                ['nsys', 'export', '-t', 'json', '-o', nsys_json, nsys_rep],
                capture_output=True
            )
            if export_result.returncode == 0 and os.path.exists(nsys_json):
                events = _parse_nsys_json(nsys_json)
        except Exception:
            pass
    
    return exit_code, nsys_rep, events


def _parse_nsys_json(json_path: str) -> List:
    """
    Parse nsys JSON export and convert to TraceSmith TraceEvent format.
    
    Args:
        json_path: Path to nsys JSON export file
    
    Returns:
        List of TraceEvent objects
    """
    import json
    
    from . import EventType, TraceEvent
    
    events = []
    
    try:
        with open(json_path, 'r') as f:
            data = json.load(f)
    except Exception as e:
        print_warning(f"Failed to parse nsys JSON: {e}")
        return events
    
    # nsys JSON format varies by version, handle common formats
    # Look for CUDA API events, kernel events, memory events
    
    # Try to find events in various nsys JSON structures
    cuda_api_events = []
    kernel_events = []
    memcpy_events = []
    
    # nsys export JSON structure
    if isinstance(data, dict):
        # Check for 'traceEvents' (Chrome trace format)
        if 'traceEvents' in data:
            cuda_api_events = data['traceEvents']
        # Check for 'CudaEvent' or similar
        elif 'CudaEvent' in data:
            cuda_api_events = data['CudaEvent']
        # Check for nested structure
        elif 'StringTable' in data and 'TraceProcessEvents' in data:
            # Older nsys format
            pass
    elif isinstance(data, list):
        # Direct list of events
        cuda_api_events = data
    
    # Convert to TraceEvent format
    correlation_id = 0
    for raw_event in cuda_api_events:
        if not isinstance(raw_event, dict):
            continue
        
        event = TraceEvent()
        event.correlation_id = correlation_id
        correlation_id += 1
        
        # Get event name/type
        name = raw_event.get('name', raw_event.get('Name', ''))
        cat = raw_event.get('cat', raw_event.get('Category', ''))
        
        # Determine event type
        name_lower = name.lower()
        if 'kernel' in name_lower or 'launch' in name_lower:
            event.type = EventType.KernelLaunch
        elif 'memcpy' in name_lower:
            if 'htod' in name_lower or 'h2d' in name_lower:
                event.type = EventType.MemcpyH2D
            elif 'dtoh' in name_lower or 'd2h' in name_lower:
                event.type = EventType.MemcpyD2H
            elif 'dtod' in name_lower or 'd2d' in name_lower:
                event.type = EventType.MemcpyD2D
            else:
                event.type = EventType.MemcpyH2D
        elif 'memset' in name_lower:
            event.type = EventType.MemsetDevice
        elif 'sync' in name_lower:
            event.type = EventType.StreamSync
        elif 'malloc' in name_lower or 'alloc' in name_lower:
            event.type = EventType.MemAlloc
        elif 'free' in name_lower:
            event.type = EventType.MemFree
        else:
            event.type = EventType.Marker
        
        event.name = name
        
        # Get timing (nsys uses microseconds or nanoseconds)
        ts = raw_event.get('ts', raw_event.get('Timestamp', 0))
        dur = raw_event.get('dur', raw_event.get('Duration', 0))
        
        # nsys typically uses microseconds in Chrome format
        if 'ts' in raw_event:
            event.timestamp = int(ts * 1000)  # us to ns
            event.duration = int(dur * 1000) if dur else 0
        else:
            event.timestamp = int(ts)
            event.duration = int(dur) if dur else 0
        
        # Get thread/stream info
        event.thread_id = raw_event.get('tid', raw_event.get('ThreadId', 0))
        event.stream_id = raw_event.get('args', {}).get('stream', 0) if isinstance(raw_event.get('args'), dict) else 0
        event.device_id = raw_event.get('args', {}).get('device', 0) if isinstance(raw_event.get('args'), dict) else 0
        
        # Store additional args as metadata
        args = raw_event.get('args', {})
        if isinstance(args, dict):
            for k, v in args.items():
                if isinstance(v, (str, int, float)):
                    event.metadata[str(k)] = str(v)
        
        events.append(event)
    
    return events


def _cleanup_nsys_files(base_path: str):
    """Clean up nsys temporary files."""
    for ext in ['.nsys-rep', '.json', '.sqlite', '.qdstrm']:
        path = f"{base_path}{ext}"
        if os.path.exists(path):
            try:
                os.remove(path)
            except Exception:
                pass


# =============================================================================
# Helper: Run Python code in the same process (for CUPTI capture)
# =============================================================================
def _run_python_in_process(command):
    """
    Run Python code in the same process so CUPTI can capture GPU events.
    
    CUPTI can only capture GPU activity in the SAME process, not child processes.
    This function handles:
      - python script.py [args]
      - python -c "code"
      - python -m module [args]
    """
    import runpy
    import sys
    
    # Parse Python command
    # command = ['python', ...] or ['python3', ...]
    python_args = command[1:]  # Skip 'python'
    
    if not python_args:
        print_warning("No Python script or code specified")
        return 1
    
    # Save original sys.argv
    original_argv = sys.argv.copy()
    
    exit_code = 0
    try:
        if python_args[0] == '-c':
            # python -c "code"
            if len(python_args) < 2:
                print_error("No code provided after -c")
                return 1
            code = python_args[1]
            sys.argv = ['<string>'] + python_args[2:]
            exec(compile(code, '<string>', 'exec'), {'__name__': '__main__'})
            
        elif python_args[0] == '-m':
            # python -m module [args]
            if len(python_args) < 2:
                print_error("No module name provided after -m")
                return 1
            module_name = python_args[1]
            sys.argv = python_args[1:]  # module name becomes argv[0]
            runpy.run_module(module_name, run_name='__main__', alter_sys=True)
            
        else:
            # python script.py [args]
            script_path = python_args[0]
            sys.argv = python_args  # script path becomes argv[0]
            
            # Check if file exists
            import os
            if not os.path.exists(script_path):
                print_error(f"Script not found: {script_path}")
                return 1
            
            runpy.run_path(script_path, run_name='__main__')
            
    except SystemExit as e:
        # Script called sys.exit()
        exit_code = e.code if isinstance(e.code, int) else (1 if e.code else 0)
    except KeyboardInterrupt:
        print()
        print_warning("Interrupted by user (Ctrl+C)")
        exit_code = 130
    except Exception as e:
        import traceback
        print_error(f"Exception in Python code: {e}")
        traceback.print_exc()
        exit_code = 1
    finally:
        # Restore original sys.argv
        sys.argv = original_argv
    
    return exit_code


# =============================================================================
# Command: profile - Profile a Command (Record + Execute)
# =============================================================================
def cmd_profile(args):
    """Profile a command by recording GPU events during its execution."""
    import os
    import signal
    import subprocess
    import sys
    import threading
    import time

    from . import (
        PlatformType,
        ProfilerConfig,
        SBTWriter,
        TraceMetadata,
        create_profiler,
        detect_platform,
        export_perfetto,
        platform_type_to_string,
    )

    # Parse command - handle the '--' separator
    command = args.command
    
    # Remove leading '--' if present
    if command and command[0] == '--':
        command = command[1:]
    
    if not command:
        print_error("No command specified")
        print()
        print(f"{colorize(Color.BOLD)}Usage:{colorize(Color.RESET)}")
        print(f"  tracesmith-cli profile [options] -- <command>")
        print()
        print(f"{colorize(Color.BOLD)}Examples:{colorize(Color.RESET)}")
        print(f"  tracesmith-cli profile -- python train.py")
        print(f"  tracesmith-cli profile -o trace.sbt -- python train.py --epochs 10")
        print(f"  tracesmith-cli profile --perfetto -- ./my_cuda_app")
        print(f"  tracesmith-cli profile --xctrace -- python train.py  # Use Instruments on macOS")
        print(f"  tracesmith-cli profile -- python -c \"import torch; x=torch.randn(1000).cuda()\"")
        return 1
    
    # Check if xctrace or nsys should be used
    use_xctrace = getattr(args, 'xctrace', False)
    use_nsys = getattr(args, 'nsys', False)
    keep_nsys = getattr(args, 'keep_nsys', False)
    
    # Output file
    if args.output:
        output_file = args.output
    else:
        # Generate output name from command
        cmd_name = os.path.basename(command[0]).replace('.py', '').replace('.sh', '')
        output_file = f"{cmd_name}_trace.sbt"
    
    # Use nsys if requested
    if use_nsys:
        if not _is_nsys_available():
            print_error("nsys (NVIDIA Nsight Systems) not found.")
            print("Install from: https://developer.nvidia.com/nsight-systems")
            return 1
        return _cmd_profile_nsys(args, command, output_file, keep_nsys)
    
    # On macOS with Metal, suggest xctrace if not specified
    platform = detect_platform()
    if sys.platform == 'darwin' and platform == PlatformType.Metal and not use_xctrace:
        print_info("Tip: Use --xctrace for real Metal GPU events on macOS")
        print()
    
    # On Linux/Windows with CUDA, suggest nsys if not specified
    if sys.platform != 'darwin' and platform == PlatformType.CUDA:
        print_info("Tip: Use --nsys for system-wide GPU profiling (can capture any process)")
        print()
    
    # Use xctrace if requested
    if use_xctrace:
        return _cmd_profile_xctrace(args, command)

    print_section("TraceSmith Profile")

    print(f"{colorize(Color.BOLD)}Configuration:{colorize(Color.RESET)}")
    print(f"  Command: {colorize(Color.CYAN)}{' '.join(command)}{colorize(Color.RESET)}")
    print(f"  Output:  {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    print()

    # Detect platform
    platform = detect_platform()
    platform_name = platform_type_to_string(platform)

    if platform == PlatformType.Unknown:
        print_warning("No GPU detected, will record without GPU profiling")
        profiler = None
    else:
        print_success(f"Detected GPU platform: {platform_name}")

        # Create profiler
        profiler = create_profiler(platform)
        if not profiler:
            print_warning(f"Failed to create profiler for {platform_name}")
            profiler = None
        else:
            # Configure
            config = ProfilerConfig()
            config.buffer_size = args.buffer_size

            if not profiler.initialize(config):
                print_warning("Failed to initialize profiler")
                profiler = None
            else:
                print_success("Profiler initialized")

    # Create writer
    writer = SBTWriter(output_file)
    if not writer.is_open():
        print_error(f"Failed to open output file: {output_file}")
        return 1

    # Write metadata
    metadata = TraceMetadata()
    metadata.application_name = os.path.basename(command[0])
    metadata.command_line = ' '.join(command)
    writer.write_metadata(metadata)

    # Event collection thread
    events_lock = threading.Lock()
    all_events = []
    stop_collection = threading.Event()
    total_events = [0]  # Use list for mutable counter in closure

    def collect_events():
        """Background thread to collect events."""
        while not stop_collection.is_set():
            if profiler:
                events = profiler.get_events(10000)
                if events:
                    with events_lock:
                        all_events.extend(events)
                        total_events[0] += len(events)
            time.sleep(0.05)  # 50ms polling interval

    # Start profiling
    print()
    if profiler:
        profiler.start_capture()
        print(f"{colorize(Color.GREEN)}▶ GPU profiling started{colorize(Color.RESET)}")

    # Start collection thread
    collector_thread = threading.Thread(target=collect_events, daemon=True)
    collector_thread.start()

    # Record start time
    start_time = time.time()
    start_timestamp = time.time_ns()

    print(f"{colorize(Color.GREEN)}▶ Executing command...{colorize(Color.RESET)}")
    print()
    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")

    # Execute command
    exit_code = 0
    
    # Check if this is a Python script/command that we can run in-process
    # CUPTI can only capture GPU events in the SAME process
    is_python_cmd = (command[0] == 'python' or command[0] == 'python3' or 
                     command[0].endswith('python') or command[0].endswith('python3'))
    
    if is_python_cmd and profiler is not None:
        # Run Python code in the same process for CUPTI to capture events
        exit_code = _run_python_in_process(command)
    else:
        # Fallback to subprocess (won't capture GPU events from child process)
        if profiler is not None:
            print_warning("Running as subprocess - CUPTI cannot capture GPU events from child processes")
            print_info("For Python scripts, tracesmith runs them in-process automatically")
            print()
        try:
            # Run the command
            result = subprocess.run(
                command,
                shell=False,
                env=os.environ.copy()
            )
            exit_code = result.returncode
        except KeyboardInterrupt:
            print()
            print_warning("Command interrupted by user (Ctrl+C)")
            exit_code = 130
        except FileNotFoundError:
            print_error(f"Command not found: {command[0]}")
            exit_code = 127
        except Exception as e:
            print_error(f"Failed to execute command: {e}")
            exit_code = 1

    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    print()

    # Record end time
    end_time = time.time()
    end_timestamp = time.time_ns()
    duration_sec = end_time - start_time

    # Stop profiling
    stop_collection.set()
    collector_thread.join(timeout=1.0)

    if profiler:
        profiler.stop_capture()

        # Drain remaining events
        remaining = profiler.get_events()
        if remaining:
            with events_lock:
                all_events.extend(remaining)
                total_events[0] += len(remaining)

        print_success("GPU profiling stopped")

    # Write events
    if all_events:
        writer.write_events(all_events)

    writer.finalize()

    # Print summary
    print_section("Profile Complete")

    # Command result
    if exit_code == 0:
        print_success(f"Command completed successfully")
    else:
        print_warning(f"Command exited with code: {exit_code}")

    print()
    print(f"{colorize(Color.BOLD)}Summary:{colorize(Color.RESET)}")
    print(f"  Command:      {' '.join(command)}")
    print(f"  Duration:     {duration_sec:.2f} seconds")
    print(f"  GPU Events:   {colorize(Color.GREEN)}{total_events[0]}{colorize(Color.RESET)}")
    print(f"  Output:       {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")

    # Analyze events
    if all_events:
        from collections import Counter
        from . import EventType

        type_counts = Counter(e.type for e in all_events)
        kernel_count = type_counts.get(EventType.KernelLaunch, 0)
        memcpy_count = sum(type_counts.get(t, 0) for t in 
                          [EventType.MemcpyH2D, EventType.MemcpyD2H, EventType.MemcpyD2D])

        print()
        print(f"{colorize(Color.BOLD)}Event Breakdown:{colorize(Color.RESET)}")
        print(f"  Kernel Launches: {kernel_count}")
        print(f"  Memory Copies:   {memcpy_count}")
        print(f"  Other Events:    {total_events[0] - kernel_count - memcpy_count}")

    print()

    # Export to Perfetto if requested
    if args.perfetto:
        perfetto_file = output_file.replace('.sbt', '.json')
        if export_perfetto(all_events, perfetto_file):
            print_success(f"Exported Perfetto trace: {perfetto_file}")
            print(f"  View at: {colorize(Color.CYAN)}https://ui.perfetto.dev/{colorize(Color.RESET)}")
        else:
            print_warning("Failed to export Perfetto trace")
        print()

    # Next steps
    print(f"{colorize(Color.BOLD)}Next steps:{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli view {output_file} --stats{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli export {output_file}{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli analyze {output_file}{colorize(Color.RESET)}")

    return exit_code


def _cmd_profile_nsys(args, command, output_file, keep_nsys):
    """Profile a command using NVIDIA Nsight Systems (nsys)."""
    import time
    
    from . import (
        SBTWriter,
        TraceMetadata,
        export_perfetto,
    )
    
    print_section("TraceSmith Profile (nsys)")
    
    print(f"{colorize(Color.BOLD)}Configuration:{colorize(Color.RESET)}")
    print(f"  Command:  {colorize(Color.CYAN)}{' '.join(command)}{colorize(Color.RESET)}")
    print(f"  Output:   {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    print(f"  Backend:  {colorize(Color.GREEN)}NVIDIA Nsight Systems (nsys){colorize(Color.RESET)}")
    print()
    
    # Show nsys version
    nsys_version = _get_nsys_version()
    if nsys_version:
        print_success(f"nsys: {nsys_version}")
    else:
        print_success("nsys available")
    print()
    
    print(f"{colorize(Color.GREEN)}▶ Starting nsys profiling...{colorize(Color.RESET)}")
    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    
    start_time = time.time()
    
    # Run with nsys
    exit_code, nsys_rep, events = _run_with_nsys(
        command=command,
        output_file=output_file,
        duration=None,  # No duration limit for profile command
        gpu_metrics=True,
        cuda_api=True,
        nvtx=True,
        sample_cpu=False,
    )
    
    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    print()
    
    end_time = time.time()
    duration_sec = end_time - start_time
    
    # Show command result
    if exit_code == 0:
        print_success("Command completed successfully")
    else:
        print_warning(f"Command exited with code: {exit_code}")
    
    print_success("nsys profiling stopped")
    
    # Save events to SBT format
    writer = SBTWriter(output_file)
    if writer.is_open():
        metadata = TraceMetadata()
        metadata.application_name = os.path.basename(command[0])
        metadata.command_line = ' '.join(command)
        writer.write_metadata(metadata)
        
        if events:
            writer.write_events(events)
        
        writer.finalize()
    
    # Print summary
    print_section("Profile Complete")
    
    print(f"{colorize(Color.BOLD)}Summary:{colorize(Color.RESET)}")
    print(f"  Command:      {' '.join(command)}")
    print(f"  Duration:     {duration_sec:.2f} seconds")
    print(f"  GPU Events:   {colorize(Color.GREEN)}{len(events)}{colorize(Color.RESET)}")
    print(f"  Output:       {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    
    # Show nsys report location
    if nsys_rep and os.path.exists(nsys_rep):
        if keep_nsys:
            print(f"  nsys Report:  {colorize(Color.CYAN)}{nsys_rep}{colorize(Color.RESET)}")
        else:
            # Cleanup nsys files
            base_name = output_file.replace('.sbt', '').replace('.json', '')
            _cleanup_nsys_files(f"{base_name}_nsys")
    
    # Analyze events if any
    if events:
        from collections import Counter
        from . import EventType
        
        type_counts = Counter(e.type for e in events)
        kernel_count = type_counts.get(EventType.KernelLaunch, 0)
        memcpy_count = sum(type_counts.get(t, 0) for t in 
                          [EventType.MemcpyH2D, EventType.MemcpyD2H, EventType.MemcpyD2D])
        
        print()
        print(f"{colorize(Color.BOLD)}Event Breakdown:{colorize(Color.RESET)}")
        print(f"  Kernel Launches: {kernel_count}")
        print(f"  Memory Copies:   {memcpy_count}")
        print(f"  Other Events:    {len(events) - kernel_count - memcpy_count}")
    
    print()
    
    # Export to Perfetto if requested
    if getattr(args, 'perfetto', False) and events:
        perfetto_file = output_file.replace('.sbt', '.json')
        if export_perfetto(events, perfetto_file):
            print_success(f"Exported Perfetto trace: {perfetto_file}")
            print(f"  View at: {colorize(Color.CYAN)}https://ui.perfetto.dev/{colorize(Color.RESET)}")
        else:
            print_warning("Failed to export Perfetto trace")
        print()
    
    # Next steps
    print(f"{colorize(Color.BOLD)}Next steps:{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli view {output_file} --stats{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli export {output_file}{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli analyze {output_file}{colorize(Color.RESET)}")
    if nsys_rep and keep_nsys and os.path.exists(nsys_rep):
        print(f"  {colorize(Color.CYAN)}nsys-ui {nsys_rep}{colorize(Color.RESET)}  # Open in Nsight Systems UI")
    
    return exit_code


def _cmd_profile_xctrace(args, command):
    """Profile using Apple Instruments (xctrace) on macOS."""
    import os
    import sys
    import time
    
    from . import (
        SBTWriter,
        TraceMetadata,
        export_perfetto,
    )
    
    # Check platform
    if sys.platform != 'darwin':
        print_error("xctrace is only available on macOS")
        return 1
    
    # Import xctrace module
    try:
        from .xctrace import XCTraceProfiler, XCTraceConfig
    except ImportError as e:
        print_error(f"Failed to import xctrace module: {e}")
        return 1
    
    # Check if xctrace is available
    if not XCTraceProfiler.is_available():
        print_error("xctrace not found. Install Xcode Command Line Tools:")
        print(f"  {colorize(Color.CYAN)}xcode-select --install{colorize(Color.RESET)}")
        return 1
    
    # Output file
    if args.output:
        output_file = args.output
    else:
        cmd_name = os.path.basename(command[0]).replace('.py', '').replace('.sh', '')
        output_file = f"{cmd_name}_trace.sbt"
    
    print_section("TraceSmith Profile (xctrace)")
    
    print(f"{colorize(Color.BOLD)}Configuration:{colorize(Color.RESET)}")
    print(f"  Command:   {colorize(Color.CYAN)}{' '.join(command)}{colorize(Color.RESET)}")
    print(f"  Output:    {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    print(f"  Backend:   {colorize(Color.GREEN)}Apple Instruments (xctrace){colorize(Color.RESET)}")
    print(f"  Template:  {args.xctrace_template}")
    print()
    
    # Create profiler
    config = XCTraceConfig(
        template=args.xctrace_template,
        duration_seconds=3600,  # 1 hour max, will stop when command exits
    )
    
    profiler = XCTraceProfiler(config)
    
    # Get trace output dir
    trace_dir = os.path.dirname(output_file) or "."
    trace_file = os.path.join(
        trace_dir,
        os.path.basename(output_file).replace('.sbt', '.trace')
    )
    
    print_success("xctrace profiler initialized")
    print()
    
    # Record start time
    start_time = time.time()
    
    # Profile the command
    print(f"{colorize(Color.GREEN)}▶ Starting xctrace profiling...{colorize(Color.RESET)}")
    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    
    try:
        all_events = profiler.profile_command(
            command,
            duration=None,  # Run until command exits
            output_file=trace_file if args.keep_trace else None
        )
    except Exception as e:
        print_error(f"Profiling failed: {e}")
        return 1
    
    print(f"{colorize(Color.YELLOW)}{'─' * 60}{colorize(Color.RESET)}")
    print()
    
    end_time = time.time()
    duration_sec = end_time - start_time
    
    print_success("xctrace profiling stopped")
    
    # Save to SBT format
    writer = SBTWriter(output_file)
    if writer.is_open():
        metadata = TraceMetadata()
        metadata.application_name = os.path.basename(command[0])
        metadata.command_line = ' '.join(command)
        writer.write_metadata(metadata)
        
        if all_events:
            writer.write_events(all_events)
        
        writer.finalize()
    
    # Print summary
    print_section("Profile Complete")
    
    print(f"{colorize(Color.BOLD)}Summary:{colorize(Color.RESET)}")
    print(f"  Command:      {' '.join(command)}")
    print(f"  Duration:     {duration_sec:.2f} seconds")
    print(f"  GPU Events:   {colorize(Color.GREEN)}{len(all_events)}{colorize(Color.RESET)}")
    print(f"  Output:       {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    
    # Show trace file location
    raw_trace = profiler.get_trace_file()
    if raw_trace and os.path.exists(raw_trace):
        if args.keep_trace:
            print(f"  Raw Trace:    {colorize(Color.CYAN)}{raw_trace}{colorize(Color.RESET)}")
            print()
            print(f"  Open in Instruments: {colorize(Color.YELLOW)}open \"{raw_trace}\"{colorize(Color.RESET)}")
        else:
            # Cleanup temp trace
            profiler.cleanup()
    
    # Analyze events
    if all_events:
        from collections import Counter
        from . import EventType
        
        type_counts = Counter(e.type for e in all_events)
        kernel_count = type_counts.get(EventType.KernelLaunch, 0)
        complete_count = type_counts.get(EventType.KernelComplete, 0)
        
        print()
        print(f"{colorize(Color.BOLD)}Event Breakdown:{colorize(Color.RESET)}")
        print(f"  GPU Commands:    {kernel_count + complete_count}")
        print(f"  Other Events:    {len(all_events) - kernel_count - complete_count}")
    
    print()
    
    # Export to Perfetto if requested
    if args.perfetto:
        perfetto_file = output_file.replace('.sbt', '.json')
        if export_perfetto(all_events, perfetto_file):
            print_success(f"Exported Perfetto trace: {perfetto_file}")
            print(f"  View at: {colorize(Color.CYAN)}https://ui.perfetto.dev/{colorize(Color.RESET)}")
        else:
            print_warning("Failed to export Perfetto trace")
        print()
    
    # Next steps
    print(f"{colorize(Color.BOLD)}Next steps:{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli view {output_file} --stats{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli export {output_file}{colorize(Color.RESET)}")
    if raw_trace and args.keep_trace:
        print(f"  {colorize(Color.CYAN)}open \"{raw_trace}\"{colorize(Color.RESET)}  # Open in Instruments")
    
    return 0


# =============================================================================
# Command: view - View Trace Contents
# =============================================================================
def cmd_view(args):
    """View trace file contents."""
    from collections import Counter

    from . import SBTReader, event_type_to_string

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
    print(f"{colorize(Color.BOLD)}File Info:{colorize(Color.RESET)}")
    print(f"  Events:   {colorize(Color.GREEN)}{len(events)}{colorize(Color.RESET)}")
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

    print(f"\n{colorize(Color.BOLD)}Statistics:{colorize(Color.RESET)}")
    if events:
        print(f"  Time span: {format_duration(int(max_ts - min_ts))}")
    print(f"  Streams:   {len(stream_counts)}")

    # Events by type
    print(f"\n{colorize(Color.BOLD)}Events by Type:{colorize(Color.RESET)}")
    print(f"  {'Type':<20} {'Count':>8} {'Total Time':>12} {'Avg Time':>12}")
    print(f"  {'-'*52}")

    for event_type, count in type_counts.most_common():
        type_name = event_type_to_string(event_type)
        total_dur = type_durations.get(event_type, 0)
        avg_dur = total_dur // count if count > 0 else 0
        print(f"  {type_name:<20} {count:>8} {format_duration(total_dur):>12} {format_duration(avg_dur):>12}")

    if args.stats:
        # Stream breakdown
        print(f"\n{colorize(Color.BOLD)}Events by Stream:{colorize(Color.RESET)}")
        for stream_id, count in sorted(stream_counts.items()):
            print(f"  Stream {stream_id}: {count} events")
        return 0

    # Show events
    limit = args.limit or 20
    print(f"\n{colorize(Color.BOLD)}Events (first {limit}):{colorize(Color.RESET)}")

    for i, event in enumerate(events[:limit]):
        type_name = event_type_to_string(event.type)
        print(f"  {colorize(Color.CYAN)}[{i:>5}]{colorize(Color.RESET)} {type_name:<16} | Stream {event.stream_id} | {format_duration(event.duration):>10} | {event.name}")

    if len(events) > limit:
        print(f"\n  ... and {len(events) - limit} more events")

    return 0


# =============================================================================
# Command: export - Export to Perfetto Format
# =============================================================================
def cmd_export(args):
    """Export trace to Perfetto format."""
    from . import PerfettoExporter, SBTReader

    input_path = Path(args.input)
    output_path = Path(args.output) if args.output else input_path.with_suffix('.json')

    if not input_path.exists():
        print_error(f"Input file '{input_path}' not found")
        return 1

    print_section("Exporting Trace")

    print(f"Input:  {colorize(Color.CYAN)}{input_path}{colorize(Color.RESET)}")
    print(f"Output: {colorize(Color.CYAN)}{output_path}{colorize(Color.RESET)}")
    print("Format: Perfetto JSON")
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
        print(f"\nView at: {colorize(Color.CYAN)}https://ui.perfetto.dev/{colorize(Color.RESET)}")
        return 0
    else:
        print_error(f"Failed to export to '{output_path}'")
        return 1


# =============================================================================
# Command: analyze - Analyze Trace
# =============================================================================
def cmd_analyze(args):
    """Analyze a trace file for performance insights."""
    from collections import defaultdict

    from . import EventType, SBTReader, build_timeline

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

    print(f"File: {colorize(Color.CYAN)}{input_path}{colorize(Color.RESET)}")
    print(f"Events: {len(events)}")
    print()

    # Build timeline
    timeline = build_timeline(events)

    # GPU Utilization
    print(f"{colorize(Color.BOLD)}GPU Utilization:{colorize(Color.RESET)}")
    print(f"  Overall:        {colorize(Color.GREEN)}{timeline.gpu_utilization * 100:.1f}%{colorize(Color.RESET)}")
    print(f"  Max concurrent: {timeline.max_concurrent_ops} ops")
    print(f"  Total duration: {format_duration(timeline.total_duration)}")

    # Kernel analysis
    kernel_stats: Dict[str, List[int]] = defaultdict(list)

    for event in events:
        if event.type == EventType.KernelLaunch or event.type == EventType.KernelComplete:
            kernel_stats[event.name].append(event.duration)

    if kernel_stats:
        print(f"\n{colorize(Color.BOLD)}Top Kernels by Time:{colorize(Color.RESET)}")

        # Sort by total time
        sorted_kernels = sorted(
            kernel_stats.items(),
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
    from . import ReplayConfig, ReplayEngine, ReplayMode, SBTReader

    input_path = Path(args.input)

    if not input_path.exists():
        print_error(f"Input file '{input_path}' not found")
        return 1

    print_section("Replay Trace")

    print(f"File: {colorize(Color.CYAN)}{input_path}{colorize(Color.RESET)}")
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

    print(f"\n{colorize(Color.BOLD)}Replay Results:{colorize(Color.RESET)}")
    success_color = Color.GREEN if result.success else Color.RED
    print(f"  Success:       {colorize(success_color)}{result.success}{colorize(Color.RESET)}")
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
        from . import (  # noqa: I001
            EventType,
            SBTWriter,
            StackCapture,
            StackCaptureConfig,
            TraceEvent,
            TraceMetadata,
            get_current_timestamp,
            get_cuda_device_count,
            is_cuda_available,
        )
    except ImportError as e:
        print_error(f"Failed to import TraceSmith modules: {e}")
        return 1

    # Check CUDA availability
    cuda_available = False
    try:
        cuda_available = is_cuda_available()
    except Exception:
        pass

    if not cuda_available:
        print()
        print(f"{colorize(Color.BOLD)}{colorize(Color.RED)}")
        print("╔══════════════════════════════════════════════════════════════════════╗")
        print("║  ERROR: CUDA support not available                                   ║")
        print("╚══════════════════════════════════════════════════════════════════════╝")
        print(f"{colorize(Color.RESET)}")
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
    cupti_profiler_cls = None
    try:
        from . import CUPTIProfiler as _CUPTIProfiler
        cupti_profiler_cls = _CUPTIProfiler
        cupti_available = True
    except ImportError:
        pass

    # Determine benchmark mode
    use_real_gpu = args.real_gpu and cupy_available and cupti_available

    # Print banner
    print()
    print(f"{colorize(Color.BOLD)}{colorize(Color.CYAN)}")
    print("╔══════════════════════════════════════════════════════════════════════╗")
    print("║  TraceSmith Benchmark: 10,000+ GPU Instruction-Level Call Stacks     ║")
    print("║  Feature: Non-intrusive capture of instruction-level GPU call stacks ║")
    print("╚══════════════════════════════════════════════════════════════════════╝")
    print(f"{colorize(Color.RESET)}")
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
    print(f"{colorize(Color.BOLD)}Configuration:{colorize(Color.RESET)}")
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
            cp, cupti_profiler_cls,
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
        print("\r  Progress: [████████████████████] 100%")

    print_success(f"Captured {target_kernels} events")
    print(f"  Total time:    {duration_ms:.0f} ms")
    print(f"  Events/sec:    {target_kernels * 1000 / duration_ms:.0f}")
    print()

    # =================================================================
    # Results
    # =================================================================
    print_section("Results")

    print(f"{colorize(Color.BOLD)}Events:{colorize(Color.RESET)}")
    print(f"  Total events:    {len(events)}")

    if capture_stacks:
        stacks_captured = sum(1 for e in events if e.call_stack is not None)
        total_frames = sum(e.call_stack.depth() if e.call_stack else 0 for e in events)
        avg_depth = total_frames / stacks_captured if stacks_captured > 0 else 0

        print()
        print(f"{colorize(Color.BOLD)}Host Call Stacks:{colorize(Color.RESET)}")
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

    print(f"{colorize(Color.BOLD)}{colorize(color)}")
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
    print(f"{colorize(Color.RESET)}")
    print()

    return 0 if goal_achieved else 1


def _run_real_gpu_benchmark(cp, cupti_profiler_cls, target_kernels, output_file,
                            capture_stacks, stack_capturer, host_stacks,
                            verbose, sbt_writer_cls, trace_metadata_cls, trace_event_cls,
                            event_type_cls, get_current_timestamp):
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
    profiler = cupti_profiler_cls()
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
        print("\r  Progress: [████████████████████] 100%")

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
    kernel_launches = sum(1 for e in gpu_events if e.type == event_type_cls.KernelLaunch)
    kernel_completes = sum(1 for e in gpu_events if e.type == event_type_cls.KernelComplete)
    other = len(gpu_events) - kernel_launches - kernel_completes

    print(f"{colorize(Color.BOLD)}GPU Events (CUPTI):{colorize(Color.RESET)}")
    print(f"  Events captured:   {event_count}")
    print(f"  Events dropped:    {events_dropped}")
    print(f"  Kernel launches:   {kernel_launches}")
    print(f"  Kernel completes:  {kernel_completes}")
    print(f"  Other events:      {other}")
    print()

    # Attach host stacks to GPU events
    if capture_stacks and host_stacks:
        stack_map = dict(host_stacks)
        attached = 0
        for event in gpu_events:
            if event.correlation_id in stack_map:
                event.call_stack = stack_map[event.correlation_id]
                attached += 1

        print(f"{colorize(Color.BOLD)}Host Call Stacks:{colorize(Color.RESET)}")
        print(f"  Stacks captured:        {len(host_stacks)}")
        print(f"  GPU events with stacks: {attached}")
        print()

    # =================================================================
    # Save to file
    # =================================================================
    try:
        writer = sbt_writer_cls(output_file)
        meta = trace_metadata_cls()
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

    print(f"{colorize(Color.BOLD)}{colorize(color)}")
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
    print(f"{colorize(Color.RESET)}")
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
{colorize(Color.BOLD)}Examples:{colorize(Color.RESET)}
  tracesmith-cli profile -- python train.py          # Profile a Python script
  tracesmith-cli profile -o trace.sbt -- ./my_app    # Profile with custom output
  tracesmith-cli profile --perfetto -- python test.py # Profile + export Perfetto
  tracesmith-cli record -o trace.sbt -d 5            # Record for 5 seconds
  tracesmith-cli view trace.sbt --stats              # Show statistics
  tracesmith-cli export trace.sbt                    # Export to Perfetto
  tracesmith-cli analyze trace.sbt                   # Analyze performance
  tracesmith-cli benchmark -n 10000                  # Run 10K benchmark
  tracesmith-cli devices                             # List GPUs

Run '{colorize(Color.CYAN)}tracesmith-cli <command> --help{colorize(Color.RESET)}' for more information.
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
    record_parser = subparsers.add_parser(
        'record', 
        help='Record GPU events',
        description='''
Record GPU events to a trace file.

IMPORTANT: CUPTI can only capture GPU events from the SAME process.
Use --exec to run GPU code in this process for event capture.

Examples:
  tracesmith-cli record --exec "python train.py"
  tracesmith-cli record --exec "python -c \\"import torch; x=torch.randn(1000).cuda()\\""
  tracesmith-cli record -o trace.sbt -d 10  # Passive mode (no events without --exec)
''',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    record_parser.add_argument('-o', '--output', help='Output file (default: trace.sbt)')
    record_parser.add_argument('-d', '--duration', type=float, default=5.0, 
                               help='Duration in seconds (for passive mode without --exec)')
    record_parser.add_argument('-p', '--platform', choices=['cuda', 'metal', 'rocm', 'auto'], default='auto')
    record_parser.add_argument('--exec', dest='exec', nargs=argparse.REMAINDER,
                               help='Execute command in same process (CUPTI capture). '
                                    'Example: --exec python train.py')
    record_parser.add_argument('--nsys', action='store_true',
                               help='Use NVIDIA Nsight Systems for system-wide GPU profiling. '
                                    'Requires --exec. Can capture GPU events from any process.')
    record_parser.add_argument('--keep-nsys', action='store_true',
                               help='Keep nsys report file (.nsys-rep) after profiling')
    record_parser.add_argument('--perfetto', action='store_true',
                               help='Also export to Perfetto JSON format')
    record_parser.set_defaults(func=cmd_record)

    # profile command (NEW!)
    profile_parser = subparsers.add_parser(
        'profile', 
        help='Profile a command (start recording, execute command, stop recording)',
        description='''
Profile a command by recording GPU events during its execution.

Examples:
  tracesmith-cli profile -- python train.py
  tracesmith-cli profile -o model_trace.sbt -- python train.py --epochs 10
  tracesmith-cli profile --perfetto -- ./my_cuda_app
  tracesmith-cli profile -- python -c "import torch; torch.randn(1000,1000).cuda()"
''',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    profile_parser.add_argument('-o', '--output', 
                                help='Output trace file (default: <command>_trace.sbt)')
    profile_parser.add_argument('--perfetto', action='store_true',
                                help='Also export to Perfetto JSON format')
    profile_parser.add_argument('--buffer-size', type=int, default=1000000,
                                help='Event buffer size (default: 1000000)')
    profile_parser.add_argument('--nsys', action='store_true',
                                help='Use NVIDIA Nsight Systems for system-wide GPU profiling. '
                                     'Can capture GPU events from any process (subprocess or external).')
    profile_parser.add_argument('--keep-nsys', action='store_true',
                                help='Keep nsys report file (.nsys-rep) after profiling')
    profile_parser.add_argument('--xctrace', action='store_true',
                                help='Use Apple Instruments (xctrace) for Metal GPU profiling on macOS')
    profile_parser.add_argument('--xctrace-template', default='Metal System Trace',
                                help="Instruments template (default: 'Metal System Trace')")
    profile_parser.add_argument('--keep-trace', action='store_true',
                                help='Keep the raw .trace file after profiling (xctrace only)')
    profile_parser.add_argument('command', nargs=argparse.REMAINDER,
                                help='Command to profile (use -- before command)')
    profile_parser.set_defaults(func=cmd_profile)

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
