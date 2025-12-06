# TraceSmith Python Examples

This directory contains comprehensive examples demonstrating how to use TraceSmith for GPU profiling with Python and deep learning frameworks.

## Cross-Platform Device Support

All examples support multiple GPU platforms:

| Platform | Device Flag | Description |
|----------|-------------|-------------|
| **CUDA** | `--device cuda` | NVIDIA GPUs |
| **MPS** | `--device mps` | Apple Silicon (M1/M2/M3) |
| **ROCm** | `--device rocm` | AMD GPUs |
| **CPU** | `--device cpu` | CPU fallback |

If no device is specified, the best available device is auto-detected.

## Examples Overview

| Example | Description | Requirements |
|---------|-------------|--------------|
| `basic_usage.py` | Basic TraceSmith API usage | TraceSmith only |
| `kernel_timing_stats.py` | Kernel execution time statistics | TraceSmith only |
| `pytorch_profiling.py` | PyTorch model inference/training profiling | PyTorch |
| `pytorch_hooks_profiling.py` | Layer-by-layer profiling using hooks | PyTorch |
| `memory_profiling.py` | GPU memory tracking and leak detection | PyTorch (optional) |
| `multi_gpu_profiling.py` | Multi-GPU and DataParallel profiling | PyTorch, multi-GPU |
| `realtime_tracing.py` | Real-time tracing with lock-free buffers | TraceSmith only |
| `transformers_profiling.py` | Transformer/LLM model profiling | PyTorch, transformers |
| `device_utils.py` | Cross-platform device utilities | PyTorch (optional) |
| `run_tests.py` | Test runner for all examples | TraceSmith only |

## Quick Start

### Installation

```bash
# Install TraceSmith (from project root)
cd /path/to/TraceSmith
pip install -e ./python

# Install optional dependencies for examples
pip install torch torchvision transformers
```

### Running Examples

```bash
# Basic usage (auto-detect device)
python examples/basic_usage.py

# Run on specific device
python examples/basic_usage.py --device cuda   # NVIDIA GPU
python examples/basic_usage.py --device mps    # Apple Silicon
python examples/basic_usage.py --device cpu    # CPU fallback

# Run all examples with test runner
python examples/run_tests.py                   # Best device
python examples/run_tests.py --all-devices     # All devices
python examples/run_tests.py --device mps      # Specific device
python examples/run_tests.py --list            # List available tests

# PyTorch profiling
python examples/pytorch_profiling.py --device cuda
```

## Test Runner

The `run_tests.py` script runs all examples and reports results:

```bash
# Run all tests on best available device
python run_tests.py

# Run on all available devices
python run_tests.py --all-devices

# Run specific test
python run_tests.py --test basic
python run_tests.py --test pytorch

# List available tests
python run_tests.py --list
```

**Sample Output:**
```
TraceSmith Examples Test Runner
============================================================
Devices: mps, cpu
Tests: basic, pytorch, hooks, memory, kernel, realtime, multigpu, transformers
============================================================

>>> Running tests on MPS <<<

Running basic...     ✓ PASSED (2.3s)
Running pytorch...   ✓ PASSED (15.4s)
Running memory...    ✓ PASSED (8.2s)
...

======================================================================
TEST RESULTS SUMMARY
======================================================================

MPS:
--------------------------------------------------
  ✓ PASSED basic                (2.3s)
  ✓ PASSED pytorch              (15.4s)
  ✓ PASSED memory               (8.2s)
  ...

  Total: 7 passed, 0 failed, 1 skipped, 0 errors

======================================================================
OVERALL: 7/8 tests passed
```

## Device Utilities

The `device_utils.py` module provides cross-platform device management:

```python
from device_utils import DeviceManager, get_device, benchmark

# Auto-detect best device
dm = DeviceManager()
print(f"Using: {dm.get_device_name()}")

# Or specify device
dm = DeviceManager(prefer_device="mps")

# Create tensors on device
x = dm.randn(1000, 1000)
y = dm.randn(1000, 1000)

# Benchmark with proper synchronization
def matmul():
    return x @ y

results = benchmark(matmul, warmup=3, iterations=10, dm=dm)
print(f"Mean: {results['mean_ms']:.2f} ms")

# Device-agnostic synchronization
dm.synchronize()

# Memory info
print(f"Allocated: {dm.memory_allocated() / 1024**2:.1f} MB")
```

