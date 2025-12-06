# TraceSmith Python Examples

This directory contains comprehensive examples demonstrating how to use TraceSmith for GPU profiling with Python and deep learning frameworks.

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
# Basic usage
python examples/basic_usage.py

# Kernel timing statistics
python examples/kernel_timing_stats.py

# PyTorch profiling
python examples/pytorch_profiling.py
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
