# Perfetto SDK

**Version**: v50.1  
**License**: Apache 2.0  
**Source**: https://github.com/google/perfetto

## Files

- `perfetto.h` (7.4MB, 178K lines) - Amalgamated header
- `perfetto.cc` (2.7MB, 69K lines) - Amalgamated implementation

## Usage

TraceSmith can optionally use the Perfetto SDK for native protobuf trace export. This provides:

- **3-5x smaller file sizes** compared to JSON export
- **Real-time tracing** support
- **SQL query** capabilities over traces
- **Industry-standard format** compatible with Perfetto UI

## Build Configuration

To enable Perfetto SDK support:

```bash
cmake -DTRACESMITH_USE_PERFETTO_SDK=ON ..
make
```

To disable (default):

```bash
cmake -DTRACESMITH_USE_PERFETTO_SDK=OFF ..
make
```

## Requirements

- **C++ Standard**: C++11 minimum (C++17 for some advanced features)
- **Compiler**: GCC 7+, Clang 8+, MSVC 2019+
- **Platforms**: Linux, macOS, Windows, Android, iOS

## Why Amalgamated Files?

The Perfetto SDK is provided as amalgamated (single-file) headers to simplify integration. This means:

- No complex build system dependencies
- Faster compilation (single translation unit)
- Easy to integrate into existing projects
- No need for protobuf compiler

## License Compatibility

Perfetto uses Apache 2.0 license, which is compatible with TraceSmith's MIT license. See [LICENSE](../../LICENSE) for details.

## Update Instructions

To update to a newer Perfetto version:

1. Clone Perfetto repository:
   ```bash
   git clone --depth 1 --branch vX.Y https://github.com/google/perfetto.git /tmp/perfetto-sdk
   ```

2. Copy amalgamated files:
   ```bash
   cp /tmp/perfetto-sdk/sdk/perfetto.h third_party/perfetto/
   cp /tmp/perfetto-sdk/sdk/perfetto.cc third_party/perfetto/
   ```

3. Update this README with new version number

4. Test compilation:
   ```bash
   mkdir build && cd build
   cmake -DTRACESMITH_USE_PERFETTO_SDK=ON ..
   make
   ```

## More Information

- [Perfetto Documentation](https://perfetto.dev/docs/)
- [TraceSmith Perfetto Integration Guide](../../docs/PERFETTO_INTEGRATION.md)
- [TraceSmith Perfetto Phase 2 Plan](../../docs/PERFETTO_PHASE2.md)
