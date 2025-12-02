# TraceSmith

**TraceSmith** is an open-source, cross-platform GPU Profiling & Replay system designed for AI compilers, deep learning frameworks, and GPU driver engineers.

[![Build Status](https://github.com/your-org/tracesmith/workflows/CI/badge.svg)](https://github.com/your-org/tracesmith/actions)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

## Features

- **High-Performance Event Capture**: Collect 10,000+ GPU instruction-level call stacks without interrupting execution
- **Lock-Free Ring Buffer**: Minimal overhead event collection using SPSC (Single Producer Single Consumer) design
- **SBT Binary Trace Format**: Compact, efficient binary format with string interning and delta timestamp encoding
- **Multi-Platform Support**: NVIDIA CUDA (via CUPTI), AMD ROCm, Apple Metal (planned)
- **Multi-GPU & Multi-Stream**: Full support for complex GPU topologies and async execution
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
# Install from PyPI (when published)
pip install tracesmith

# Or install from source
git clone https://github.com/xingqiangchen/TraceSmith.git
cd TraceSmith
pip install .
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

# Capture GPU events
events = ts.capture_trace(duration_ms=1000)
print(f"Captured {len(events)} events")

# Build timeline and analyze
timeline = ts.build_timeline(events)
print(f"GPU Utilization: {timeline.gpu_utilization * 100:.1f}%")
print(f"Max Concurrent Ops: {timeline.max_concurrent_ops}")

# Export to Perfetto (chrome://tracing)
ts.export_perfetto(events, "trace.json")

# Replay trace with validation
result = ts.replay_trace(events, mode=ts.ReplayMode.Full)
print(f"Replay: {result.operations_executed}/{result.operations_total} ops")
print(f"Deterministic: {result.deterministic}")

# Save to TraceSmith format
writer = ts.SBTWriter("trace.sbt")
writer.write_events(events)
writer.finalize()
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
    
    // Export to Perfetto for chrome://tracing
    PerfettoExporter exporter;
    exporter.exportToFile(events, "trace.json");
    // Open chrome://tracing and load trace.json
    
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

## Contributing

Contributions are welcome! Please read our [Contributing Guide](CONTRIBUTING.md) before submitting PRs.

## License

TraceSmith is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.

## Acknowledgments

TraceSmith draws inspiration from:
- [NVIDIA CUPTI](https://docs.nvidia.com/cupti/)
- [ROCm ROCProfiler](https://github.com/ROCm/rocprofiler)
- [Google Perfetto](https://perfetto.dev/)
- [RenderDoc](https://renderdoc.org/)
- [PyTorch Kineto](https://github.com/pytorch/kineto)

## Contact

- GitHub Issues: [Report a bug](https://github.com/your-org/tracesmith/issues)
- Discussions: [Ask questions](https://github.com/your-org/tracesmith/discussions)
