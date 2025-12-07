# MetaX MACA GPU Profiling Guide

This guide explains how to use TraceSmith for profiling MetaX GPUs (C500, C550, etc.) using the MCPTI (MACA Profiling Tools Interface).

## Overview

TraceSmith provides native support for MetaX GPUs through the MCPTI backend, which offers an API compatible with NVIDIA's CUPTI. This allows for:

- Kernel execution profiling
- Memory transfer tracking (H2D, D2H, D2D)
- Memory set operations
- Synchronization events
- Low overhead profiling (< 2%)

## Requirements

### Hardware

| GPU | Memory | Status |
|-----|--------|--------|
| MetaX C500 | 64 GB | ✅ Verified |
| MetaX C550 | - | 🔜 Planned |

### Software

- **MACA SDK**: Version 3.0.0 or later
- **MACA Driver**: Version 3.0.11 or later
- **CMake**: 3.16 or later
- **C++ Compiler**: GCC 9+ or Clang 10+

### Verify Installation

```bash
# Check GPU status
mx-smi

# Expected output:
# +---------------------------------------------------------------------------------+
# | MX-SMI 2.2.6                        Kernel Mode Driver Version: 3.0.11          |
# | MACA Version: 3.0.0.8               BIOS Version: 1.27.5.0                      |
# |------------------------------------+---------------------+----------------------+
# | GPU     NAME         Persistence-M | Bus-id              | GPU-Util      sGPU-M |
# | 0       MetaX C500             Off | 0000:0e:00.0        | 0%            Native |
# | 35C     59W / 350W              P0 | 858/65536 MiB       | Available            |
# +------------------------------------+---------------------+----------------------+

# Check MACA SDK
ls /opt/maca-3.0.0/include/mcpti/
# Should show: mcpti.h, mcpti_type.h, etc.
```

## Building TraceSmith with MACA Support

### CMake Configuration

```bash
git clone https://github.com/chenxingqiang/TraceSmith.git
cd TraceSmith
mkdir build && cd build

# Configure with MACA support
cmake .. -DTRACESMITH_ENABLE_MACA=ON

# If MACA SDK is in non-standard location:
cmake .. -DTRACESMITH_ENABLE_MACA=ON -DMACA_ROOT=/path/to/maca

# Build
make -j$(nproc)
```

### Verify Build

```bash
# Check configuration
cmake .. -DTRACESMITH_ENABLE_MACA=ON 2>&1 | grep -E "MACA|MetaX"

# Expected output:
# -- MACA SDK found at: /opt/maca-3.0.0
# --   MCPTI library: /opt/maca-3.0.0/lib/libmcpti.so
# --   MCRuntime library: /opt/maca-3.0.0/lib/libmcruntime.so
# --   MACA support:   ON
```

## Usage

### C++ API

#### Basic Profiling

```cpp
#include <tracesmith/tracesmith.hpp>
#include <mcr/mc_runtime_api.h>  // For MACA runtime

using namespace tracesmith;

int main() {
    // Check availability
    if (!isMACAAvailable()) {
        std::cerr << "MetaX GPU not available" << std::endl;
        return 1;
    }
    
    std::cout << "Device count: " << getMACADeviceCount() << std::endl;
    std::cout << "Driver version: " << getMACADriverVersion() << std::endl;
    
    // Create profiler
    auto profiler = createProfiler(PlatformType::MACA);
    if (!profiler) {
        std::cerr << "Failed to create profiler" << std::endl;
        return 1;
    }
    
    // Get device info
    auto devices = profiler->getDeviceInfo();
    for (const auto& dev : devices) {
        std::cout << "Device " << dev.device_id << ": " << dev.name << std::endl;
        std::cout << "  Memory: " << (dev.total_memory / 1e9) << " GB" << std::endl;
        std::cout << "  Compute: " << dev.compute_major << "." << dev.compute_minor << std::endl;
    }
    
    // Configure
    ProfilerConfig config;
    config.capture_kernels = true;
    config.capture_memcpy = true;
    config.capture_memset = true;
    config.capture_sync = true;
    
    profiler->initialize(config);
    
    // Start capture
    profiler->startCapture();
    
    // === Your GPU code here ===
    float* d_data;
    mcMalloc(&d_data, 1024 * sizeof(float));
    mcMemset(d_data, 0, 1024 * sizeof(float));
    mcFree(d_data);
    // ==========================
    
    // Stop capture
    profiler->stopCapture();
    
    // Get events
    std::vector<TraceEvent> events;
    profiler->getEvents(events);
    
    std::cout << "Captured " << events.size() << " events" << std::endl;
    
    // Save to file
    SBTWriter writer("trace.sbt");
    writer.writeEvents(events);
    writer.finalize();
    
    // Export to Perfetto
    PerfettoExporter exporter;
    exporter.exportToFile(events, "trace.json");
    
    profiler->finalize();
    return 0;
}
```

