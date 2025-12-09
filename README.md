<p align="center">
  <img src="docs/logo.svg" alt="TraceSmith Logo" >
</p>

<p align="center">
  <strong>Open-source, cross-platform GPU Profiling & Replay System</strong><br>
  Designed for AI compilers, deep learning frameworks, and GPU driver engineers
</p>

<p align="center">
  <a href="https://github.com/chenxingqiang/tracesmith/actions"><img src="https://github.com/chenxingqiang/tracesmith/workflows/CI/badge.svg" alt="Build Status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache%202.0-blue.svg" alt="License"></a>
  <a href="https://pypi.org/project/tracesmith/"><img src="https://badge.fury.io/py/tracesmith.svg" alt="PyPI version"></a>
</p>

---

## Features

- **High-Performance Event Capture**: Collect 10,000+ GPU instruction-level call stacks without interrupting execution
- **Lock-Free Ring Buffer**: Minimal overhead event collection using SPSC (Single Producer Single Consumer) design
- **SBT Binary Trace Format**: Compact, efficient binary format with string interning and delta timestamp encoding
- **Multi-Platform Support**: NVIDIA CUDA (via CUPTI/nsys), AMD ROCm, Apple Metal + Instruments (xctrace), MetaX MACA (via MCPTI)
- **Multi-GPU & Multi-Stream**: Full support for complex GPU topologies and async execution
- **Multi-GPU Cluster Profiling** (v0.7.x): GPUTopology discovery, TimeSync (NTP/PTP/CUDA), NCCLTracker for distributed training
- **Perfetto SDK Integration**: Native protobuf export (85% smaller files) + JSON fallback
- **Real-time Tracing**: Thread-safe `TracingSession` with lock-free buffers (9K+ events/sec)
- **Kineto-Compatible Schema**: PyTorch profiler compatibility with thread tracking, flexible metadata, and structured flows
- **Memory & Counter Profiling**: `MemoryEvent` and `CounterEvent` for detailed resource tracking
- **LLVM XRay Support**: Import compiler-instrumented function traces
- **eBPF Types** (Linux): Kernel-level GPU event tracing support
- **RenderDoc-style Frame Capture**: F12-trigger capture with resource state snapshots
- **GPU Memory Profiler**: Allocation tracking, leak detection, peak usage monitoring
- **CLI Tools**: Easy-to-use command-line interface for recording and viewing traces

## Architecture

![TraceSmith Architecture](docs/architecture.svg)

**Core Modules:**

| Module | Description |
|--------|-------------|
| **Capture** | GPU profiling backends (CUPTI, Metal, BPF, Memory) |
| **Common** | Core types, lock-free ring buffer, stack capture, XRay import |
| **Format** | SBT binary trace format (read/write) |
| **State** | GPU state machine, timeline builder, Perfetto exporters |
| **Replay** | Trace replay engine, stream scheduler, determinism checker |
| **Cluster** | Multi-GPU profiling, time sync, NCCL tracking (v0.7.x) |

**Supported Backends:**

| Platform | Backend | Status |
|----------|---------|--------|
| NVIDIA | CUPTI SDK | ✅ Production |
| Apple | Metal API | ✅ Production |
| Apple | Instruments (xctrace) | ✅ Production |
| MetaX | MCPTI SDK | ✅ Production |
| AMD | ROCm | 🔜 Coming Soon |
| Linux | eBPF | ✅ Available |

