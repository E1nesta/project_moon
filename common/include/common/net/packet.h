#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace common::net {

struct PacketHeader {
    std::uint32_t magic = 0x4D47424B;
    std::uint32_t version = 1;
    std::uint32_t msg_id = 0;
    std::uint32_t body_len = 0;
    std::uint64_t request_id = 0;
};

struct Packet {
    PacketHeader header;
    std::string body;
};

constexpr std::size_t kPacketHeaderSize = 24;

[[nodiscard]] std::string EncodePacket(const PacketHeader& header, std::string_view body);
[[nodiscard]] bool TryExtractPacket(std::string& buffer, Packet* packet);

}  // namespace common::net
