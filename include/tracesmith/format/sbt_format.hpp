#pragma once

#include "tracesmith/common/types.hpp"
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <cstring>

namespace tracesmith {

/// SBT file format constants
namespace sbt {
    constexpr char MAGIC[4] = {'S', 'B', 'T', '\0'};
    constexpr uint16_t FORMAT_VERSION_MAJOR = 0;
    constexpr uint16_t FORMAT_VERSION_MINOR = 1;
    
    // Section types
    enum class SectionType : uint8_t {
        Header = 0,
        Metadata = 1,
        StringTable = 2,
        DeviceInfo = 3,
        Events = 4,
        CallStacks = 5,
        EndOfFile = 255
    };
    
    // Compression types
    enum class Compression : uint8_t {
        None = 0,
        LZ4 = 1,
        Zstd = 2
    };
    
    // Flags
    constexpr uint32_t FLAG_LITTLE_ENDIAN = 0x01;
    constexpr uint32_t FLAG_HAS_CALLSTACKS = 0x02;
    constexpr uint32_t FLAG_COMPRESSED = 0x04;
}

#pragma pack(push, 1)
/// SBT file header (fixed size: 64 bytes)
struct SBTHeader {
    char magic[4];              // "SBT\0"
    uint16_t version_major;     // Format version major
    uint16_t version_minor;     // Format version minor
    uint32_t flags;             // Feature flags
    uint32_t header_size;       // Size of this header
    uint64_t metadata_offset;   // Offset to metadata section
    uint64_t string_table_offset;
    uint64_t device_info_offset;
    uint64_t events_offset;
    uint64_t callstacks_offset;
    uint64_t event_count;       // Total number of events
    uint8_t compression;        // Compression type
    uint8_t reserved[7];        // Reserved for future use
    
    SBTHeader() {
        std::memcpy(magic, sbt::MAGIC, 4);
        version_major = sbt::FORMAT_VERSION_MAJOR;
        version_minor = sbt::FORMAT_VERSION_MINOR;
        flags = sbt::FLAG_LITTLE_ENDIAN;
        header_size = sizeof(SBTHeader);
        metadata_offset = 0;
        string_table_offset = 0;
        device_info_offset = 0;
        events_offset = 0;
        callstacks_offset = 0;
        event_count = 0;
        compression = static_cast<uint8_t>(sbt::Compression::None);
        std::memset(reserved, 0, sizeof(reserved));
    }
    
    bool isValid() const {
        return std::memcmp(magic, sbt::MAGIC, 4) == 0;
    }
};
#pragma pack(pop)

/// Result type for SBT operations
struct SBTResult {
    bool success;
    std::string error_message;
    
    SBTResult() : success(true) {}
    SBTResult(bool ok) : success(ok) {}
    SBTResult(const std::string& error) : success(false), error_message(error) {}
    
    operator bool() const { return success; }
};

/**
 * SBT file writer for streaming trace data to disk.
 * 
 * Usage:
 *   SBTWriter writer("trace.sbt");
 *   writer.writeMetadata(metadata);
 *   writer.writeEvent(event1);
 *   writer.writeEvent(event2);
 *   writer.finalize();
 */
class SBTWriter {
public:
    explicit SBTWriter(const std::string& filename);
    ~SBTWriter();
    
    /// Check if the writer is ready
    bool isOpen() const { return file_.is_open(); }
    
    /// Write trace metadata
    SBTResult writeMetadata(const TraceMetadata& metadata);
    
    /// Write device information
    SBTResult writeDeviceInfo(const std::vector<DeviceInfo>& devices);
    
    /// Write a single event
    SBTResult writeEvent(const TraceEvent& event);
    
    /// Write multiple events
    SBTResult writeEvents(const std::vector<TraceEvent>& events);
    
    /// Finalize the file (writes header and string table)
    SBTResult finalize();
    
    /// Get the number of events written
    uint64_t eventCount() const { return event_count_; }
    
    /// Get the current file size
    uint64_t fileSize() const;

private:
    std::ofstream file_;
    std::string filename_;
    SBTHeader header_;
    
    // String table for deduplication
    std::unordered_map<std::string, uint32_t> string_table_;
    std::vector<std::string> string_list_;
    
    // Tracking
    uint64_t event_count_;
    uint64_t events_start_offset_;
    Timestamp first_timestamp_;
    Timestamp last_timestamp_;
    bool finalized_;
    
    // Internal methods
    uint32_t internString(const std::string& str);
    void writeVarInt(uint64_t value);
    void writeString(const std::string& str);
    void writeEventCompact(const TraceEvent& event);
};

/**
 * SBT file reader for loading trace data.
 * 
 * Usage:
 *   SBTReader reader("trace.sbt");
 *   auto record = reader.readAll();
 */
class SBTReader {
public:
    explicit SBTReader(const std::string& filename);
    ~SBTReader();
    
    /// Check if the reader is ready
    bool isOpen() const { return file_.is_open(); }
    
    /// Check if the file is valid SBT format
    bool isValid() const { return header_.isValid(); }
    
    /// Get the file header
    const SBTHeader& header() const { return header_; }
    
    /// Read all data into a TraceRecord
    SBTResult readAll(TraceRecord& record);
    
    /// Read only metadata
    SBTResult readMetadata(TraceMetadata& metadata);
    
    /// Read events in batches (for large files)
    SBTResult readEvents(std::vector<TraceEvent>& events, 
                         size_t offset, size_t count);
    
    /// Get the total number of events
    uint64_t eventCount() const { return header_.event_count; }

private:
    std::ifstream file_;
    std::string filename_;
    SBTHeader header_;
    std::vector<std::string> string_table_;
    bool header_read_;
    
    // Internal methods
    uint64_t readVarInt();
    std::string readString();
    bool readStringTable();
    TraceEvent readEventCompact();
};

} // namespace tracesmith