### DeviceManager API

```python
from device_utils import DeviceManager

dm = DeviceManager(prefer_device="cuda")  # or "mps", "rocm", "cpu", None

# Properties
dm.device           # DeviceInfo object
dm.device_type      # DeviceType enum
dm.torch_device     # PyTorch device object
dm.is_gpu           # bool
dm.is_cuda          # bool
dm.is_mps           # bool
dm.is_rocm          # bool

# Methods
dm.list_devices()           # List all detected devices
dm.synchronize()            # Sync current device
dm.empty_cache()            # Clear memory cache
dm.memory_allocated()       # Current memory usage
dm.memory_reserved()        # Reserved memory

# Tensor creation
dm.randn(100, 100)          # Random tensor on device
dm.zeros(100, 100)          # Zero tensor on device
dm.ones(100, 100)           # Ones tensor on device
dm.create_tensor([1, 2, 3]) # From data
dm.to_device(tensor)        # Move to device

# TraceSmith integration
platform = dm.get_tracesmith_platform()
profiler = dm.create_profiler()
```

### Test Decorators

```python
from device_utils import skip_if_no_gpu, skip_if_not_cuda, skip_if_not_mps

@skip_if_no_gpu
def test_gpu_only():
    # Runs only if GPU is available
    pass

@skip_if_not_cuda
def test_cuda_specific():
    # Runs only on CUDA
    pass

@skip_if_not_mps
def test_mps_specific():
    # Runs only on Apple Silicon
    pass
```

## Example Details

### 1. Kernel Timing Statistics (`kernel_timing_stats.py`)

Captures and analyzes GPU kernel execution times with:
- Per-kernel timing statistics (min, max, avg, percentiles)
- Aggregated statistics by kernel name
- CSV/JSON export for further analysis
- Top-N kernels by various metrics

```python
import tracesmith as ts
from kernel_timing_stats import KernelTimingAnalyzer

# Capture events
profiler = ts.create_profiler(ts.detect_platform())
profiler.initialize(ts.ProfilerConfig())
profiler.start_capture()
# ... your GPU code ...
profiler.stop_capture()
events = profiler.get_events()

# Analyze
analyzer = KernelTimingAnalyzer(events)
analyzer.analyze()
analyzer.print_summary(top_n=20, sort_by="total")
analyzer.export_to_csv("kernel_stats.csv")
```

**Output:**
```
================================================================================
KERNEL EXECUTION TIME STATISTICS
================================================================================
Total Kernels: 45 unique, 1250 invocations
Total Kernel Time: 125.34 ms
--------------------------------------------------------------------------------
Kernel Name                              Count    Total(ms)    Avg(µs)    ...
--------------------------------------------------------------------------------
void gemm_kernel<float>                     50      45.230     904.60    ...
void conv2d_forward_kernel                  40      32.150     803.75    ...
...
```

### 2. PyTorch Model Profiling (`pytorch_profiling.py`)

Profile PyTorch models during inference and training:

```python
from pytorch_profiling import TorchProfiler, profile_inference

# Create model
model = YourModel().cuda()

# Profile inference
profiler = profile_inference(model, input_data, num_iterations=10)
profiler.print_summary()
profiler.export_perfetto("model_trace.json")
```

### 3. Layer-by-Layer Profiling (`pytorch_hooks_profiling.py`)

Uses PyTorch hooks to correlate GPU kernels with specific layers:

```python
from pytorch_hooks_profiling import PyTorchLayerProfiler

profiler = PyTorchLayerProfiler()
profile = profiler.profile_forward_pass(model, input_data)
print_layer_profile(profile)

# Analyze bottlenecks
bottlenecks = analyze_bottlenecks(profile)
for b in bottlenecks:
    print(f"  - {b}")
```

