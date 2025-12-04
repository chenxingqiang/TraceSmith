"""
TraceSmith - GPU Profiling & Replay System

A cross-platform GPU profiling and replay system for AI compilers,
deep learning frameworks, and GPU driver engineers.

Version: 0.7.1

Features:
- Cross-platform GPU profiling (CUDA via CUPTI, ROCm, Metal)
- SBT binary trace format (compact, efficient)
- Perfetto JSON and Protobuf export (85% smaller files)
- Real-time tracing with lock-free ring buffers
- RenderDoc-inspired frame capture
- GPU memory profiling and leak detection
- LLVM XRay trace import
- eBPF GPU event tracing (Linux)
- Multi-GPU profiling with topology discovery (v0.7.0)
- NVLink/NVSwitch tracking
- Time synchronization for multi-GPU/cluster (v0.7.1)
- NCCL collective operation tracking (v0.7.1)
- Communication pattern analysis (v0.7.1)
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
    platform_type_to_string,
    ReplayMode,
    FlowType,
    OverflowPolicy,
    PerfettoFormat,
    ResourceType,
    CaptureState,
    MemoryCategory,
    TracingState,
    TracingMode,
    XRayEntryType,
    XRayFileHeader,
    XRayFunctionRecord,
    XRayStatistics,
    BPFEventType,
    GPUState,
    DependencyType,
    
    # ========================================================================
    # Core Classes
    # ========================================================================
    TraceEvent,
    DeviceInfo,
    TraceMetadata,
    ProfilerConfig,
    FlowInfo,
    MemoryEvent,
    CounterEvent,
    
    # ========================================================================
    # Stack Capture
    # ========================================================================
    StackFrame,
    CallStack,
    StackCaptureConfig,
    StackCapture,
    
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
    PerfettoExporter,
    PerfettoProtoExporter,
    
    # ========================================================================
    # Real-time Tracing
    # ========================================================================
    TracingSession,
    TracingStatistics,
    
    # ========================================================================
    # Frame Capture (RenderDoc-inspired)
    # ========================================================================
    FrameCapture,
    FrameCaptureConfig,
    CapturedFrame,
    DrawCallInfo,
    ResourceState,
    ResourceTracker,
    
    # ========================================================================
    # Memory Profiler
    # ========================================================================
    MemoryProfiler,
    MemoryProfilerConfig,
    MemoryAllocation,
    MemorySnapshot,
    MemoryLeak,
    MemoryReport,
    
    # ========================================================================
    # XRay Importer
    # ========================================================================
    XRayImporter,
    XRayImporterConfig,
    
    # ========================================================================
    # BPF Tracer (Linux only)
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
    # State Module (GPU State Machine, Instruction Stream, Timeline Viewer)
    # ========================================================================
    StateTransition,
    GPUStreamState,
    GPUStateMachine,
    GPUStateMachineStatistics,
    GPUStateHistory,
    OperationDependency,
    InstructionNode,
    InstructionStreamBuilder,
    InstructionStreamStatistics,
    TimelineViewConfig,
    TimelineViewer,
    
    # ========================================================================
    # Utility Functions
    # ========================================================================
    get_current_timestamp,
    event_type_to_string,
    resource_type_to_string,
    format_bytes,
    format_duration,
    bpf_event_type_to_string,
    bpf_event_to_trace_event,
    create_profiler,
    is_cuda_available,
    get_cuda_device_count,
    get_cuda_driver_version,
    is_metal_available,
    get_metal_device_count,
    detect_platform,
    
    # ========================================================================
    # Cluster Module - Multi-GPU Profiling (v0.7.0)
    # ========================================================================
    GPULinkType,
    GPULink,
    GPUDeviceTopology,
    GPUTopologyInfo,
    GPUTopology,
    is_nvml_available,
    get_nvml_version,
    link_type_to_string,
    get_link_bandwidth,
    NVLinkTransfer,
    PeerAccess,
    MultiGPUConfig,
    MultiGPUStats,
    MultiGPUProfiler,
    
    # ========================================================================
    # Cluster Module - Time Sync (v0.7.1)
    # ========================================================================
    TimeSyncMethod,
    TimeSyncConfig,
    SyncResult,
    TimeSync,
    DriftModel,
    ClockCorrelator,
    time_sync_method_to_string,
    string_to_time_sync_method,
    
    # ========================================================================
    # Cluster Module - NCCL Tracking (v0.7.1)
    # ========================================================================
    NCCLOpType,
    NCCLRedOp,
    NCCLDataType,
    NCCLOperation,
    NCCLTrackerConfig,
    NCCLStatistics,
    NCCLTracker,
    CommPattern,
    CommMatrix,
    CommBottleneck,
    LoadImbalance,
    CommAnalysis,
    nccl_op_type_to_string,
    nccl_red_op_to_string,
    nccl_data_type_to_string,
    nccl_data_type_size,
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
    'platform_type_to_string',
    'ReplayMode',
    'FlowType',
    'OverflowPolicy',
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
    'GPUState',
    'DependencyType',
    
    # Core Classes
    'TraceEvent',
    'DeviceInfo',
    'TraceMetadata',
    'ProfilerConfig',
    'FlowInfo',
    'MemoryEvent',
    'CounterEvent',
    
    # Stack Capture
    'StackFrame',
    'CallStack',
    'StackCaptureConfig',
    'StackCapture',
    
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
    
    # State Module
    'StateTransition',
    'GPUStreamState',
    'GPUStateMachine',
    'GPUStateMachineStatistics',
    'GPUStateHistory',
    'OperationDependency',
    'InstructionNode',
    'InstructionStreamBuilder',
    'InstructionStreamStatistics',
    'TimelineViewConfig',
    'TimelineViewer',
    
    # Functions
    'get_current_timestamp',
    'event_type_to_string',
    'resource_type_to_string',
    'format_bytes',
    'format_duration',
    'bpf_event_type_to_string',
    'bpf_event_to_trace_event',
    'create_profiler',
    'is_cuda_available',
    'get_cuda_device_count',
    'get_cuda_driver_version',
    'is_metal_available',
    'get_metal_device_count',
    'detect_platform',
    
    # High-level convenience functions
    'build_timeline',
    'export_perfetto',
    'is_protobuf_available',
    'replay_trace',
    'profile_memory',
    'is_bpf_available',
    
    # Cluster Module (v0.7.0)
    'GPULinkType',
    'GPULink',
    'GPUDeviceTopology',
    'GPUTopologyInfo',
    'GPUTopology',
    'is_nvml_available',
    'get_nvml_version',
    'link_type_to_string',
    'get_link_bandwidth',
    'NVLinkTransfer',
    'PeerAccess',
    'MultiGPUConfig',
    'MultiGPUStats',
    'MultiGPUProfiler',
    
    # Time Sync (v0.7.1)
    'TimeSyncMethod',
    'TimeSyncConfig',
    'SyncResult',
    'TimeSync',
    'DriftModel',
    'ClockCorrelator',
    'time_sync_method_to_string',
    'string_to_time_sync_method',
    
    # NCCL Tracking (v0.7.1)
    'NCCLOpType',
    'NCCLRedOp',
    'NCCLDataType',
    'NCCLOperation',
    'NCCLTrackerConfig',
    'NCCLStatistics',
    'NCCLTracker',
    'CommPattern',
    'CommMatrix',
    'CommBottleneck',
    'LoadImbalance',
    'CommAnalysis',
    'nccl_op_type_to_string',
    'nccl_red_op_to_string',
    'nccl_data_type_to_string',
    'nccl_data_type_size',
]


# ============================================================================
# High-level Convenience Functions
# ============================================================================

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
