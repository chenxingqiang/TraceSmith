# Changelog

All notable changes to TraceSmith will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.7.1] - 2024-12-04

### Added
- **Multi-GPU Cluster Profiling (Phase 2)**: Time Synchronization
  - `TimeSync`: Cross-GPU and cross-node time synchronization
    - Multiple sync methods: SystemClock, NTP, PTP, CUDA
    - GPU timestamp correlation
    - Offset and drift compensation
  - `ClockCorrelator`: Clock drift modeling and correction
    - Linear regression for drift compensation
    - Automatic timestamp correction
  - `NCCLTracker`: NCCL collective operation tracking
    - Intercept AllReduce, AllGather, Broadcast, etc.
    - Correlation with GPU events
    - Communication statistics
  - `CommAnalysis`: Communication pattern analysis
    - Communication matrix generation
    - Pattern detection (AllToAll, Ring, Tree)
    - Bottleneck identification
- Python bindings for TimeSync, NCCLTracker, CommAnalysis
- TraceEvent.call_stack property for storing call stacks

### Fixed
- Python CLI benchmark display alignment
- TraceEvent.call_stack binding for std::optional<CallStack>

## [0.7.0] - 2024-12-04

### Added
- **Multi-GPU Cluster Profiling (Phase 1)**: Single-node multi-GPU support
  - `MultiGPUProfiler`: Unified profiling across multiple GPUs
    - Automatic GPU discovery and initialization
    - Per-GPU event buffering with configurable sizes
    - Aggregation thread for real-time event collection
    - Event callbacks for live processing
  - `GPUTopology`: GPU interconnect topology discovery using NVML
    - NVLink detection (v1-v4)
    - NVSwitch detection
    - PCIe fallback for basic connectivity
    - Topology visualization (ASCII, Graphviz DOT, JSON)
    - Optimal path finding between GPUs
  - `MultiGPUConfig`: Configuration for multi-GPU profiling
    - Per-GPU buffer size
    - NVLink/peer access tracking toggles
    - Aggregation interval control
- New `cluster/` module: `include/tracesmith/cluster/`, `src/cluster/`
- New `multi_gpu_example.cpp`: Demonstrates multi-GPU profiling capabilities
- CuPy as optional dependency for Python CLI real GPU profiling
  - `pip install tracesmith[cuda12]` for CUDA 12.x
  - `pip install tracesmith[cuda11]` for CUDA 11.x

### Changed
- Updated `tracesmith.hpp` to include cluster module headers
- CLI benchmark command now compiles with CUDA support when available
- Improved CMake CUDA detection with `check_language(CUDA)`

## [0.6.9] - 2024-12-04

### Changed
- **Reorganized include directory structure** to match `src/` layout:
  - `include/tracesmith/capture/` - GPU profiling backends (profiler, cupti, metal, memory, bpf)
  - `include/tracesmith/common/` - Core utilities (types, ring_buffer, stack_capture, xray_importer)
  - `include/tracesmith/format/` - Trace file I/O (sbt_format)
  - `include/tracesmith/state/` - State analysis (gpu_state_machine, instruction_stream, timeline, perfetto)
  - `include/tracesmith/replay/` - Replay engine (replay_engine, frame_capture, determinism_checker)
- Updated all source files, examples, tests, and Python bindings to use new paths
- Main `tracesmith.hpp` header remains at top level and includes all modules

## [0.6.8] - 2024-12-04

### Added
- **Enhanced CLI**: Comprehensive command-line interface with ASCII banner
  - New commands: `record`, `view`, `info`, `export`, `analyze`, `replay`, `devices`
  - Colored terminal output with ANSI codes
  - Progress bar for recording
  - `--no-color` option for plain output
- **Python CLI**: Full-featured Python CLI matching C++ interface
  - `python -m tracesmith <command>` usage
  - All commands from C++ CLI available

### Changed
- Updated `getting_started.md` with real GPU profiling examples
- Removed all simulation code from profiler

## [0.6.7] - 2024-12-04

### Added
- **StackCapture bindings**: Full Python API for call stack capturing
  - `StackCapture`, `CallStack`, `StackFrame`, `StackCaptureConfig` classes
  - `is_available()`, `get_current_thread_id()` static methods
  - Performance: ~108µs/capture, 9000+ captures/second
- **OverflowPolicy enum**: `DropOldest`, `DropNewest`, `Block`
- **ProfilerConfig.overflow_policy**: Configure ring buffer overflow behavior
- **MemoryProfiler.detect_leaks()**: Detect potential memory leaks
- Complete SBT format bindings: `DeviceInfo`, `TraceMetadata` full fields
- Complete Replay module bindings: `ReplayConfig`, `ReplayResult` full fields

### Fixed
- Fixed missing `SBTReader.read_metadata()` binding
- Fixed `MemoryProfiler.detect_leaks()` visibility (moved to public)
- Fixed missing `OverflowPolicy` export in Python

## [0.6.4] - 2024-12-04

### Added
- **Platform-specific builds**: Support compiling for specific GPU platforms
  - `TRACESMITH_CUDA=1` for NVIDIA CUDA/CUPTI
  - `TRACESMITH_ROCM=1` for AMD ROCm
  - `TRACESMITH_METAL=1` for Apple Metal
