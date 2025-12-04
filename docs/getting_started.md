# Getting Started with TraceSmith

TraceSmith is a cross-platform GPU profiling and replay system designed for AI compilers, deep learning frameworks, and GPU driver engineers.

## Quick Start

### Installation

#### From Source (C++)

```bash
git clone https://github.com/chenxingqiang/tracesmith.git
cd tracesmith
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

#### Python (pip)

```bash
pip install tracesmith
```

### Basic Usage

#### Command Line

```bash
# Record a trace (simulation mode)
./bin/tracesmith-cli record -o trace.sbt -d 5

# View trace contents
./bin/tracesmith-cli view trace.sbt

# Get trace info
./bin/tracesmith-cli info trace.sbt
```

#### C++ API

```cpp
#include <tracesmith/tracesmith.hpp>

using namespace tracesmith;

int main() {
    // Create profiler
    auto profiler = createProfiler(PlatformType::Simulation);
    
    ProfilerConfig config;
    profiler->initialize(config);
    
    // Capture events
    profiler->startCapture();
    // ... GPU operations ...
    profiler->stopCapture();
    
    // Get events
    std::vector<TraceEvent> events;
    profiler->getEvents(events, 0);
    
    // Save to file
    SBTWriter writer("trace.sbt");
    writer.writeEvents(events);
    writer.finalize();
    
    return 0;
}
```

#### Python API

```python
import tracesmith as ts

# Capture events
events = ts.capture_trace(duration_ms=1000)

# Build timeline
timeline = ts.build_timeline(events)
print(f"GPU utilization: {timeline.gpu_utilization * 100:.1f}%")

# Export to Perfetto
ts.export_perfetto(events, "trace.json")

# Replay trace
result = ts.replay_trace(events)
print(f"Replay success: {result.success}")
```

## Key Features

### 1. Event Capture
- Kernel launches and completions
- Memory operations (H2D, D2H, D2D)
- Stream synchronization
- Custom markers

### 2. Timeline Analysis
- GPU utilization calculation
- Concurrent operation tracking
- Stream-based visualization
- Perfetto export for chrome://tracing

### 3. Trace Replay
- Full and partial replay
- Determinism validation
- Dry-run mode for analysis
- Stream-specific replay

## Architecture

```
┌─────────────────────────────────────────────┐
│               TraceSmith                    │
├─────────────────────────────────────────────┤
│ 1. Data Capture Layer                       │
│    - Platform abstraction (CUDA/ROCm/Metal) │
│    - Ring Buffer (Lock-free)                │
├─────────────────────────────────────────────┤
│ 2. Trace Format Layer                       │
│    - SBT (TraceSmith Binary Trace)          │
│    - Event Encoding / Compression           │
├─────────────────────────────────────────────┤
│ 3. Analysis Layer                           │
│    - GPU Timeline Builder                   │
│    - State Machine Generator                │
├─────────────────────────────────────────────┤
│ 4. Replay Engine                            │
│    - Instruction Replay                     │
│    - Deterministic Checker                  │
└─────────────────────────────────────────────┘
```

## Next Steps

- Read the [Installation Guide](installation.md) for detailed setup
- Check the [CLI Reference](cli_reference.md) for command-line usage
- See the [Python Guide](python_guide.md) for Python integration
- Explore [examples/](../examples/) for more code samples

## Support

- GitHub Issues: Report bugs and request features
- Documentation: Full API reference available
- Examples: Working code samples for all features