#### Event Callback (Real-time Processing)

```cpp
#include <tracesmith/tracesmith.hpp>

void onEvent(const TraceEvent& event) {
    std::cout << "[" << eventTypeToString(event.type) << "] " 
              << event.name << std::endl;
}

int main() {
    auto profiler = createProfiler(PlatformType::MACA);
    
    // Register callback for real-time event processing
    profiler->setEventCallback(onEvent);
    
    ProfilerConfig config;
    profiler->initialize(config);
    profiler->startCapture();
    
    // ... GPU code ...
    
    profiler->stopCapture();
    return 0;
}
```

### Python API

#### Basic Usage

```python
import tracesmith as ts

def main():
    # Check availability
    if not ts.is_maca_available():
        print("MetaX GPU not available")
        return
    
    print(f"Device count: {ts.get_maca_device_count()}")
    print(f"Driver version: {ts.get_maca_driver_version()}")
    
    # Create profiler
    profiler = ts.create_profiler(ts.PlatformType.MACA)
    
    # Get device info
    devices = profiler.get_device_info()
    for dev in devices:
        print(f"Device {dev.device_id}: {dev.name}")
        print(f"  Memory: {dev.total_memory / 1e9:.2f} GB")
    
    # Configure
    config = ts.ProfilerConfig()
    config.capture_kernels = True
    config.capture_memcpy = True
    profiler.initialize(config)
    
    # Capture
    profiler.start_capture()
    
    # ... Your GPU code here ...
    
    profiler.stop_capture()
    
    # Get events
    events = profiler.get_events()
    print(f"Captured {len(events)} events")
    
    # Save
    writer = ts.SBTWriter("trace.sbt")
    writer.write_events(events)
    writer.finalize()

if __name__ == "__main__":
    main()
```

#### With PyTorch (if available)

```python
import tracesmith as ts

try:
    import torch
    # MetaX may use 'cuda' device with HIP-compatible backend
    device = torch.device('cuda:0')
except ImportError:
    print("PyTorch not available")
    exit(1)

# Create profiler
profiler = ts.create_profiler(ts.PlatformType.MACA)
config = ts.ProfilerConfig()
profiler.initialize(config)

# Profile PyTorch operations
profiler.start_capture()

x = torch.randn(1000, 1000, device=device)
y = torch.randn(1000, 1000, device=device)
z = torch.mm(x, y)
torch.cuda.synchronize()

profiler.stop_capture()

events = profiler.get_events()
print(f"Captured {len(events)} events")
```

### CLI Usage

```bash
# Record a trace
./bin/tracesmith record -o trace.sbt -- ./your_maca_program

# Profile a command
./bin/tracesmith profile -o trace.sbt -- ./your_maca_program

# View trace info
./bin/tracesmith info trace.sbt

# Convert to Perfetto JSON
./bin/tracesmith convert trace.sbt -o trace.json
```

## Captured Event Types

| Event Type | MCPTI Activity | Description |
|------------|----------------|-------------|
| `KernelLaunch` | `MCPTI_ACTIVITY_KIND_KERNEL` | Kernel start with grid/block dims |
| `KernelComplete` | `MCPTI_ACTIVITY_KIND_KERNEL` | Kernel end with duration |
| `MemcpyH2D` | `MCPTI_ACTIVITY_KIND_MEMCPY` | Host to Device transfer |
| `MemcpyD2H` | `MCPTI_ACTIVITY_KIND_MEMCPY` | Device to Host transfer |
| `MemcpyD2D` | `MCPTI_ACTIVITY_KIND_MEMCPY` | Device to Device transfer |
| `MemsetDevice` | `MCPTI_ACTIVITY_KIND_MEMSET` | Device memory set |
| `StreamSync` | `MCPTI_ACTIVITY_KIND_SYNCHRONIZATION` | Stream synchronization |
| `DeviceSync` | `MCPTI_ACTIVITY_KIND_SYNCHRONIZATION` | Device synchronization |