- Auto-detection of GPU platform during installation
- Ninja build system support for faster compilation
- Added `is_cuda_available()`, `get_cuda_device_count()`, `detect_platform()` to Python API

### Changed
- Build system now auto-detects CUDA_HOME/CUDA_PATH environment variables
- Parallel build enabled by default

## [0.6.3] - 2024-12-04

### Changed
- **Remove simulation mode**: Focus on real GPU profiling
  - Removed `SimulationProfiler` from Python exports
  - Removed `capture_trace()` convenience function (use `create_profiler()` instead)
  - Added `create_profiler(PlatformType)` to Python exports

## [0.6.2] - 2024-12-04

### Fixed
- **PyPI packaging**: Fixed native extension not being included in wheel
  - Use custom `TRACESMITH_PYTHON_OUTPUT_DIR` variable instead of `CMAKE_LIBRARY_OUTPUT_DIRECTORY`
  - Ensures the `_tracesmith.so` is placed in the correct location for wheel packaging

## [0.6.1] - 2024-12-04

### Fixed
- **PyPI packaging**: Attempted fix for native extension (incomplete)

## [0.6.0] - 2024-12-04

### Added
- **GPU Memory Profiler**: Complete memory tracking with leak detection
  - `MemoryProfiler` class for recording allocations/deallocations
  - `MemorySnapshot` for point-in-time memory state
  - `MemoryReport` with detailed analysis and JSON export
  - Automatic leak detection with configurable thresholds
  - Python bindings: `profile_memory()` convenience function
- **Python API completeness**: All C++ features now exposed to Python
  - 12 new MemoryProfiler tests (100% pass rate)
  - Full coverage of XRay importer and BPF tracer
  - `format_bytes()` and `format_duration()` utilities
- **CLI tool**: `tracesmith` command-line interface
  - `tracesmith info` - Version and system information
  - `tracesmith convert` - Convert between trace formats
  - `tracesmith analyze` - Analyze trace files
  - `tracesmith export` - Export to Perfetto format

### Changed
- Updated version to 0.6.0
- Total test count: 86 tests (100% pass rate)

## [0.5.0] - 2024-12-03

### Added
- **RenderDoc-inspired Frame Capture**
  - `FrameCapture` class for capturing GPU frames
  - `ResourceTracker` for managing GPU resource lifecycle
  - Draw call and dispatch recording
  - Perfetto export for captured frames
- **Counter Track Visualization** support
- **eBPF Runtime Types** for Linux kernel-level tracing

### Changed
- Enhanced Python bindings with Frame Capture support
- Improved Perfetto export with counter tracks

## [0.4.0] - 2024-12-02

### Added
- **LLVM XRay Integration**
  - `XRayImporter` for parsing XRay trace files
  - Conversion to TraceSmith events
- **eBPF Types** for GPU event tracing
  - CUDA and HIP kernel tracing
  - Memory operation tracing
  - UVM fault and migration tracking

## [0.3.0] - 2024-12-01

### Added
- **Real-time Tracing**
  - `TracingSession` with lock-free ring buffers
  - Thread-safe event emission
  - Counter track support
- **Counter Events** for time-series metrics

## [0.2.0] - 2024-11-30

### Added
- **Perfetto SDK Integration**
  - Native protobuf export (85% smaller files)
  - `PerfettoProtoExporter` class
- **Kineto Schema Compatibility**
  - `thread_id` field for thread tracking
  - `metadata` map for custom data
  - `FlowInfo` for event relationships
  - `MemoryEvent` for memory operations
  - `CounterEvent` for metrics

## [0.1.0] - 2024-11-28

### Added
- Initial release
- **Core GPU Profiling**
  - Multi-platform support (CUDA, ROCm, Metal, Simulation)
  - `TraceEvent` structure with call stacks
  - `DeviceInfo` for GPU information
- **SBT Binary Format**
  - Compact trace file format
  - `SBTWriter` and `SBTReader` classes
  - 10x smaller than JSON
- **Timeline Building**
  - `TimelineBuilder` for trace analysis
  - GPU utilization calculation
  - Concurrent operation tracking
- **Perfetto JSON Export**
  - `PerfettoExporter` class
  - Chrome Trace Event format
- **Replay Engine**
  - `ReplayEngine` for trace replay
  - Multiple replay modes (Full, Partial, DryRun)
  - Determinism validation
- **Python Bindings**
  - pybind11-based Python API
  - High-level convenience functions

---

## Version History Summary

| Version | Date       | Highlights                                    |
|---------|------------|-----------------------------------------------|
| 0.6.0   | 2024-12-04 | GPU Memory Profiler, CLI, PyPI packaging      |
| 0.5.0   | 2024-12-03 | Frame Capture, Counter Tracks, eBPF runtime   |
| 0.4.0   | 2024-12-02 | XRay integration, eBPF types                  |
| 0.3.0   | 2024-12-01 | Real-time tracing, lock-free buffers          |
| 0.2.0   | 2024-11-30 | Perfetto SDK, Kineto compatibility            |
| 0.1.0   | 2024-11-28 | Initial release                               |