**Output Formats:**
- `.sbt` - TraceSmith Binary Trace (compact, indexed)
- `.json` - Perfetto JSON (chrome://tracing)
- `.perfetto` - Perfetto Protobuf (85% smaller)
- `.dot` - Graphviz dependency graph
- ASCII Timeline - Terminal visualization

## Prerequisites & Dependencies

Before installing TraceSmith, install the required dependencies for your platform.

### Linux (Ubuntu/Debian)

```bash
# Core build tools
sudo apt update
sudo apt install -y cmake g++ make git

# Optional: libunwind for call stack capture
sudo apt install -y libunwind-dev

# Optional: Python development headers (for Python bindings)
sudo apt install -y python3-dev python3-pip
```

### Linux with NVIDIA CUDA

```bash
# 1. Install CUDA Toolkit (includes CUPTI)
# Download from: https://developer.nvidia.com/cuda-downloads
# Or use package manager:
sudo apt install -y nvidia-cuda-toolkit

# 2. Install Nsight Systems (required for `tracesmith profile --nsys`)
# Option A: Install from CUDA repository (recommended)
sudo apt install -y cuda-nsight-systems-12-8  # Match your CUDA version

# Option B: Install standalone
sudo apt install -y nsight-systems

# 3. Verify installation
nvcc --version           # CUDA compiler
nsys --version           # Nsight Systems profiler (2024.x recommended)
ls /usr/local/cuda/include/cupti.h  # CUPTI headers

# 4. Set environment variables (add to ~/.bashrc)
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

### Linux with MetaX MACA

```bash
# Install MACA SDK (includes MCPTI)
# Contact MetaX for SDK access: https://www.metax-tech.com

# Set MACA_ROOT environment variable
export MACA_ROOT=/opt/maca-3.0.0

# Install mcTracer (required for `tracesmith profile --mctracer`)
# mcTracer is included in MACA SDK
```

### macOS

```bash
# Install Xcode Command Line Tools (includes Metal framework)
xcode-select --install

# Install CMake via Homebrew
brew install cmake

# Verify Metal support
xcrun metal --version

# Note: Instruments (xctrace) is included with Xcode
# Used for `tracesmith profile --xctrace`
```

### Windows

```powershell
# Install Visual Studio 2019+ with C++ workload
# Install CMake: https://cmake.org/download/

# For CUDA support:
# Install CUDA Toolkit: https://developer.nvidia.com/cuda-downloads
# Install Nsight Systems: https://developer.nvidia.com/nsight-systems
```

### Version Requirements

| Dependency | Minimum Version | Recommended | Notes |
|------------|----------------|-------------|-------|
| CMake | 3.16 | 3.22+ | Build system |
| GCC | 8.0 | 11+ | C++17 support |
| Clang | 8.0 | 14+ | C++17 support |
| CUDA Toolkit | 11.0 | 12.x | CUPTI included |
| Nsight Systems | 2022.x | 2024.x | GPU profiling |
| libunwind | 1.3 | 1.6+ | Stack capture |
| Python | 3.7 | 3.10+ | Python bindings |

---

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
- (Optional) Nsight Systems for `--nsys` profiling
- (Optional) Xcode Command Line Tools (for Metal on macOS)

**Basic Build:**

```bash
git clone https://github.com/chenxingqiang/TraceSmith.git
cd TraceSmith
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

**CMake Build Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Debug | Build type: Debug, Release, RelWithDebInfo |
| `TRACESMITH_ENABLE_CUDA` | OFF | Enable NVIDIA CUDA/CUPTI support |
| `TRACESMITH_ENABLE_ROCM` | OFF | Enable AMD ROCm support |
| `TRACESMITH_ENABLE_METAL` | OFF | Enable Apple Metal support |
| `TRACESMITH_ENABLE_MACA` | OFF | Enable MetaX MACA/MCPTI support |
| `TRACESMITH_BUILD_PYTHON` | OFF | Build Python bindings (pybind11) |
| `TRACESMITH_BUILD_TESTS` | ON | Build unit tests (Google Test) |
| `TRACESMITH_BUILD_EXAMPLES` | ON | Build example programs |
| `TRACESMITH_BUILD_CLI` | ON | Build command-line interface |
| `TRACESMITH_USE_PERFETTO_SDK` | OFF | Use Perfetto SDK for protobuf export |

**Build Examples:**

```bash
# Release build with Metal support (macOS)
cmake .. -DCMAKE_BUILD_TYPE=Release -DTRACESMITH_ENABLE_METAL=ON
cmake --build . -j$(nproc)

# CUDA build (Linux/Windows with NVIDIA GPU)
cmake .. -DCMAKE_BUILD_TYPE=Release -DTRACESMITH_ENABLE_CUDA=ON
cmake --build . -j$(nproc)

# Full build with all features
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DTRACESMITH_ENABLE_METAL=ON \
         -DTRACESMITH_BUILD_PYTHON=ON \
         -DTRACESMITH_USE_PERFETTO_SDK=ON
cmake --build . -j$(nproc)

# Minimal build (library only, no tests/examples/CLI)
cmake .. -DTRACESMITH_BUILD_TESTS=OFF \
         -DTRACESMITH_BUILD_EXAMPLES=OFF \
         -DTRACESMITH_BUILD_CLI=OFF
cmake --build . -j$(nproc)
```

**Install:**

```bash
# Install to default location (/usr/local)
sudo cmake --install .

# Install to custom prefix
cmake --install . --prefix /path/to/install

# Installed files:
#   bin/tracesmith          - CLI executable
#   include/tracesmith/     - Header files
#   lib/libtracesmith-*.a   - Static libraries
```

**Run Tests:**

```bash
# Run all tests
ctest --output-on-failure

# Run specific test
./bin/tracesmith_tests --gtest_filter="RingBuffer*"
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

TraceSmith provides a comprehensive CLI with ASCII banner and colored output:

```
████████╗██████╗  █████╗  ██████╗███████╗███████╗███╗   ███╗██╗████████╗██╗  ██╗
╚══██╔══╝██╔══██╗██╔══██╗██╔════╝██╔════╝██╔════╝████╗ ████║██║╚══██╔══╝██║  ██║
   ██║   ██████╔╝███████║██║     █████╗  ███████╗██╔████╔██║██║   ██║   ███████║
   ██║   ██╔══██╗██╔══██║██║     ██╔══╝  ╚════██║██║╚██╔╝██║██║   ██║   ██╔══██║
   ██║   ██║  ██║██║  ██║╚██████╗███████╗███████║██║ ╚═╝ ██║██║   ██║   ██║  ██║
   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚══════╝╚══════╝╚═╝     ╚═╝╚═╝   ╚═╝   ╚═╝  ╚═╝

                    GPU Profiling & Replay System v0.8.1
```

**Available Commands:**

| Command | Description |
|---------|-------------|
| `profile` | **Profile a command** (record + execute in one step) |
| `record` | Record GPU events to a trace file |
| `view` | View contents of a trace file |
| `info` | Show detailed information about a trace file |
| `export` | Export trace to Perfetto or other formats |
| `analyze` | Analyze trace for performance insights |
| `replay` | Replay a captured trace |
| `benchmark` | Run 10K GPU call stacks benchmark |
| `devices` | List available GPU devices (CUDA, Metal, MACA, ROCm) |
| `version` | Show version information |
| `help` | Show help message |

> **IMPORTANT: GPU Profiling API Limitations**
>
> CUPTI/MCPTI can only profile the calling process, not child processes.
> For CUDA/MACA platforms, you **MUST** use system-level profilers:
>
> | Platform | Required Option | Tool | Min Version |
> |----------|-----------------|------|-------------|
> | NVIDIA CUDA | `--nsys` | Nsight Systems | 2022.x (2024.x recommended) |
> | MetaX MACA | `--mctracer` | mcTracer | MACA SDK 3.0+ |
> | Apple Metal | `--xctrace` | Instruments | Xcode 14+ |
>
> The `record` command is **not supported** for CUDA/MACA platforms.
> Use `profile --nsys` or `profile --mctracer` instead.

**⚠️ Important Notes:**

1. **Nsight Systems Version**: Use version 2024.x or later. Older versions (2021.x) may have compatibility issues with newer CUDA drivers.
   ```bash
   # Check your nsys version
   nsys --version
   
   # Install latest version (Ubuntu with CUDA 12.x)
   sudo apt install cuda-nsight-systems-12-8
   ```

2. **CUPTI Conflict**: When using `--nsys`, your program should **not** use CUPTI directly (nsys subscribes to CUPTI). If your program uses CUPTI, run it without `--nsys`:
   ```bash
   # For programs using CUPTI internally
   ./bin/cupti_example  # Run directly, not with --nsys
   ```

3. **Root/Admin Not Required**: Modern nsys (2024.x) does not require root privileges for basic profiling.

**C++ CLI Examples:**

```bash
# NVIDIA CUDA - Use --nsys (REQUIRED for GPU profiling)
./bin/tracesmith profile --nsys -- python train.py
./bin/tracesmith profile --nsys --perfetto -- ./my_cuda_app
./bin/tracesmith profile --nsys -o model.sbt -- python train.py --epochs 10

# MetaX MACA - Use --mctracer (REQUIRED for GPU profiling)
./bin/tracesmith profile --mctracer -- ./my_maca_app
./bin/tracesmith profile --mctracer --perfetto -- python train.py

# Apple Metal - Use --xctrace for real Metal GPU events
./bin/tracesmith profile --xctrace -- python train.py
./bin/tracesmith profile --xctrace --keep-trace -- python mps_benchmark.py
./bin/tracesmith profile --xctrace --xctrace-template "GPU Driver" -- ./app

# View trace with statistics
./bin/tracesmith view trace.sbt --stats

# Show trace file info
./bin/tracesmith info trace.sbt

# Export to Perfetto (view at ui.perfetto.dev)
./bin/tracesmith export trace.sbt -f perfetto

# Analyze performance
./bin/tracesmith analyze trace.sbt

# Replay trace (dry-run)
./bin/tracesmith replay trace.sbt --mode dry-run

# List available GPUs
./bin/tracesmith devices

# Disable colored output
./bin/tracesmith --no-color help
```

**Python CLI Examples:**

```bash
# NVIDIA CUDA - Use --nsys (REQUIRED for GPU profiling)
tracesmith-cli profile --nsys -- python train.py
tracesmith-cli profile --nsys --perfetto -- ./my_cuda_app
tracesmith-cli profile --nsys -o model.sbt -- python train.py --epochs 10

# MetaX MACA - Use --mctracer (REQUIRED for GPU profiling)
tracesmith-cli profile --mctracer -- ./my_maca_app
tracesmith-cli profile --mctracer --perfetto -- python train.py

# Apple Metal - Use --xctrace for real Metal GPU events
tracesmith-cli profile --xctrace -- python train.py
tracesmith-cli profile --xctrace --keep-trace -- python mps_benchmark.py

# Other commands
tracesmith-cli info
tracesmith-cli devices

# View trace contents
tracesmith-cli view trace.sbt --stats

# Export to Perfetto
tracesmith-cli export trace.sbt -o trace.json

# Analyze trace
tracesmith-cli analyze trace.sbt

# Replay trace
tracesmith-cli replay trace.sbt --mode dry-run
```

#### NVIDIA Nsight Systems (nsys) Integration

TraceSmith integrates with NVIDIA Nsight Systems for system-wide GPU profiling, providing comprehensive CUDA kernel and memory operation tracing.

**Usage:**

```bash
# Profile with nsys (system-wide profiling)
./bin/tracesmith profile --nsys -- python train.py
./bin/tracesmith profile --nsys -o trace.sbt -- ./my_cuda_app

# With custom nsys options
./bin/tracesmith profile --nsys --nsys-args="-t cuda,nvtx" -- python benchmark.py
```

**Features:**
- System-wide CUDA kernel tracing
- Memory transfer profiling (H2D, D2H, D2D)
- NVTX annotation support
- Multi-GPU profiling
- Automatic .nsys-rep to TraceSmith format conversion

#### macOS Metal GPU Profiling with xctrace

On macOS, TraceSmith integrates with Apple Instruments (xctrace) for capturing real Metal GPU events. This provides accurate GPU timing and event capture that the Metal Frame Capture API cannot achieve programmatically.

**Why use xctrace?**
- Captures real Metal GPU execution events (kernel launches, command buffer submissions)
- Accurate GPU timing from hardware counters
- Works with any Metal application (PyTorch MPS, TensorFlow Metal, custom Metal apps)

**Usage:**

```bash
# Python CLI (recommended - includes event parsing)
tracesmith-cli profile --xctrace -- python train.py
tracesmith-cli profile --xctrace --keep-trace -o model.sbt -- python inference.py
tracesmith-cli profile --xctrace --perfetto -- python benchmark.py

# C++ CLI (calls xctrace, outputs raw .trace file)
./bin/tracesmith profile --xctrace -- python train.py
./bin/tracesmith profile --xctrace --xctrace-template "GPU Driver" -- ./app

# Python API
from tracesmith.xctrace import XCTraceProfiler, profile_with_xctrace

# Simple usage
events, trace_file = profile_with_xctrace(
    ["python", "train.py"],
    duration=60,
    template="Metal System Trace"
)

# Full control
profiler = XCTraceProfiler()
events = profiler.profile_command(["python", "train.py"])
profiler.export_perfetto("metal_trace.json")
```

**Available Templates:**
- `Metal System Trace` - Most detailed Metal profiling (default)
- `GPU Driver` - Driver-level analysis
- `Game Performance` - Frame rate and GPU time
- `Animation Hitches` - Animation performance

**Output:**

#### MetaX GPU Profiling with MCPTI

TraceSmith supports MetaX GPUs (C500, C550, etc.) using the MCPTI (MACA Profiling Tools Interface), which provides an API compatible with NVIDIA CUPTI.

> 📖 **Full documentation**: See [docs/MACA_PROFILING.md](docs/MACA_PROFILING.md) for detailed setup and usage guide.

**Tested Hardware:**

| GPU | Memory | Compute Units | Driver | Status |
|-----|--------|---------------|--------|--------|
| MetaX C500 | 64 GB | 104 CUs | 3.0.11 | ✅ Verified |
| MetaX C550 | - | - | - | 🔜 Planned |

**Benchmark Results (MetaX C500, MACA 3.0.0):**

| Test | Data Size | Bandwidth |
|------|-----------|-----------|
| Host → Device | 256 MB | 10.1 GB/s |
| Device → Host | 256 MB | 9.9 GB/s |
| Device → Device | 256 MB | **608 GB/s** |
| **MCPTI Overhead** | - | **< 2%** (negligible) |

**CLI Device Detection:**
```bash
$ ./bin/tracesmith devices

MetaX MACA:
✓ MACA available
  Devices: 1
  Driver:  3000

  Device 0: MetaX C500
    Vendor:     MetaX
    Compute:    10.0
    Memory:     63.62 GB
    SMs:        104
    Clock:      1600 MHz
```

**System-Wide Profiling with mcTracer:**

TraceSmith integrates with MetaX's `mcTracer` tool for comprehensive system-wide GPU profiling (similar to NVIDIA nsys):

```bash
# Profile with mcTracer
./bin/tracesmith profile --mctracer -- ./my_maca_app
./bin/tracesmith profile --mctracer --perfetto -- python train.py

# Output is Perfetto-compatible JSON
# View at: https://ui.perfetto.dev
```

mcTracer captures:
- All MACA API calls (mcInit, mcMalloc, mcMemcpy, etc.)
- GPU memory operations with bandwidth
- Stream operations and synchronization
- CPU-GPU launch flow arrows

**Build with MetaX support:**

```bash
# On MetaX system (MACA SDK auto-detected at /opt/maca-3.0.0)
cmake -DTRACESMITH_ENABLE_MACA=ON ..
make -j4

# Run examples
./bin/metax_example      # Basic profiling demo
./bin/metax_benchmark    # Memory bandwidth test
```

**C++ API:**

```cpp
#include <tracesmith/tracesmith.hpp>

// Check MetaX GPU availability
if (tracesmith::isMACAAvailable()) {
    std::cout << "MetaX devices: " << tracesmith::getMACADeviceCount() << std::endl;
}

// Create MCPTI profiler
auto profiler = tracesmith::createProfiler(tracesmith::PlatformType::MACA);

// Configure
tracesmith::ProfilerConfig config;
config.capture_kernels = true;
config.capture_memcpy = true;
profiler->initialize(config);

// Capture events
profiler->startCapture();
// ... GPU code using MACA runtime ...
profiler->stopCapture();

// Get events
std::vector<tracesmith::TraceEvent> events;
profiler->getEvents(events);

// Export to Perfetto
tracesmith::PerfettoExporter exporter;
exporter.exportToFile(events, "metax_trace.json");
```

**Python API:**

```python
import tracesmith as ts

# Check MetaX availability
if ts.is_maca_available():
    print(f"MetaX devices: {ts.get_maca_device_count()}")
    
    # Create profiler
    profiler = ts.create_profiler(ts.PlatformType.MACA)
    profiler.initialize(ts.ProfilerConfig())
    
    profiler.start_capture()
    # ... GPU code ...
    profiler.stop_capture()
    
    events = profiler.get_events()
    
    # Save trace
    writer = ts.SBTWriter("metax_trace.sbt")
    writer.write_events(events)
    writer.finalize()
```

**MCPTI Captured Events:**

| Event Type | Description |
|------------|-------------|
| KernelLaunch/Complete | Kernel execution timing with grid/block dimensions |
| MemcpyH2D/D2H/D2D | Memory transfers with bandwidth calculation |
| MemsetDevice | Memory initialization operations |
| StreamSync/DeviceSync | Synchronization events with duration |

**Output:**
- `.sbt` - TraceSmith Binary Trace format
- `.json` - Perfetto JSON (view at https://ui.perfetto.dev)
- Device info: name, memory, compute capability, clock rates

#### Python Examples with Cross-Platform Device Support

All Python examples support multiple GPU platforms with automatic device detection:

```bash
# Run examples on specific device
python examples/basic_usage.py --device cuda    # NVIDIA GPU
python examples/basic_usage.py --device mps     # Apple Silicon
python examples/basic_usage.py --device rocm    # AMD GPU
python examples/basic_usage.py --device cpu     # CPU fallback

# Run all examples with test runner
python examples/run_tests.py                    # Best available device
python examples/run_tests.py --all-devices      # Test on all devices
python examples/run_tests.py --test pytorch     # Run specific test
python examples/run_tests.py --list             # List available tests
```

**Using DeviceManager for cross-platform code:**

```python
from examples.device_utils import DeviceManager, benchmark

# Auto-detect best device
dm = DeviceManager()  # or DeviceManager(prefer_device="mps")
print(f"Using: {dm.get_device_name()}")  # Apple Silicon GPU (mps:0, 25.2 GB)

# Create tensors on device
x = dm.randn(1000, 1000)
y = dm.randn(1000, 1000)

# Benchmark with proper synchronization
results = benchmark(lambda: x @ y, warmup=3, iterations=10, dm=dm)
print(f"Mean: {results['mean_ms']:.2f} ms")

# Device-agnostic operations
dm.synchronize()
print(f"Memory: {dm.memory_allocated() / 1024**2:.1f} MB")
```

#### C++ API

```cpp
#include <tracesmith/tracesmith.hpp>

using namespace tracesmith;

int main() {
    // Create profiler
    auto profiler = createProfiler(PlatformType::CUDA);
    
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
#include <tracesmith/state/timeline_builder.hpp>
#include <tracesmith/state/timeline_viewer.hpp>
#include <tracesmith/state/perfetto_exporter.hpp>

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

### Core Feature: 10,000+ GPU Instruction-Level Call Stacks

**Tested on NVIDIA GeForce RTX 4090 D** (24GB, CUDA 12.8, Driver 570.124.06)

```
╔═══════════════════════════════════════════════════════════════════════╗
║  Non-intrusive capture of 10,000+ instruction-level GPU call stacks   ║
║  ✅ VERIFIED!                                                         ║
╚═══════════════════════════════════════════════════════════════════════╝
```

| Metric | Result | Note |
|--------|--------|------|
| **CUDA Kernels Launched** | 10,000 | Real `__global__` kernels |
| **GPU Events (CUPTI)** | 20,011 | Instruction-level events |
| **Kernel Launches** | 10,000 | Each kernel captured |
| **Kernel Completes** | 10,000 | Full lifecycle |
| **Host Call Stacks** | 10,000 | 7 frames/stack avg |
| **Events with Stacks** | 19,989 (99.9%) | GPU + Host merged |
| **Total Time** | 107 ms | Non-intrusive |
| **Throughput** | 93,457 kernels/sec | High performance |

**Verified Capabilities:**
- ✅ Real CUDA kernels executed on GPU )
- ✅ CUPTI captured instruction-level GPU events
- ✅ Host call stacks attached to GPU events
- ✅ Non-intrusive profiling

### How to Run the Benchmark

```bash
# On NVIDIA GPU server with CUDA
git clone https://github.com/chenxingqiang/TraceSmith.git
cd TraceSmith
mkdir build && cd build

# Build with CUDA support
cmake .. -DTRACESMITH_ENABLE_CUDA=ON -DTRACESMITH_BUILD_EXAMPLES=ON
make benchmark_10k_stacks -j8

# Run the benchmark
./bin/benchmark_10k_stacks
```

### CUPTI Real GPU Profiling Results

| Kernel | Duration (ns) | Duration (µs) | Duration (ms) |
|--------|---------------|---------------|---------------|
| vectorAdd (1M elements) | 5,313 | 5.31 | 0.0053 |
| matrixMul (512×512) | 66,912 | 66.91 | 0.0669 |
| relu (1M elements) | 4,704 | 4.70 | 0.0047 |
| **TOTAL** | **76,929** | **76.93** | **0.0769** |

### Real GPU Memory Profiling Results

| Phase | Operation | Memory |
|-------|-----------|--------|
| Parameters | 5× cudaMalloc | 31 MB |
| Activations | 8× cudaMalloc | 72 MB |
| Gradients | 5× cudaMalloc | 31 MB |
| Workspace | 3× cudaMalloc | 96 MB |
| **Total Allocated** | 21 operations | **230 MB** |
| **Total Freed** | 16 cudaFree | **199 MB** |
| **Test Duration** | - | **5 ms** |

### Performance Characteristics

| Feature | Performance |
|---------|-------------|
| GPU Event Capture | 93K+ kernels/sec |
| Ring Buffer Throughput | 10K+ events/sec |
| Event Collection Overhead | < 1% |
| SBT File Compression | ~3x vs JSON |
| Perfetto Protobuf | 85% smaller than JSON |
| Stack Capture (no symbols) | ~5 µs/stack |
| Stack Capture (with symbols) | ~13 µs/stack |

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
✅ CUPTI Profiler        (14/14) - Real GPU profiling
```

## PyPI Package

[![PyPI version](https://badge.fury.io/py/tracesmith.svg)](https://badge.fury.io/py/tracesmith)

```bash
# Basic installation
pip install tracesmith==0.8.1

# With CuPy for real GPU profiling in Python CLI (choose one):
pip install tracesmith[cuda12]    # CUDA 12.x
pip install tracesmith[cuda11]    # CUDA 11.x
pip install tracesmith[cuda118]   # CUDA 11.8 specific
pip install tracesmith[cuda120]   # CUDA 12.0 specific

# With visualization tools
pip install tracesmith[visualization]

# With PyTorch integration
pip install tracesmith[torch]

# All optional dependencies
pip install tracesmith[all]
```

### Python CLI Real GPU Benchmark

With CuPy installed, you can run real GPU profiling from Python:

```bash
# Install CuPy first
pip install tracesmith[cuda12]

# Run real GPU benchmark
tracesmith-cli benchmark --real-gpu -n 10000
```

**Tested on NVIDIA GPU Server (RTX 4090):**

| Feature | Status |
|---------|--------|
| Core Types (69 exports) | ✅ |
| CUPTIProfiler | ✅ |
| MemoryProfiler | ✅ |
| Frame Capture | ✅ |
| Stack Capture | ✅ |
| BPF Tracing | ✅ (Linux) |
| CLI Tools | ✅ |

## Testing Methodology

### Feature Validation

TraceSmith provides a comprehensive validation example that tests all features from [PLANNING.md](docs/PLANNING.md):

```bash
# Build and run feature validation
cd build
cmake .. -DTRACESMITH_ENABLE_CUDA=ON -DTRACESMITH_BUILD_EXAMPLES=ON
make goal_validation_example
./bin/goal_validation_example
```

### Benchmark Testing

The `benchmark_10k_stacks` uses **real CUDA kernels and CUPTI profiling**:

```cpp
// Real CUDA kernel executed on GPU
__global__ void benchmark_kernel(float* data, int n, int kernel_id) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        data[idx] = data[idx] * 2.0f + static_cast<float>(kernel_id);
    }
}

