/**
 * @file rsp_packet.cpp
 * @brief Implementation of GDB RSP packet parser/encoder
 */

#include "tracesmith/gdb/rsp_packet.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace tracesmith {
namespace gdb {

// ============================================================
// RSPPacket Implementation
// ============================================================

RSPPacket::RSPPacket(const std::string& data) : data_(data) {}

std::string RSPPacket::encode(const std::string& data) {
    // First escape special characters
    std::string escaped;
    escaped.reserve(data.size() * 2);
    
    for (char c : data) {
        // Characters that need escaping: }, #, $, *
        if (c == '}' || c == '#' || c == '$' || c == '*') {
            escaped += '}';
            escaped += static_cast<char>(c ^ 0x20);
        } else {
            escaped += c;
        }
    }
    
    // Calculate checksum
    uint8_t cs = checksum(escaped);
    
    // Format: $<data>#<checksum>
    std::ostringstream oss;
    oss << '$' << escaped << '#' 
        << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(cs);
    
    return oss.str();
}

std::optional<std::string> RSPPacket::decode(const std::string& packet) {
    // Minimum valid packet: $#00 (4 chars)
    if (packet.size() < 4) {
        return std::nullopt;
    }
    
    // Check start marker
    if (packet[0] != '$') {
        return std::nullopt;
    }
    
    // Find hash marker
    size_t hash_pos = packet.rfind('#');
    if (hash_pos == std::string::npos || hash_pos < 1) {
        return std::nullopt;
    }
    
    // Need at least 2 chars after hash for checksum
    if (packet.size() < hash_pos + 3) {
        return std::nullopt;
    }
    
    // Extract data and checksum
    std::string data = packet.substr(1, hash_pos - 1);
    std::string cs_str = packet.substr(hash_pos + 1, 2);
    
    // Parse expected checksum
    int expected_cs;
    try {
        expected_cs = std::stoi(cs_str, nullptr, 16);
    } catch (...) {
        return std::nullopt;
    }
    
    // Verify checksum
    uint8_t actual_cs = checksum(data);
    if (actual_cs != static_cast<uint8_t>(expected_cs)) {
        return std::nullopt;
    }
    
    // Unescape the data
    std::string unescaped;
    unescaped.reserve(data.size());
    
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '}' && i + 1 < data.size()) {
            unescaped += static_cast<char>(data[i + 1] ^ 0x20);
            ++i;
        } else {
            unescaped += data[i];
        }
    }
    
    return unescaped;
}

uint8_t RSPPacket::checksum(const std::string& data) {
    uint32_t sum = 0;
    for (unsigned char c : data) {
        sum += c;
    }
    return static_cast<uint8_t>(sum & 0xFF);
}

RSPPacketType RSPPacket::parseType(const std::string& data) {
    if (data.empty()) {
        return RSPPacketType::Unknown;
    }
    
    char cmd = data[0];
    
    switch (cmd) {
        case 'g': return RSPPacketType::ReadRegisters;
        case 'G': return RSPPacketType::WriteRegisters;
        case 'm': return RSPPacketType::ReadMemory;
        case 'M': return RSPPacketType::WriteMemory;
        case 'X': return RSPPacketType::BinaryWrite;
        case 'c': return RSPPacketType::Continue;
        case 'C': return RSPPacketType::ContinueSignal;
        case 's': return RSPPacketType::Step;
        case 'S': return RSPPacketType::StepSignal;
        case 'k': return RSPPacketType::Kill;
        case 'D': return RSPPacketType::Detach;
        case 'Z': return RSPPacketType::InsertBreakpoint;
        case 'z': return RSPPacketType::RemoveBreakpoint;
        case 'q': return RSPPacketType::Query;
        case 'Q': return RSPPacketType::QuerySet;
        case 'v': return RSPPacketType::VCommand;
        case '!': return RSPPacketType::ExtendedMode;
        case '?': return RSPPacketType::RestartReason;
        case 'T': return RSPPacketType::ThreadAlive;
        case 'H': return RSPPacketType::SetThread;
        case '+': return RSPPacketType::Ack;
        case '-': return RSPPacketType::Nack;
        case '\x03': return RSPPacketType::Interrupt;
        default: return RSPPacketType::Unknown;
    }
}

// ============================================================
// Standard Responses
// ============================================================

std::string RSPPacket::ok() {
    return encode("OK");
}

std::string RSPPacket::error(int code) {
    std::ostringstream oss;
    oss << 'E' << std::hex << std::setw(2) << std::setfill('0') << (code & 0xFF);
    return encode(oss.str());
}

