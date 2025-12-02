# TraceSmith Documentation

Complete documentation for the TraceSmith GPU Profiling & Replay System.

## 📚 Documentation Index

### Getting Started
- **[Getting Started Guide](getting_started.md)** - Quick start guide for building and using TraceSmith

### Project Documentation
- **[Project Summary](PROJECT_SUMMARY.md)** - Complete project overview and architecture
- **[Planning Document](PLANNING.md)** - Original project planning and goals (目标规划书)
- **[Test Report](TEST_REPORT.md)** - Comprehensive functionality test results
- **[Changelog](CHANGELOG.md)** - Version history and changes

### Phase Documentation
- **[Phase 2 Summary](PHASE2_SUMMARY.md)** - Call Stack Collection implementation
- **[Phase 3 Summary](PHASE3_SUMMARY.md)** - GPU State Machine & Timeline implementation
- **[Phase 4 Summary](PHASE4_SUMMARY.md)** - Replay Engine implementation
- **[Phase 5 Summary](PHASE5_SUMMARY.md)** - Production Release (CLI, Python bindings, Docker)

### Reference
- **[GPU Profiling Projects](GPU%20Profiling%20与调用栈采集（Callstack）方向的开源项目.md)** - Survey of related open source projects
- **[Integration Recommendations](INTEGRATION_RECOMMENDATIONS.md)** - Strategic recommendations for integrating open source components

## 🚀 Quick Links

### For Users
1. Start with [Getting Started Guide](getting_started.md)
2. Read [Project Summary](PROJECT_SUMMARY.md) for architecture overview
3. Check [Test Report](TEST_REPORT.md) for tested functionality

### For Developers
1. Review [Planning Document](PLANNING.md) for design goals
2. Study [Phase 2](PHASE2_SUMMARY.md) and [Phase 3](PHASE3_SUMMARY.md) for implementation details
3. Check [Changelog](CHANGELOG.md) for recent changes

### For Contributors
1. Read [Project Summary](PROJECT_SUMMARY.md) for codebase structure
2. Review [Test Report](TEST_REPORT.md) to see what's tested
3. Check open issues on GitHub

## 📖 Documentation Structure

```
docs/
├── README.md                    # This file
├── getting_started.md           # Quick start guide
├── PROJECT_SUMMARY.md           # Complete project overview
├── PLANNING.md                  # Original planning document
├── TEST_REPORT.md              # Functionality test results
├── CHANGELOG.md                # Version history
├── PHASE2_SUMMARY.md           # Phase 2 implementation
├── PHASE3_SUMMARY.md           # Phase 3 implementation
├── PHASE4_SUMMARY.md           # Phase 4 implementation (Replay)
├── PHASE5_SUMMARY.md           # Phase 5 implementation (Production)
└── GPU Profiling 与调用栈...   # Related projects survey
```

## 🎯 Key Features Documented

- **SBT Binary Format** - Custom trace format optimized for GPU events
- **Ring Buffer** - Lock-free circular buffer for event capture
- **Call Stack Capture** - Cross-platform stack unwinding
- **GPU State Machine** - Multi-stream GPU execution modeling
- **Timeline Builder** - Event timeline construction and visualization
- **Perfetto Export** - Chrome tracing format export
- **Replay Engine** - Deterministic GPU execution replay
- **CUPTI Integration** - NVIDIA GPU profiling (code complete)
- **Metal Integration** - Apple GPU profiling (tested on M3 Max)

## 📊 Current Status

- **Version**: 0.1.0
- **Completion**: 97%
- **Lines of Code**: ~5,300 (C++ + Python)
- **Test Coverage**: All core functionality tested on macOS
- **Hardware Tested**: Apple M3 Max (Metal)
- **Pending**: NVIDIA GPU testing (CUPTI)

## 🔗 External Resources

- **Repository**: https://github.com/chenxingqiang/TraceSmith
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