// Launches 10,000 real kernels with CUPTI profiling
for (int i = 0; i < 10000; ++i) {
    benchmark_kernel<<<blocks, threads>>>(d_data, n, i);
}
```

### Platform-Specific Testing

| Platform | Profiler | Test Command |
|----------|----------|--------------|
| **NVIDIA CUDA** | CUPTIProfiler | `./bin/cupti_example` |
| **Apple Metal** | MetalProfiler | `./bin/metal_example` |
| **MetaX MACA** | MCPTIProfiler | `./bin/metax_example` |
| **CPU Fallback** | StackCapture | `./bin/stack_capture_example` |

## Version History

| Version | Date | Highlights |
|---------|------|------------|
| **v0.8.2** | 2025-12 | **CLI Breaking Change** - Enforce --nsys/--mctracer for CUDA/MACA, record command blocked, clearer API limitation messages |
| **v0.8.1** | 2025-12 | **nsys & MACA Enhancement** - NVIDIA Nsight Systems integration, MetaX CLI device detection, MACA cluster module support |
| **v0.8.1** | 2025-12 | **mcTracer Integration** - MetaX system-wide profiling, Enhanced MACA CLI, Cluster module support |
| **v0.8.0** | 2025-12 | **xctrace Integration** - Apple Instruments, Cross-Platform Device Utils, Enhanced Examples |
| v0.7.1 | 2025-12 | **Multi-GPU Phase 2** - TimeSync, NCCLTracker, ClockCorrelator, CommAnalysis |
| **v0.7.0** | 2025-12 | **Multi-GPU Cluster** - GPUTopology, MultiGPUProfiler, GitHub Actions CI/CD |
| v0.6.9 | 2025-12 | Include reorganization - Directory structure matches `src/` layout |
| v0.6.8 | 2025-12 | Enhanced CLI - ASCII banner, all commands, Python CLI |
| v0.6.7 | 2025-12 | Real GPU benchmark - 10K+ CUDA kernels with CUPTI  |
| v0.6.5 | 2025-12 | StackCapture bindings, OverflowPolicy, detect_leaks |
| v0.6.2 | 2025-12 | PyPI release, Native extension packaging fix |
| v0.6.0 | 2025-12 | NVIDIA CUPTI integration, Full GPU testing |
| v0.5.0 | 2025-12 | RenderDoc-style frame capture, Resource tracking |
| v0.4.0 | 2025-12 | LLVM XRay, eBPF types, TracingSession, Counter tracks |
| v0.3.0 | 2025-12 | Real-time tracing, Counter events, Memory events |
| v0.2.0 | 2025-12 | Perfetto SDK (85% smaller traces), Kineto schema |
| v0.1.1 | 2025-11 | libunwind, Enhanced Perfetto export, Flow events |
| v0.1.0 | 2025-11 | Initial release: SBT format, Ring buffer, Replay |

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
