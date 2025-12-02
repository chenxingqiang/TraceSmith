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
    
    # Core classes
    TraceEvent,
    DeviceInfo,
    TraceMetadata,
    ProfilerConfig,
    SimulationProfiler,
    
    # File I/O
    SBTWriter,
    SBTReader,
    
    # Timeline
    TimelineSpan,
    Timeline,
    TimelineBuilder,
    
    # Export
    PerfettoExporter,
    
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
    
    # Core
    'TraceEvent',
    'DeviceInfo',
    'TraceMetadata',
    'ProfilerConfig',
    'SimulationProfiler',
    
    # I/O
    'SBTWriter',
    'SBTReader',
    
    # Timeline
    'TimelineSpan',
    'Timeline',
    'TimelineBuilder',
    
    # Export
    'PerfettoExporter',
    
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


def export_perfetto(events: list, filename: str) -> bool:
    """
    Export events to Perfetto JSON format.
    
    Args:
        events: List of TraceEvent objects
        filename: Output file path
    
    Returns:
        True if successful
    """
    exporter = PerfettoExporter()
    return exporter.export_to_file(events, filename)


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
