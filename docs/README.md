# TraceSmith Documentation

Complete documentation for the TraceSmith GPU Profiling & Replay System.

## 📚 Documentation Index

### Getting Started
- **[Getting Started Guide](getting_started.md)** - Quick start guide for building and using TraceSmith

### Project Documentation
- **[Planning Document](PLANNING.md)** - Project planning, goals, and architecture
- **[Changelog](../CHANGELOG.md)** - Version history and changes

### Feature Documentation
- **[Cluster Profiling](CLUSTER_PROFILING.md)** - Multi-GPU and cluster profiling implementation plan (v0.7.0+)
- **[MACA Profiling](MACA_PROFILING.md)** - MetaX GPU profiling guide with MCPTI backend

### Reference
- **[GPU Profiling Projects](gpu_profiling_callstack_opensource.md)** - Survey of related open source projects

## 🚀 Quick Links

### For Users
1. Start with [Getting Started Guide](getting_started.md)
2. Read [Planning Document](PLANNING.md) for architecture overview
3. Check [Changelog](../CHANGELOG.md) for recent changes

### For Developers
1. Review [Planning Document](PLANNING.md) for design goals
2. Study [Cluster Profiling](CLUSTER_PROFILING.md) for multi-GPU implementation
3. Check source code in `src/` directories

### For Contributors
1. Read the main [README.md](../README.md) for project overview
2. Review [Changelog](../CHANGELOG.md) to see what's new
3. Check open issues on GitHub

## 📖 Documentation Structure

```
docs/
├── README.md                         # This file
├── getting_started.md                # Quick start guide
├── PLANNING.md                       # Project planning & goals
├── CLUSTER_PROFILING.md              # Multi-GPU cluster profiling plan
├── MACA_PROFILING.md                 # MetaX GPU profiling guide
├── gpu_profiling_callstack_opensource.md  # Related projects survey
└── CHANGELOG.md                      # Local changelog (deprecated, use root)
```

## 🎯 Key Features

### Core Profiling
- **SBT Binary Format** - Custom trace format optimized for GPU events
- **Ring Buffer** - Lock-free SPSC circular buffer for event capture
- **Call Stack Capture** - Cross-platform stack unwinding (libunwind)
- **Memory Profiler** - GPU memory tracking and leak detection

### State Analysis
- **GPU State Machine** - Multi-stream GPU execution modeling
- **Instruction Stream** - Dependency graph construction
- **Timeline Builder** - Event timeline construction and visualization
- **Timeline Viewer** - ASCII art timeline rendering

### Export & Integration
- **Perfetto JSON** - Chrome tracing format export
- **Perfetto Protobuf** - Native protobuf export (85% smaller files)
- **XRay Import** - LLVM XRay trace import

### Replay Engine
- **Replay Engine** - Deterministic GPU execution replay
- **Determinism Checker** - Verify replay correctness
- **Stream Scheduler** - Multi-stream operation scheduling
- **Frame Capture** - RenderDoc-inspired GPU frame capture

### Multi-GPU (v0.7.0+)
- **Multi-GPU Profiler** - Profile multiple GPUs simultaneously
- **GPU Topology** - NVLink/NVSwitch topology discovery
- **Time Sync** - Cross-GPU/node time synchronization
- **NCCL Tracker** - Collective operation tracking

### Platform Support
- **CUDA/CUPTI** - NVIDIA GPU profiling
- **Metal** - Apple GPU profiling (M1/M2/M3)
- **MACA/MCPTI** - MetaX GPU profiling (C500, C550)
- **ROCm** - AMD GPU profiling (planned)
- **BPF** - Linux kernel-level tracing

## 📊 Current Status

| Metric | Value |
|--------|-------|
| **Version** | 0.8.0 |
| **C++ Standard** | C++17 |
| **Python** | 3.9 - 3.12 |
| **Platforms** | Linux, macOS, Windows |
| **GPU Backends** | CUDA, Metal, MACA, (ROCm planned) |

## 🛠️ Build Requirements

### Required
- CMake 3.16+
- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- Python 3.9+ (for Python bindings)
- pybind11 2.11+

### Optional
- CUDA Toolkit 11.0+ (for NVIDIA GPU profiling)
- libunwind (for stack capture on Linux)
- Ninja (for faster builds)

## 🔗 External Resources

- **Repository**: https://github.com/chenxingqiang/TraceSmith
- **PyPI**: https://pypi.org/project/tracesmith/
- **Perfetto UI**: https://ui.perfetto.dev
- **NVIDIA CUPTI**: https://developer.nvidia.com/cupti
- **Apple Metal**: https://developer.apple.com/metal/

## 📝 Contributing

When adding documentation:
1. Place new docs in this `docs/` directory
2. Update this README.md index
3. Follow existing document structure
4. Include code examples where applicable
5. Test all instructions before committing

## 📧 Contact

For questions or issues, please open a GitHub issue at:
https://github.com/chenxingqiang/TraceSmith/issues
