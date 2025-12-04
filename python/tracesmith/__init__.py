"""
TraceSmith - GPU Profiling & Replay System

A cross-platform GPU profiling and replay system for AI compilers,
deep learning frameworks, and GPU driver engineers.

Version: 0.6.0

Features:
- Cross-platform GPU profiling (CUDA, ROCm, Metal, Simulation)
- SBT binary trace format (compact, efficient)
- Perfetto JSON and Protobuf export (85% smaller files)
- Real-time tracing with lock-free ring buffers
- RenderDoc-inspired frame capture
- GPU memory profiling and leak detection
- LLVM XRay trace import
- eBPF GPU event tracing (Linux)
- Python and C++ APIs
"""

from ._tracesmith import (
    # Version info
    __version__,
    VERSION_MAJOR,
    VERSION_MINOR,
    VERSION_PATCH,
    
    # ========================================================================
    # Core Enums
    # ========================================================================
    EventType,
    PlatformType,
    ReplayMode,
    FlowType,           # v0.2.0: Kineto-compatible
    PerfettoFormat,     # v0.2.0: Protobuf export
    ResourceType,       # v0.5.0: Frame capture
    CaptureState,       # v0.5.0: Frame capture
    MemoryCategory,     # v0.2.0: Memory categories
    
    # Real-time Tracing enums (v0.3.0)
    TracingState,
    TracingMode,
    
    # XRay enums and types (v0.4.0)
    XRayEntryType,
    XRayFileHeader,
    XRayFunctionRecord,
    XRayStatistics,
    
    # BPF enums (v0.4.0)
    BPFEventType,
    
    # ========================================================================
    # Core Classes
    # ========================================================================
    TraceEvent,
    DeviceInfo,
    TraceMetadata,
    ProfilerConfig,
    SimulationProfiler,
    FlowInfo,           # v0.2.0: Kineto-compatible
    MemoryEvent,        # v0.2.0: Memory profiling
    CounterEvent,       # v0.2.0: Metrics/counters
    
    # ========================================================================
    # File I/O - SBT Binary Format
    # ========================================================================
    SBTWriter,
    SBTReader,
    
    # ========================================================================
    # Timeline Building
    # ========================================================================
    TimelineSpan,
    Timeline,
    TimelineBuilder,
    
    # ========================================================================
    # Export - Perfetto
    # ========================================================================
    PerfettoExporter,           # JSON format
    PerfettoProtoExporter,      # Protobuf format (v0.2.0)
    
    # ========================================================================
    # Real-time Tracing (v0.3.0)
    # ========================================================================
    TracingSession,
    TracingStatistics,
    
    # ========================================================================
    # Frame Capture (v0.5.0 - RenderDoc-inspired)
    # ========================================================================
    FrameCapture,
    FrameCaptureConfig,
    CapturedFrame,
    DrawCallInfo,
    ResourceState,
    ResourceTracker,
    
    # ========================================================================
    # Memory Profiler (v0.6.0)
    # ========================================================================
    MemoryProfiler,
    MemoryProfilerConfig,
    MemoryAllocation,
    MemorySnapshot,
    MemoryLeak,
    MemoryReport,
    
    # ========================================================================
    # XRay Importer (v0.4.0)
    # ========================================================================
    XRayImporter,
    XRayImporterConfig,
    
    # ========================================================================
    # BPF Tracer (v0.4.0 - Linux only)
    # ========================================================================
    BPFTracer,
    BPFEventRecord,
    
    # ========================================================================
    # Replay Engine
    # ========================================================================
    ReplayConfig,
    ReplayResult,
    ReplayEngine,
    
    # ========================================================================
    # Utility Functions
    # ========================================================================
    get_current_timestamp,
    event_type_to_string,
    resource_type_to_string,    # v0.5.0
    format_bytes,               # v0.6.0
    format_duration,            # v0.6.0
    bpf_event_type_to_string,   # v0.4.0
    bpf_event_to_trace_event,   # v0.4.0
)

