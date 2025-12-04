# TraceSmith

**TraceSmith** is an open-source, cross-platform GPU Profiling & Replay system designed for AI compilers, deep learning frameworks, and GPU driver engineers.

[![Build Status](https://github.com/chenxingqiang/tracesmith/workflows/CI/badge.svg)](https://github.com/chenxingqiang/tracesmith/actions)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

## Features

- **High-Performance Event Capture**: Collect 10,000+ GPU instruction-level call stacks without interrupting execution
- **Lock-Free Ring Buffer**: Minimal overhead event collection using SPSC (Single Producer Single Consumer) design
- **SBT Binary Trace Format**: Compact, efficient binary format with string interning and delta timestamp encoding
- **Multi-Platform Support**: NVIDIA CUDA (via CUPTI), AMD ROCm, Apple Metal (planned)
- **Multi-GPU & Multi-Stream**: Full support for complex GPU topologies and async execution
- **Perfetto SDK Integration**: Native protobuf export (85% smaller files) + JSON fallback
- **Real-time Tracing**: Thread-safe `TracingSession` with lock-free buffers (9K+ events/sec)
- **Kineto-Compatible Schema**: PyTorch profiler compatibility with thread tracking, flexible metadata, and structured flows
- **Memory & Counter Profiling**: `MemoryEvent` and `CounterEvent` for detailed resource tracking
- **LLVM XRay Support**: Import compiler-instrumented function traces
- **eBPF Types** (Linux): Kernel-level GPU event tracing support
- **RenderDoc-style Frame Capture**: F12-trigger capture with resource state snapshots
- **GPU Memory Profiler**: Allocation tracking, leak detection, peak usage monitoring
- **CLI Tools**: Easy-to-use command-line interface for recording and viewing traces
- **Simulation Mode**: Test and develop without GPU hardware

## Architecture

```
┌─────────────────────────────────────────────┐
│               TraceSmith                    │
├─────────────────────────────────────────────┤
│ 1. Data Capture Layer                       │
│    - Platform abstraction (CUDA/ROCm/Metal) │
│    - Ring Buffer (Lock-free)                │
│    - Event hooks                            │
├─────────────────────────────────────────────┤
│ 2. Trace Format Layer                       │
│    - SBT (TraceSmith Binary Trace)          │
│    - Event Encoding / Compression           │
├─────────────────────────────────────────────┤
│ 3. State Reconstruction (Phase 3)           │
│    - GPU Timeline Builder                   │
│    - Stream Dependency Graph                │
│    - State Machine Generator                │
├─────────────────────────────────────────────┤
│ 4. Replay Engine (Phase 4)                  │
│    - Instruction Replay                     │
│    - Stream Re-Scheduler                    │
│    - Deterministic Checker                  │
└─────────────────────────────────────────────┘
```

## Quick Start

### Installation

#### Python (Recommended)

```bash
# Install from PyPI (auto-detects GPU platform)
pip install tracesmith

# Platform-specific installation:
# CUDA/CUPTI (NVIDIA GPU)
TRACESMITH_CUDA=1 pip install tracesmith

# ROCm (AMD GPU)
TRACESMITH_ROCM=1 pip install tracesmith

# Metal (Apple GPU)
TRACESMITH_METAL=1 pip install tracesmith

# Verify installation
python -c "import tracesmith; print(tracesmith.__version__, tracesmith.detect_platform())"

# Or install from source
git clone https://github.com/chenxingqiang/TraceSmith.git
cd TraceSmith
TRACESMITH_CUDA=1 pip install .  # with CUDA support
```

#### C++ from Source

**Prerequisites:**
- CMake 3.16+
- C++17 compatible compiler (GCC 8+, Clang 8+, MSVC 2019+)
- Python 3.7+ (for Python bindings)
- (Optional) NVIDIA CUDA Toolkit with CUPTI

**Build:**

```bash
git clone https://github.com/xingqiangchen/TraceSmith.git
cd TraceSmith
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Optionally run tests
ctest --output-on-failure
```

#### Docker

```bash
docker build -t tracesmith .
docker run -it tracesmith
```

### Usage

#### Python API (Recommended)

```python
import tracesmith as ts

# Create profiler for your GPU platform
profiler = ts.create_profiler(ts.PlatformType.CUDA)  # or ROCm, Metal

# Configure and capture
config = ts.ProfilerConfig()
config.capture_kernels = True
config.capture_memcpy = True
profiler.initialize(config)

profiler.start_capture()
# ... your GPU code here (CUDA kernels, etc.) ...
profiler.stop_capture()

# Get captured events
events = profiler.get_events()
print(f"Captured {len(events)} events")

# Build timeline and analyze
timeline = ts.build_timeline(events)
print(f"GPU Utilization: {timeline.gpu_utilization * 100:.1f}%")
print(f"Max Concurrent Ops: {timeline.max_concurrent_ops}")

# Export to Perfetto (chrome://tracing or ui.perfetto.dev)
ts.export_perfetto(events, "trace.json")

# Save to TraceSmith binary format
writer = ts.SBTWriter("trace.sbt")
writer.write_events(events)
writer.finalize()
```

#### Real-time Tracing (v0.3.0+)

```python
import tracesmith as ts

# Create tracing session with custom config
config = ts.TracingConfig()
config.buffer_size_kb = 8192  # 8MB buffer
config.enable_counter_tracks = True

session = ts.TracingSession()
session.start(config)

# Emit events from your application (thread-safe!)
event = ts.TraceEvent()
event.type = ts.EventType.KernelLaunch
event.name = "my_kernel"
event.thread_id = 12345
event.metadata["grid_dim"] = "256x256x1"
session.emit(event)

# Emit counter metrics
session.emit_counter("GPU Memory (MB)", 1024.5)
session.emit_counter("SM Occupancy %", 85.2)

# Stop and export
session.stop()
session.export_to_file("realtime_trace.perfetto-trace")

# Get statistics
stats = session.get_statistics()
print(f"Duration: {stats.duration_ms():.1f}ms")
print(f"Events: {stats.events_emitted} emitted, {stats.events_dropped} dropped")
```

#### Command Line Interface

**Recording a Trace:**

```bash
./bin/tracesmith-cli record -o trace.sbt -d 5
```

**Viewing a Trace:**

```bash
./bin/tracesmith-cli view trace.sbt
./bin/tracesmith-cli info trace.sbt
```

#### C++ API

```cpp
#include <tracesmith/tracesmith.hpp>

using namespace tracesmith;

int main() {
    // Create profiler
    auto profiler = createProfiler(PlatformType::Simulation);
    
    // Configure
    ProfilerConfig config;
    config.buffer_size = 1000000;
    profiler->initialize(config);
    
    // Start capture
    profiler->startCapture();
    
    // ... run GPU code ...
    
    // Stop capture
    profiler->stopCapture();
    
    // Get events
    std::vector<TraceEvent> events;
    profiler->getEvents(events);
    
    // Write to file
    SBTWriter writer("trace.sbt");
    writer.writeEvents(events);
    writer.finalize();
    
    return 0;
}
```

#### Timeline Analysis (Phase 3)

```cpp
#include <tracesmith/tracesmith.hpp>
#include <tracesmith/timeline_builder.hpp>
#include <tracesmith/timeline_viewer.hpp>
#include <tracesmith/perfetto_exporter.hpp>

using namespace tracesmith;

int main() {
    // Capture events (see above)
    std::vector<TraceEvent> events = captureEvents();
    
    // Build timeline
    TimelineBuilder builder;
    builder.addEvents(events);
    Timeline timeline = builder.build();
    
    // Print ASCII visualization
    TimelineViewer viewer;
    std::cout << viewer.render(timeline);
    
    // Export to Perfetto with enhanced GPU tracks
    PerfettoExporter exporter;
    exporter.setEnableGPUTracks(true);       // GPU-specific tracks
    exporter.setEnableFlowEvents(true);      // Dependency visualization
    exporter.exportToFile(events, "trace.json");
    // Open https://ui.perfetto.dev and load trace.json
    
    // Get statistics
    std::cout << "GPU Utilization: " << timeline.gpu_utilization << std::endl;
    std::cout << "Max Concurrent Ops: " << timeline.max_concurrent_ops << std::endl;
    
    return 0;
}
```

## SBT File Format

TraceSmith uses a custom binary format (SBT - TraceSmith Binary Trace) optimized for:

- **Compactness**: Variable-length integer encoding, string interning
- **Streaming**: Support for streaming writes during capture
- **Fast Access**: Indexed sections for random access

File structure:
```
┌──────────────────┐
│ Header (64 bytes)│ Magic, version, offsets
├──────────────────┤
│ Metadata Section │ Application info, timestamps
├──────────────────┤
│ Device Info      │ GPU device details
├──────────────────┤
│ Events Section   │ Trace events (variable length)
├──────────────────┤
│ String Table     │ Deduplicated strings
├──────────────────┤
│ EOF Marker       │
└──────────────────┘
```

## Development Roadmap

### Phase 1: MVP ✅
- [x] Project structure and build system
- [x] Core data structures (TraceEvent, DeviceInfo)
- [x] SBT binary trace format
- [x] Lock-free ring buffer
- [x] Platform abstraction interface
- [x] Simulation profiler
- [x] CLI tools (record, view, info)

### Phase 2: Instruction-Level Call Stack ✅
- [x] Cross-platform stack capture (macOS/Linux/Windows)
- [x] Symbol resolution with demangling
- [x] GPU kernel call chain capture
- [x] Instruction stream builder
- [x] Dependency analysis

### Phase 3: GPU State Machine & Timeline Builder ✅
- [x] GPU state machine with stream tracking
- [x] Timeline builder with span generation
- [x] Perfetto export (chrome://tracing format)
- [x] ASCII timeline visualization
- [x] Concurrent operation analysis

### Phase 4: Replay Engine ✅
- [x] Replay engine with full orchestration
- [x] Stream scheduler with dependency tracking
- [x] Determinism checker with validation
- [x] Partial replay (time/operation ranges)
- [x] Dry-run mode for analysis

### Phase 5: Production Release ✅
- [x] Python bindings (pybind11)
- [x] pip-installable package
- [x] Comprehensive documentation
- [x] Docker support
- [x] Example programs
- [ ] TraceSmith Studio GUI (future)
- [ ] Homebrew formula (future)

### Phase 6: Advanced Integrations ✅ (v0.4.0)
- [x] Perfetto SDK Integration (85% smaller traces)
- [x] Real-time TracingSession with lock-free buffers
- [x] Kineto-compatible schema (thread_id, metadata, FlowInfo)
- [x] Memory profiling (MemoryEvent, MemoryCategory)
- [x] Counter tracks (CounterEvent)
- [x] LLVM XRay import support
- [x] eBPF types for Linux kernel tracing

## Contributing

Contributions are welcome! Please read our [Contributing Guide](CONTRIBUTING.md) before submitting PRs.

## License

TraceSmith is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.

## Benchmark Results

Tested on **NVIDIA GeForce RTX 4090 D** with CUDA 12.8 (Driver 570.124.06):

| Metric | Value |
|--------|-------|
| **Test Suite** | 86 tests |
| **Pass Rate** | 100% (86/86) |
| **Platform** | Linux x86_64, Kernel 5.4.0 |
| **CUPTI Version** | 26 |

### Performance Characteristics

| Feature | Performance |
|---------|-------------|
| Ring Buffer Throughput | 10K+ events/sec |
| Event Collection Overhead | < 1% |
| SBT File Compression | ~3x vs JSON |
| Perfetto Protobuf | 85% smaller than JSON |

### Test Categories

```
✅ RingBuffer Tests      (9/9)   - Lock-free SPSC buffer
✅ SBT Format Tests      (7/7)   - Binary trace format
✅ Types Tests           (12/12) - Core data structures
✅ Kineto Schema Tests   (7/7)   - PyTorch compatibility
✅ Kineto V2 Tests       (6/6)   - Memory & Counter events
✅ TracingSession Tests  (10/10) - Real-time tracing
✅ XRay Importer Tests   (5/5)   - LLVM XRay support
✅ BPF Types Tests       (6/6)   - eBPF integration
✅ FrameCapture Tests    (12/12) - RenderDoc-style capture
✅ MemoryProfiler Tests  (12/12) - GPU memory tracking
```

## PyPI Package

[![PyPI version](https://badge.fury.io/py/tracesmith.svg)](https://badge.fury.io/py/tracesmith)

```bash
pip install tracesmith==0.6.2
```

**Tested on NVIDIA GPU Server (RTX 4090):**

| Feature | Status |
|---------|--------|
| Core Types (69 exports) | ✅ |
| SimulationProfiler | ✅ |
| MemoryProfiler | ✅ |
| Frame Capture | ✅ |
| BPF Tracing | ✅ (Linux) |
| CLI Tools | ✅ |

## Version History

| Version | Date | Highlights |
|---------|------|------------|
| v0.6.2 | 2024-12 | **PyPI release**, Native extension packaging fix |
| v0.6.0 | 2024-12 | NVIDIA CUPTI integration, Full GPU testing |
| v0.5.0 | 2024-12 | RenderDoc-style frame capture, Resource tracking |
| v0.4.0 | 2024-12 | LLVM XRay, eBPF types, TracingSession, Counter tracks |
| v0.3.0 | 2024-12 | Real-time tracing, Counter events, Memory events |
| v0.2.0 | 2024-12 | Perfetto SDK (85% smaller traces), Kineto schema |
| v0.1.1 | 2024-11 | libunwind, Enhanced Perfetto export, Flow events |
| v0.1.0 | 2024-11 | Initial release: SBT format, Ring buffer, Replay |

## Acknowledgments

TraceSmith draws inspiration from:
- [NVIDIA CUPTI](https://docs.nvidia.com/cupti/)
- [ROCm ROCProfiler](https://github.com/ROCm/rocprofiler)
- [Google Perfetto](https://perfetto.dev/)
- [LLVM XRay](https://llvm.org/docs/XRay.html)
- [RenderDoc](https://renderdoc.org/)
- [PyTorch Kineto](https://github.com/pytorch/kineto)

## Contact

- GitHub Issues: [Report a bug](https://github.com/chenxingqiang/tracesmith/issues)
- Discussions: [Ask questions](https://github.com/chenxingqiang/tracesmith/discussions)