### 4. Memory Profiling (`memory_profiling.py`)

Track GPU memory usage, detect leaks, and analyze memory patterns:

```python
from memory_profiling import GPUMemoryTracker

tracker = GPUMemoryTracker()
tracker.start()

# Your code
with tracker.track("forward"):
    output = model(input)

tracker.stop()
tracker.print_summary()
tracker.export_timeline("memory_timeline.json")
```

### 5. Multi-GPU Profiling (`multi_gpu_profiling.py`)

Profile multi-GPU setups including DataParallel:

```python
import tracesmith as ts

# Discover topology
topology = ts.GPUTopology()
topology.discover()
print(topology.to_ascii())

# Multi-GPU profiling
config = ts.MultiGPUConfig()
config.enable_nvlink_tracking = True
profiler = ts.MultiGPUProfiler(config)
profiler.start_capture()
# ... your multi-GPU code ...
profiler.stop_capture()
```

### 6. Real-time Tracing (`realtime_tracing.py`)

Lock-free tracing suitable for production:

```python
from realtime_tracing import RealTimeProfiler

profiler = RealTimeProfiler()
profiler.start()
profiler.start_monitoring(interval_ms=50)  # Background GPU monitoring

with profiler.trace_region("my_operation"):
    # Your code here
    pass

profiler.emit_counter("custom_metric", 42.0)
profiler.stop()
```

### 7. Transformer Profiling (`transformers_profiling.py`)

Specialized profiling for transformer models:

```python
from transformers_profiling import TransformerProfiler

profiler = TransformerProfiler()

with profiler.profile():
    output = model(input_ids)

metrics = profiler.get_inference_metrics(batch_size, seq_len)
profiler.print_metrics(metrics)
# Shows: throughput, latency, attention vs FFN time breakdown
```

## Output Files

All examples generate Perfetto-compatible trace files (`*.json`) that can be visualized at:
**https://ui.perfetto.dev/**

Simply drag and drop the generated JSON files to visualize:
- Kernel timelines
- Memory allocations
- Multi-GPU communication
- Custom events and counters

## Key TraceSmith APIs

### Event Capture

```python
import tracesmith as ts

# Create profiler
platform = ts.detect_platform()  # Auto-detect CUDA/ROCm/Metal
profiler = ts.create_profiler(platform)

# Configure
config = ts.ProfilerConfig()
config.buffer_size = 100000
config.capture_kernels = True
config.capture_memcpy = True

# Capture
profiler.initialize(config)
profiler.start_capture()
# ... GPU operations ...
profiler.stop_capture()

events = profiler.get_events()
```

### Timeline Analysis

```python
# Build timeline
timeline = ts.build_timeline(events)
print(f"Duration: {timeline.total_duration / 1e6:.2f} ms")
print(f"GPU Utilization: {timeline.gpu_utilization * 100:.1f}%")
```

### Export

```python
# Perfetto JSON
ts.export_perfetto(events, "trace.json")

# Perfetto Protobuf (smaller files)
ts.export_perfetto(events, "trace.perfetto-trace", use_protobuf=True)

# TraceSmith Binary Format
writer = ts.SBTWriter("trace.sbt")
writer.write_events(events)
writer.finalize()
```

### Memory Profiling

```python
config = ts.MemoryProfilerConfig()
config.snapshot_interval_ms = 10
config.detect_double_free = True

profiler = ts.MemoryProfiler(config)
profiler.start()
# ... allocations/deallocations ...
profiler.stop()

report = profiler.generate_report()
print(report.summary())
```

## Tips

1. **Warmup**: Always run a few warmup iterations before profiling to avoid cold-start effects.

2. **Synchronization**: Call `torch.cuda.synchronize()` before and after profiling for accurate timing.

3. **Buffer Size**: Increase `buffer_size` in `ProfilerConfig` if you're dropping events.

4. **Memory**: Reset peak memory stats before profiling with `torch.cuda.reset_peak_memory_stats()`.

5. **Batch Size**: Profile with different batch sizes to find optimal throughput.

## License

TraceSmith is licensed under [LICENSE](../../LICENSE).
