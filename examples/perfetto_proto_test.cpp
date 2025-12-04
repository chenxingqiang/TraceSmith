#include "tracesmith/common/types.hpp"
#include "tracesmith/state/perfetto_proto_exporter.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>

using namespace tracesmith;

int main() {
    std::cout << "Perfetto ProtoZero Export Test\n";
    std::cout << "================================\n\n";

    // Check if SDK is available
    std::cout << "Perfetto SDK available: "
              << (PerfettoProtoExporter::isSDKAvailable() ? "YES" : "NO") << "\n\n";

    // Create sample GPU events
    std::vector<TraceEvent> events;

    // Event 1: Kernel launch
    TraceEvent kernel_launch(EventType::KernelLaunch, 1000000);  // 1ms
    kernel_launch.name = "vectorAdd";
    kernel_launch.device_id = 0;
    kernel_launch.stream_id = 1;
    kernel_launch.correlation_id = 42;
    kernel_launch.thread_id = 12345;
    kernel_launch.duration = 500000;  // 500µs

    KernelParams kp;
    kp.grid_x = 256;
    kp.grid_y = 1;
    kp.grid_z = 1;
    kp.block_x = 256;
    kp.block_y = 1;
    kp.block_z = 1;
    kp.shared_mem_bytes = 0;
    kp.registers_per_thread = 32;
    kernel_launch.kernel_params = kp;

    // Add metadata
    kernel_launch.metadata["operator"] = "aten::add";
    kernel_launch.metadata["device"] = "cuda:0";

    events.push_back(kernel_launch);

    // Event 2: Memory copy
    TraceEvent memcpy(EventType::MemcpyH2D, 2000000);  // 2ms
    memcpy.name = "memcpy_H2D";
    memcpy.device_id = 0;
    memcpy.stream_id = 0;
    memcpy.correlation_id = 43;
    memcpy.duration = 100000;  // 100µs

    MemoryParams mp;
    mp.src_address = 0x7fff0000;
    mp.dst_address = 0xb0000000;
    mp.size_bytes = 4096 * 4;  // 16KB
    memcpy.memory_params = mp;

    events.push_back(memcpy);

    // Event 3: Memory allocation
    TraceEvent mem_alloc(EventType::MemAlloc, 3000000);  // 3ms
    mem_alloc.name = "cudaMalloc";
    mem_alloc.device_id = 0;
    mem_alloc.stream_id = 0;
    mem_alloc.duration = 0;  // Instant event

    MemoryParams mp2;
    mp2.dst_address = 0xb0000000;
    mp2.size_bytes = 1024 * 1024;  // 1MB
    mem_alloc.memory_params = mp2;

    events.push_back(mem_alloc);

    // Event 4: Stream sync
    TraceEvent sync(EventType::StreamSync, 4000000);  // 4ms
    sync.name = "cudaStreamSynchronize";
    sync.device_id = 0;
    sync.stream_id = 1;
    sync.duration = 200000;  // 200µs

    events.push_back(sync);

    std::cout << "Created " << events.size() << " test events\n";

#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    // Test protobuf export
    std::cout << "\n--- Protobuf Export Test ---\n";

    try {
        PerfettoProtoExporter proto_exporter(PerfettoProtoExporter::Format::PROTOBUF);

        // Export to .perfetto-trace file
        std::string proto_file = "trace_proto.perfetto-trace";
        bool success = proto_exporter.exportToFile(events, proto_file);

        if (success) {
            std::cout << "✓ Protobuf export successful: " << proto_file << "\n";

            // Check file size
            std::ifstream file(proto_file, std::ios::binary | std::ios::ate);
            if (file) {
                size_t size = file.tellg();
                std::cout << "  File size: " << size << " bytes\n";
            }
        } else {
            std::cout << "✗ Protobuf export failed\n";
        }

        // Export to memory buffer
        auto proto_data = proto_exporter.exportToProto(events);
        std::cout << "✓ Protobuf in-memory export: " << proto_data.size() << " bytes\n";

    } catch (const std::exception& e) {
        std::cerr << "Exception during protobuf export: " << e.what() << "\n";
    }
#else
    std::cout << "\nProtobuf export not available (SDK disabled)\n";
#endif

    // Test JSON fallback
    std::cout << "\n--- JSON Fallback Test ---\n";

    try {
        PerfettoProtoExporter json_exporter(PerfettoProtoExporter::Format::JSON);

        std::string json_file = "trace_fallback.json";
        bool success = json_exporter.exportToFile(events, json_file);

        if (success) {
            std::cout << "✓ JSON export successful: " << json_file << "\n";

            // Check file size
            std::ifstream file(json_file, std::ios::binary | std::ios::ate);
            if (file) {
                size_t size = file.tellg();
                std::cout << "  File size: " << size << " bytes\n";
            }
        } else {
            std::cout << "✗ JSON export failed\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception during JSON export: " << e.what() << "\n";
    }

#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    // Compare file sizes
    std::cout << "\n--- File Size Comparison ---\n";

    std::ifstream proto_file("trace_proto.perfetto-trace", std::ios::binary | std::ios::ate);
    std::ifstream json_file("trace_fallback.json", std::ios::binary | std::ios::ate);

    if (proto_file && json_file) {
        size_t proto_size = proto_file.tellg();
        size_t json_size = json_file.tellg();

        double reduction = (1.0 - static_cast<double>(proto_size) / json_size) * 100.0;
        double ratio = static_cast<double>(json_size) / proto_size;

        std::cout << "Protobuf: " << proto_size << " bytes\n";
        std::cout << "JSON:     " << json_size << " bytes\n";
        std::cout << "Reduction: " << std::fixed << std::setprecision(1)
                  << reduction << "%\n";
        std::cout << "Ratio:     " << std::fixed << std::setprecision(2)
                  << ratio << "x smaller\n";
    }
#endif

    std::cout << "\n✅ All tests completed\n";
    std::cout << "\nTo view traces:\n";
    std::cout << "  1. Open https://ui.perfetto.dev\n";
#ifdef TRACESMITH_PERFETTO_SDK_ENABLED
    std::cout << "  2. Drag and drop trace_proto.perfetto-trace\n";
#endif
    std::cout << "  3. Or drag and drop trace_fallback.json\n";

    return 0;
}
