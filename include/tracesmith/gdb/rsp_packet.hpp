/**
 * @file rsp_packet.hpp
 * @brief GDB Remote Serial Protocol (RSP) packet parser and encoder
 * @version 0.10.0
 * 
 * RSP packet format: $<data>#<checksum>
 * Where checksum is 2-digit hex sum of data bytes mod 256.
 */

#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace tracesmith {
namespace gdb {

/// RSP packet types
enum class RSPPacketType {
    // Basic responses
    Unknown,
    Ack,                // '+'
    Nack,               // '-'
    Interrupt,          // '\x03' (Ctrl-C)
    
    // Standard GDB commands
    ReadRegisters,      // 'g'
    WriteRegisters,     // 'G'
    ReadMemory,         // 'm'
    WriteMemory,        // 'M'
    BinaryWrite,        // 'X'
    Continue,           // 'c'
    ContinueSignal,     // 'C'
    Step,               // 's'
    StepSignal,         // 'S'
    Kill,               // 'k'
    Detach,             // 'D'
    
    // Breakpoint commands
    InsertBreakpoint,   // 'Z'
    RemoveBreakpoint,   // 'z'
    
    // Query commands
    Query,              // 'q'
    QuerySet,           // 'Q'
    
    // Extended commands
    ExtendedMode,       // '!'
    RestartReason,      // '?'
    ThreadAlive,        // 'T'
    SetThread,          // 'H'
    
    // vCommands
    VCommand            // 'v'
};

/// RSP breakpoint type codes
enum class RSPBreakpointType : int {
    Software = 0,       // Software breakpoint (int3)
    Hardware = 1,       // Hardware breakpoint
    WriteWatch = 2,     // Write watchpoint
    ReadWatch = 3,      // Read watchpoint
    AccessWatch = 4     // Access watchpoint
};

/**
 * RSP packet parser and encoder
 * 
 * Handles the GDB RSP packet format including:
 * - Checksum calculation and verification
 * - Hex encoding/decoding
 * - Binary escape sequences
 * - Standard response formatting
 */
class RSPPacket {
public:
    RSPPacket() = default;
    explicit RSPPacket(const std::string& data);
    
    // ============================================================
    // Packet encoding/decoding
    // ============================================================
    
    /// Encode data into RSP packet format: $<data>#<checksum>
    static std::string encode(const std::string& data);
    
    /// Decode RSP packet, returns nullopt if invalid
    static std::optional<std::string> decode(const std::string& packet);
    
    /// Calculate checksum for data (sum of bytes mod 256)
    static uint8_t checksum(const std::string& data);
    
    /// Parse packet type from decoded data
    static RSPPacketType parseType(const std::string& data);
    
    // ============================================================
    // Standard responses
    // ============================================================
    
    /// Encode "OK" response
    static std::string ok();
    
    /// Encode error response "E<code>"
    static std::string error(int code);
    
    /// Encode empty response (unsupported command)
    static std::string empty();
    
    /// Encode stop reply "S<signal>"
    static std::string stopReply(int signal);
    
    /// Encode stop reply with thread "T<signal>thread:<tid>;"
    static std::string stopReplyThread(int signal, pid_t tid);
    
    /// Encode exit reply "W<code>"
    static std::string exitReply(int code);
    
    // ============================================================
    // Hex conversion
    // ============================================================
    
    /// Convert bytes to hex string
    static std::string toHex(const std::vector<uint8_t>& data);
    
    /// Convert string to hex
    static std::string toHex(const std::string& str);
    
    /// Convert uint64 to hex (little-endian byte order)
    static std::string toHex(uint64_t value, int width = 0);
    
    /// Convert hex string to bytes
    static std::vector<uint8_t> fromHex(const std::string& hex);
    
    /// Convert hex string to uint64
    static uint64_t hexToUint64(const std::string& hex);
    
    // ============================================================
    // Binary escape
    // ============================================================
    
    /// Escape binary data for RSP transmission
    /// Characters $, #, }, * are escaped as } followed by char XOR 0x20
    static std::string escapeBinary(const std::vector<uint8_t>& data);
    
    /// Unescape binary data received from RSP
    static std::vector<uint8_t> unescapeBinary(const std::string& data);

private:
    std::string data_;
};

/**
 * RSP query parser
 * 
 * Parses query commands like:
 * - qSupported
 * - qXfer:features:read:target.xml:0,1000
 * - qfThreadInfo
 */
struct RSPQuery {
    std::string name;
    std::vector<std::string> args;
    
    /// Parse query string (without leading 'q' or 'Q')
    static RSPQuery parse(const std::string& query);
};

} // namespace gdb
} // namespace tracesmith