## Event Metadata

Each captured event includes:

```cpp
struct TraceEvent {
    EventType type;           // Event type
    Timestamp timestamp;      // Start time (ns)
    uint64_t duration;        // Duration (ns)
    uint64_t correlation_id;  // Correlation ID
    uint32_t device_id;       // GPU device ID
    uint32_t stream_id;       // Stream ID
    std::string name;         // Kernel/operation name
    
    // Additional metadata (in metadata map)
    // Kernels: gridDimX/Y/Z, blockDimX/Y/Z, sharedMemory, registers
    // Memcpy: bytes, bandwidth_gbps, srcKind, dstKind
    // Memset: bytes, value
};
```

## Performance Benchmarks

Tested on MetaX C500 (64 GB, 104 CUs):

### Memory Bandwidth

| Operation | 1 MB | 16 MB | 64 MB | 256 MB |
|-----------|------|-------|-------|--------|
| H2D | 11.0 GB/s | 7.8 GB/s | 8.2 GB/s | 8.8 GB/s |
| D2H | 5.5 GB/s | 8.3 GB/s | 8.6 GB/s | 8.7 GB/s |
| D2D | 47.0 GB/s | 252.5 GB/s | 473.3 GB/s | **599.2 GB/s** |

### Profiling Overhead

| Metric | Value |
|--------|-------|
| Without profiling | 7.73 ms |
| With MCPTI profiling | 7.58 ms |
| **Overhead** | **< 2%** |

## Troubleshooting

### Common Issues

#### 1. "MetaX GPU not detected"

```bash
# Check driver
mx-smi
lsmod | grep mx

# Reload driver if needed
sudo modprobe mx_driver
```

#### 2. "MACA SDK not found"

```bash
# Verify SDK installation
ls /opt/maca-3.0.0/include/mcpti/

# Set MACA_ROOT if in non-standard location
export MACA_ROOT=/path/to/maca
cmake .. -DTRACESMITH_ENABLE_MACA=ON -DMACA_ROOT=$MACA_ROOT
```

#### 3. "Failed to create profiler"

```bash
# Check permissions
sudo chmod 666 /dev/mx*

# Or run with sudo
sudo ./bin/metax_example
```

#### 4. "mcpti library not found"

```bash
# Add library path
export LD_LIBRARY_PATH=/opt/maca-3.0.0/lib:$LD_LIBRARY_PATH

# Or set rpath during build
cmake .. -DCMAKE_INSTALL_RPATH="/opt/maca-3.0.0/lib"
```

### Debug Mode

```bash
# Enable verbose output
export MCPTI_DEBUG=1
./bin/metax_example

# Check MCPTI version
./bin/tracesmith --version
```

## Examples

### Run Built-in Examples

```bash
cd build

# Basic profiling example
./bin/metax_example

# Memory bandwidth benchmark
./bin/metax_benchmark

# View generated files
ls -la *.sbt *.json
```

### Python Example

```bash
cd python/examples
python metax_profiling.py --mode auto --output trace
```

## API Reference

### C++ Functions

```cpp
namespace tracesmith {
    // Platform detection
    bool isMACAAvailable();
    int getMACADriverVersion();
    int getMACADeviceCount();
    
    // Profiler creation
    std::unique_ptr<IPlatformProfiler> createProfiler(PlatformType::MACA);
}
```

### Python Functions

```python
import tracesmith as ts

# Platform detection
ts.is_maca_available() -> bool
ts.get_maca_driver_version() -> int
ts.get_maca_device_count() -> int

# Profiler creation
ts.create_profiler(ts.PlatformType.MACA) -> Profiler
```

## See Also

- [README.md](../README.md) - Main documentation
- [PLANNING.md](PLANNING.md) - Project roadmap
- [CLUSTER_PROFILING.md](CLUSTER_PROFILING.md) - Multi-GPU profiling

## License

Apache 2.0 - See [LICENSE](../LICENSE)
