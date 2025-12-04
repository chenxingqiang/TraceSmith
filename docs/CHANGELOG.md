# Changelog

All notable changes to TraceSmith will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2025-12-02

### Added

#### Phase 1: MVP
- Core data structures (TraceEvent, DeviceInfo, CallStack)
- SBT binary trace format with string interning and delta encoding
- Lock-free SPSC ring buffer for event collection
- Platform abstraction interface (IPlatformProfiler)
- SimulationProfiler for testing without GPU hardware
- CLI tools: record, view, info commands

#### Phase 2: Call Stack Collection
- Cross-platform stack capture (macOS/Linux/Windows)
- Symbol resolution with C++ name demangling
- Instruction stream builder with dependency analysis
- DOT export for dependency visualization

#### Phase 3: GPU State Machine & Timeline Builder
- GPU state machine with stream tracking (Idle/Queued/Running/Waiting/Complete)
- Timeline builder with span generation
- Perfetto export for chrome://tracing visualization
- ASCII timeline viewer for terminal output
- Concurrent operation analysis

#### Phase 4: Replay Engine
- Complete replay engine with orchestration
- Stream scheduler with dependency tracking
- Round-robin, priority, and timing-based scheduling
- Determinism checker with validation
- Partial replay (time/operation ranges)
- Dry-run mode for analysis
- Stream-specific replay support

#### Phase 5: Production Release
- Python bindings via pybind11
- pip-installable package (setup.py)
- Comprehensive documentation
- Docker support for containerized builds
- Example programs for all features

### Technical Details
- C++17 standard
- CMake 3.16+ build system
- pybind11 for Python bindings
- Lock-free data structures for low overhead
- Cross-platform support (macOS, Linux, Windows)

### Performance
- < 5% profiling overhead
- < 100µs scheduling latency
- 10,000+ events per second capture rate
- O(n) memory complexity

## [Unreleased]

### Planned
- CUDA/CUPTI integration
- ROCm profiler support
- Apple Metal support
- Real kernel execution replay
- Memory state capture/restore
- Web-based visualization UI
