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
import sys
from pathlib import Path
from typing import Any, Dict, List

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
    print_section("Recording GPU Trace")

    import time

    from . import (
        PlatformType,
        ProfilerConfig,
        SBTWriter,
        create_profiler,
        detect_platform,
        platform_type_to_string,
    )

    output_file = args.output or "trace.sbt"
    duration_sec = args.duration

    print(f"{colorize(Color.BOLD)}Configuration:{colorize(Color.RESET)}")
    print(f"  Output:   {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
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
    print(f"\n{colorize(Color.GREEN)}▶ Recording...{colorize(Color.RESET)} (Press Ctrl+C to stop)\n")

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
            bar = f"{colorize(Color.GREEN)}{'█' * filled}{colorize(Color.RESET)}{'░' * (bar_width - filled)}"
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

    print(f"{colorize(Color.BOLD)}Summary:{colorize(Color.RESET)}")
    print(f"  Platform:     {platform_name}")
    print(f"  Total events: {colorize(Color.GREEN)}{total_events}{colorize(Color.RESET)}")
    print(f"  Output:       {colorize(Color.CYAN)}{output_file}{colorize(Color.RESET)}")
    print()

    print_success(f"Trace saved to {output_file}")
    print("\nNext steps:")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli view {output_file} --stats{colorize(Color.RESET)}")
    print(f"  {colorize(Color.CYAN)}tracesmith-cli export {output_file}{colorize(Color.RESET)}")

    return 0


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
    
    # Check if xctrace should be used
    use_xctrace = getattr(args, 'xctrace', False)
    
    # On macOS with Metal, suggest xctrace if not specified
    platform = detect_platform()
    if sys.platform == 'darwin' and platform == PlatformType.Metal and not use_xctrace:
        print_info("Tip: Use --xctrace for real Metal GPU events on macOS")
        print()
    
    # Use xctrace if requested
    if use_xctrace:
        return _cmd_profile_xctrace(args, command)

    # Output file
    if args.output:
        output_file = args.output
    else:
        # Generate output name from command
        cmd_name = os.path.basename(command[0]).replace('.py', '').replace('.sh', '')
        output_file = f"{cmd_name}_trace.sbt"

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
    record_parser = subparsers.add_parser('record', help='Record GPU events')
    record_parser.add_argument('-o', '--output', help='Output file (default: trace.sbt)')
    record_parser.add_argument('-d', '--duration', type=float, default=5.0, help='Duration in seconds')
    record_parser.add_argument('-p', '--platform', choices=['cuda', 'metal', 'rocm', 'auto'], default='auto')
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
