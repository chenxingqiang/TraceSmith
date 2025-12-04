# Getting Started with TraceSmith

TraceSmith is a cross-platform GPU profiling and replay system designed for AI compilers, deep learning frameworks, and GPU driver engineers. **Real GPU profiling only - no simulation.**

## Supported Platforms

| Platform | GPU | Status |
|----------|-----|--------|
| **CUDA** | NVIDIA (via CUPTI) | ✅ Full support |
| **Metal** | Apple Silicon | ✅ Full support |
| **ROCm** | AMD | 🔜 Coming soon |

## Quick Start

### Installation

#### From Source (C++)

```bash
git clone https://github.com/chenxingqiang/tracesmith.git
cd tracesmith
mkdir build && cd build

# Build with CUDA support (NVIDIA GPU)
cmake .. -DTRACESMITH_ENABLE_CUDA=ON
cmake --build . -j$(nproc)

# Or build with Metal support (Apple GPU)
cmake .. -DTRACESMITH_ENABLE_METAL=ON
cmake --build . -j$(nproc)
```

#### Python (pip)

```bash
# Auto-detect GPU platform
pip install tracesmith

# Or specify platform
TRACESMITH_CUDA=1 pip install tracesmith   # NVIDIA
TRACESMITH_METAL=1 pip install tracesmith  # Apple
```

### Basic Usage

#### Command Line

```bash
# Record a trace (auto-detect GPU)
./bin/tracesmith-cli record -o trace.sbt -d 5

# Record with specific platform
./bin/tracesmith-cli record -o trace.sbt -d 5 -p cuda

# View trace contents
./bin/tracesmith-cli view trace.sbt

# Show statistics
./bin/tracesmith-cli view trace.sbt --stats

# Get trace info
./bin/tracesmith-cli info trace.sbt
```

#### C++ API

```cpp
#include <tracesmith/tracesmith.hpp>  // Main header includes all modules

using namespace tracesmith;

int main() {
    // Auto-detect GPU platform (CUDA, Metal, or ROCm)
    PlatformType platform = detectPlatform();
    if (platform == PlatformType::Unknown) {
        std::cerr << "No GPU detected\n";
        return 1;
    }
    
    // Create profiler for real GPU
    auto profiler = createProfiler(platform);
    
    ProfilerConfig config;
    config.buffer_size = 100000;
    config.capture_kernels = true;
    config.capture_memcpy = true;
    profiler->initialize(config);
    
    // Capture real GPU events
    profiler->startCapture();
    // ... Your GPU operations (CUDA kernels, etc.) ...
    profiler->stopCapture();
    
    // Get events
    std::vector<TraceEvent> events;
    profiler->getEvents(events, 0);
    
    std::cout << "Captured " << events.size() << " events\n";
    
    // Save to file
    SBTWriter writer("trace.sbt");
    writer.writeEvents(events);
    writer.finalize();
    
    // Export to Perfetto (view at ui.perfetto.dev)
    PerfettoExporter exporter;
    exporter.exportToFile(events, "trace.json");
    
    return 0;
}
```

#### Python API

```python
import tracesmith as ts

# Detect GPU platform
platform = ts.detect_platform()
print(f"Platform: {ts.platform_type_to_string(platform)}")

if platform == ts.PlatformType.Unknown:
    print("No GPU detected")
    exit(1)

# Check specific platforms
if ts.is_cuda_available():
    print(f"CUDA devices: {ts.get_cuda_device_count()}")
elif ts.is_metal_available():
    print(f"Metal devices: {ts.get_metal_device_count()}")

# Create profiler for real GPU
profiler = ts.create_profiler(platform)
config = ts.ProfilerConfig()
config.buffer_size = 100000
config.capture_kernels = True
config.capture_memcpy = True
profiler.initialize(config)

# Capture real GPU events
profiler.start_capture()
# ... Your GPU operations ...
profiler.stop_capture()

events = profiler.get_events()
print(f"Captured {len(events)} events")

# Build timeline
timeline = ts.build_timeline(events)
print(f"GPU utilization: {timeline.gpu_utilization * 100:.1f}%")
print(f"Max concurrent ops: {timeline.max_concurrent_ops}")

# Export to Perfetto (view at ui.perfetto.dev)
ts.export_perfetto(events, "trace.json")

# Save to SBT binary format
writer = ts.SBTWriter("trace.sbt")
writer.write_events(events)
writer.finalize()
```

## Key Features

### 1. Real GPU Event Capture
- Kernel launches and completions (CUPTI/Metal)
- Memory operations (H2D, D2H, D2D)
- Stream synchronization
- Host call stack capture

### 2. Timeline Analysis
- GPU utilization calculation
- Concurrent operation tracking
- Stream-based visualization
- Perfetto export for ui.perfetto.dev

### 3. Trace Replay
- Full and partial replay
- Determinism validation
- Dry-run mode for analysis
- Stream-specific replay

### 4. Performance Benchmarks

| Feature | Performance |
|---------|-------------|
| GPU Event Capture | 93K+ kernels/sec |
| Ring Buffer Throughput | 10K+ events/sec |
| Event Collection Overhead | < 1% |
| Stack Capture | ~5 µs/stack |

## Architecture

```
┌─────────────────────────────────────────────┐
│               TraceSmith                    │
├─────────────────────────────────────────────┤
│ 1. Data Capture Layer                       │
│    - CUDA (CUPTI) / Metal / ROCm            │
│    - Ring Buffer (Lock-free SPSC)           │
│    - Call Stack Capture                     │
├─────────────────────────────────────────────┤
│ 2. Trace Format Layer                       │
│    - SBT (TraceSmith Binary Trace)          │
│    - Perfetto JSON/Protobuf Export          │
├─────────────────────────────────────────────┤
│ 3. Analysis Layer                           │
│    - GPU Timeline Builder                   │
│    - State Machine Generator                │
│    - Memory Profiler                        │
├─────────────────────────────────────────────┤
│ 4. Replay Engine                            │
│    - Instruction Replay                     │
│    - Deterministic Checker                  │
└─────────────────────────────────────────────┘
```

## Examples

```bash
# Build and run examples
cd build
make cupti_example           # CUDA profiling
make memory_profiler_example # GPU memory tracking
make benchmark_10k_stacks    # 10K stack capture benchmark
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