__all__ = [
    # Version
    '__version__',
    'VERSION_MAJOR',
    'VERSION_MINOR', 
    'VERSION_PATCH',
    
    # Core Enums
    'EventType',
    'PlatformType',
    'ReplayMode',
    'FlowType',
    'PerfettoFormat',
    'ResourceType',
    'CaptureState',
    'MemoryCategory',
    'TracingState',
    'TracingMode',
    'XRayEntryType',
    'XRayFileHeader',
    'XRayFunctionRecord',
    'XRayStatistics',
    'BPFEventType',
    
    # Core Classes
    'TraceEvent',
    'DeviceInfo',
    'TraceMetadata',
    'ProfilerConfig',
    'SimulationProfiler',
    'FlowInfo',
    'MemoryEvent',
    'CounterEvent',
    
    # File I/O
    'SBTWriter',
    'SBTReader',
    
    # Timeline
    'TimelineSpan',
    'Timeline',
    'TimelineBuilder',
    
    # Export
    'PerfettoExporter',
    'PerfettoProtoExporter',
    
    # Real-time Tracing
    'TracingSession',
    'TracingStatistics',
    
    # Frame Capture
    'FrameCapture',
    'FrameCaptureConfig',
    'CapturedFrame',
    'DrawCallInfo',
    'ResourceState',
    'ResourceTracker',
    
    # Memory Profiler
    'MemoryProfiler',
    'MemoryProfilerConfig',
    'MemoryAllocation',
    'MemorySnapshot',
    'MemoryLeak',
    'MemoryReport',
    
    # XRay Importer
    'XRayImporter',
    'XRayImporterConfig',
    
    # BPF Tracer
    'BPFTracer',
    'BPFEventRecord',
    
    # Replay
    'ReplayConfig',
    'ReplayResult',
    'ReplayEngine',
    
    # Functions
    'get_current_timestamp',
    'event_type_to_string',
    'resource_type_to_string',
    'format_bytes',
    'format_duration',
    'bpf_event_type_to_string',
    'bpf_event_to_trace_event',
    
    # High-level convenience functions
    'capture_trace',
    'build_timeline',
    'export_perfetto',
    'is_protobuf_available',
    'replay_trace',
    'profile_memory',
    'is_bpf_available',
]


# ============================================================================
# High-level Convenience Functions
# ============================================================================

def capture_trace(duration_ms: int = 1000, stream_count: int = 1, event_count: int = 10) -> list:
    """
    Capture a trace using simulation profiler.
    
    Args:
        duration_ms: Simulated capture duration (used for event timestamps)
        stream_count: Number of streams to simulate
        event_count: Number of events to generate
    
    Returns:
        List of TraceEvent objects
    """
    profiler = SimulationProfiler()
    config = ProfilerConfig()
    config.capture_callstacks = False
    profiler.initialize(config)
    profiler.start_capture()
    
    # Generate simulated events (no actual waiting)
    for i in range(event_count):
        stream_id = i % max(1, stream_count)
        profiler.generate_kernel_event(f"kernel_{i}", stream_id)
        if i % 3 == 0:
            profiler.generate_memcpy_event(EventType.MemcpyH2D, 1024 * (i + 1), stream_id)
    
    profiler.stop_capture()
    return profiler.get_events()


def build_timeline(events: list) -> Timeline:
    """
    Build a timeline from trace events.
    
    Args:
        events: List of TraceEvent objects
    
    Returns:
        Timeline object with spans and statistics
    """
    builder = TimelineBuilder()
    builder.add_events(events)
    return builder.build()


def export_perfetto(events: list, filename: str, use_protobuf: bool = False) -> bool:
    """
    Export events to Perfetto format (JSON or Protobuf).
    
    Args:
        events: List of TraceEvent objects
        filename: Output file path (.json for JSON, .perfetto-trace for protobuf)
        use_protobuf: If True, use protobuf format (85% smaller files)
    
    Returns:
        True if successful
    """
    if use_protobuf or filename.endswith('.perfetto-trace') or filename.endswith('.pftrace'):
        exporter = PerfettoProtoExporter(PerfettoFormat.PROTOBUF)
    else:
        exporter = PerfettoExporter()
    return exporter.export_to_file(events, filename)


def is_protobuf_available() -> bool:
    """
    Check if Perfetto SDK is available for protobuf export.
    
    Returns:
        True if SDK is available (6.8x smaller trace files)
    """
    return PerfettoProtoExporter.is_sdk_available()


def replay_trace(events: list, mode: ReplayMode = ReplayMode.Full) -> ReplayResult:
    """
    Replay a trace with the given mode.
    
    Args:
        events: List of TraceEvent objects
        mode: Replay mode (Full, Partial, DryRun, StreamSpecific)
    
    Returns:
        ReplayResult with execution details
    """
    engine = ReplayEngine()
    engine.load_events(events)
    
    config = ReplayConfig()
    config.mode = mode
    config.validate_order = True
    config.validate_dependencies = True
    
    return engine.replay(config)


def profile_memory(callback=None, config: MemoryProfilerConfig = None) -> MemoryProfiler:
    """
    Create and start a memory profiler.
    
    Args:
        callback: Optional callback function for allocation events
        config: Optional MemoryProfilerConfig
    
    Returns:
        MemoryProfiler instance (already started)
    
    Usage:
        profiler = profile_memory()
        # ... your GPU code ...
        profiler.stop()
        report = profiler.generate_report()
        print(report.summary())
    """
    if config is None:
        config = MemoryProfilerConfig()
    
    profiler = MemoryProfiler(config)
    profiler.start()
    return profiler


def is_bpf_available() -> bool:
    """
    Check if BPF tracing is available (Linux only).
    
    Returns:
        True if BPF is available on this system
    """
    return BPFTracer.is_available()
