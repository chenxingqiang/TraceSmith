"""
TraceSmith - GPU Profiling & Replay System

A cross-platform GPU profiling and replay system for AI compilers,
deep learning frameworks, and GPU driver engineers.
"""

from ._tracesmith import (
    # Version info
    __version__,
    VERSION_MAJOR,
    VERSION_MINOR,
    VERSION_PATCH,
    
    # Enums
    EventType,
    PlatformType,
    ReplayMode,
    FlowType,           # v0.2.0: Kineto-compatible
    PerfettoFormat,     # v0.2.0: Protobuf export
    
    # Core classes
    TraceEvent,
    DeviceInfo,
    TraceMetadata,
    ProfilerConfig,
    SimulationProfiler,
    FlowInfo,           # v0.2.0: Kineto-compatible
    MemoryEvent,        # v0.2.0: Memory profiling
    MemoryCategory,     # v0.2.0: Memory categories
    CounterEvent,       # v0.2.0: Metrics/counters
    
    # File I/O
    SBTWriter,
    SBTReader,
    
    # Timeline
    TimelineSpan,
    Timeline,
    TimelineBuilder,
    
    # Export
    PerfettoExporter,
    PerfettoProtoExporter,  # v0.2.0: Protobuf export
    
    # Real-time Tracing (v0.3.0)
    TracingSession,
    TracingState,
    TracingMode,
    TracingStatistics,
    TracingConfig,
    
    # Replay
    ReplayConfig,
    ReplayResult,
    ReplayEngine,
    
    # Functions
    get_current_timestamp,
    event_type_to_string,
)

__all__ = [
    # Version
    '__version__',
    'VERSION_MAJOR',
    'VERSION_MINOR', 
    'VERSION_PATCH',
    
    # Enums
    'EventType',
    'PlatformType',
    'ReplayMode',
    'FlowType',           # v0.2.0
    'PerfettoFormat',     # v0.2.0
    
    # Core
    'TraceEvent',
    'DeviceInfo',
    'TraceMetadata',
    'ProfilerConfig',
    'SimulationProfiler',
    'FlowInfo',           # v0.2.0
    'MemoryEvent',        # v0.2.0
    'MemoryCategory',     # v0.2.0
    'CounterEvent',       # v0.2.0
    
    # I/O
    'SBTWriter',
    'SBTReader',
    
    # Timeline
    'TimelineSpan',
    'Timeline',
    'TimelineBuilder',
    
    # Export
    'PerfettoExporter',
    'PerfettoProtoExporter',  # v0.2.0
    
    # Real-time Tracing (v0.3.0)
    'TracingSession',
    'TracingState',
    'TracingMode',
    'TracingStatistics',
    'TracingConfig',
    
    # Replay
    'ReplayConfig',
    'ReplayResult',
    'ReplayEngine',
    
    # Functions
    'get_current_timestamp',
    'event_type_to_string',
]


def capture_trace(duration_ms: int = 1000, stream_count: int = 1) -> list:
    """
    Convenience function to capture a trace using simulation profiler.
    
    Args:
        duration_ms: Capture duration in milliseconds
        stream_count: Number of streams to simulate
    
    Returns:
        List of TraceEvent objects
    """
    import time
    
    profiler = SimulationProfiler()
    config = ProfilerConfig()
    config.capture_callstacks = False
    profiler.initialize(config)
    profiler.start_capture()
    
    time.sleep(duration_ms / 1000.0)
    
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
