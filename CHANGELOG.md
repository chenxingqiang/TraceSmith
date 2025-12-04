# Changelog

All notable changes to TraceSmith will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