std::string RSPPacket::empty() {
    return encode("");
}

std::string RSPPacket::stopReply(int signal) {
    std::ostringstream oss;
    oss << 'S' << std::hex << std::setw(2) << std::setfill('0') << (signal & 0xFF);
    return encode(oss.str());
}

std::string RSPPacket::stopReplyThread(int signal, pid_t tid) {
    std::ostringstream oss;
    oss << 'T' << std::hex << std::setw(2) << std::setfill('0') << (signal & 0xFF);
    oss << "thread:" << std::hex << tid << ";";
    return encode(oss.str());
}

std::string RSPPacket::exitReply(int code) {
    std::ostringstream oss;
    oss << 'W' << std::hex << std::setw(2) << std::setfill('0') << (code & 0xFF);
    return encode(oss.str());
}

// ============================================================
// Hex Conversion
// ============================================================

std::string RSPPacket::toHex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (uint8_t byte : data) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

std::string RSPPacket::toHex(const std::string& str) {
    std::vector<uint8_t> data(str.begin(), str.end());
    return toHex(data);
}

std::string RSPPacket::toHex(uint64_t value, int width) {
    // GDB expects little-endian byte order for register values
    std::vector<uint8_t> bytes;
    
    if (width == 0) {
        // Auto-size: use minimum bytes needed
        if (value == 0) {
            bytes.push_back(0);
        } else {
            while (value > 0) {
                bytes.push_back(static_cast<uint8_t>(value & 0xFF));
                value >>= 8;
            }
        }
    } else {
        // Fixed width
        int num_bytes = width / 2;
        for (int i = 0; i < num_bytes; ++i) {
            bytes.push_back(static_cast<uint8_t>(value & 0xFF));
            value >>= 8;
        }
    }
    
    return toHex(bytes);
}

std::vector<uint8_t> RSPPacket::fromHex(const std::string& hex) {
    std::vector<uint8_t> result;
    
    // Handle odd-length strings
    size_t start = (hex.size() % 2 == 1) ? 1 : 0;
    if (start == 1) {
        // Prepend a 0 for odd length
        result.push_back(static_cast<uint8_t>(std::stoi(hex.substr(0, 1), nullptr, 16)));
    }
    
    for (size_t i = start; i + 1 < hex.size(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        try {
            result.push_back(static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16)));
        } catch (...) {
            // Invalid hex, stop parsing
            break;
        }
    }
    
    return result;
}

uint64_t RSPPacket::hexToUint64(const std::string& hex) {
    try {
        return std::stoull(hex, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

// ============================================================
// Binary Escape
// ============================================================

std::string RSPPacket::escapeBinary(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve(data.size() * 2);
    
    for (uint8_t byte : data) {
        // Escape: #, $, }, *
        if (byte == '#' || byte == '$' || byte == '}' || byte == '*') {
            result += '}';
            result += static_cast<char>(byte ^ 0x20);
        } else {
            result += static_cast<char>(byte);
        }
    }
    
    return result;
}

std::vector<uint8_t> RSPPacket::unescapeBinary(const std::string& data) {
    std::vector<uint8_t> result;
    result.reserve(data.size());
    
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '}' && i + 1 < data.size()) {
            result.push_back(static_cast<uint8_t>(data[i + 1] ^ 0x20));
            ++i;
        } else {
            result.push_back(static_cast<uint8_t>(data[i]));
        }
    }
    
    return result;
}

// ============================================================
// RSPQuery Implementation
// ============================================================

RSPQuery RSPQuery::parse(const std::string& query) {
    RSPQuery result;
    
    if (query.empty()) {
        return result;
    }
    
    // Find first colon to separate name from args
    size_t colon_pos = query.find(':');
    
    if (colon_pos == std::string::npos) {
        // No arguments
        result.name = query;
    } else {
        result.name = query.substr(0, colon_pos);
        
        // Split remaining by colons
        std::string remaining = query.substr(colon_pos + 1);
        size_t pos = 0;
        size_t next;
        
        while ((next = remaining.find(':', pos)) != std::string::npos) {
            result.args.push_back(remaining.substr(pos, next - pos));
            pos = next + 1;
        }
        
        // Last argument (or only argument after name)
        if (pos < remaining.size()) {
            result.args.push_back(remaining.substr(pos));
        }
    }
    
    return result;
}

} // namespace gdb
} // namespace tracesmith
