#include "common/net/packet.h"

#include <arpa/inet.h>

#include <cstring>

namespace common::net {

namespace {

std::uint64_t HostToNetwork64(std::uint64_t value) {
    const auto high = htonl(static_cast<std::uint32_t>(value >> 32U));
    const auto low = htonl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    return (static_cast<std::uint64_t>(low) << 32U) | high;
}

std::uint64_t NetworkToHost64(std::uint64_t value) {
    const auto high = ntohl(static_cast<std::uint32_t>(value >> 32U));
    const auto low = ntohl(static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    return (static_cast<std::uint64_t>(low) << 32U) | high;
}

}  // namespace

std::string EncodePacket(const PacketHeader& header, std::string_view body) {
    PacketHeader network_header = header;
    network_header.magic = htonl(network_header.magic);
    network_header.version = htonl(network_header.version);
    network_header.msg_id = htonl(network_header.msg_id);
    network_header.body_len = htonl(network_header.body_len);
    network_header.request_id = HostToNetwork64(network_header.request_id);

    std::string encoded;
    encoded.resize(kPacketHeaderSize + body.size());
    std::memcpy(encoded.data(), &network_header, kPacketHeaderSize);
    std::memcpy(encoded.data() + kPacketHeaderSize, body.data(), body.size());
    return encoded;
}

bool TryExtractPacket(std::string& buffer, Packet* packet) {
    if (packet == nullptr || buffer.size() < kPacketHeaderSize) {
        return false;
    }

    PacketHeader network_header{};
    std::memcpy(&network_header, buffer.data(), kPacketHeaderSize);

    PacketHeader host_header;
    host_header.magic = ntohl(network_header.magic);
    host_header.version = ntohl(network_header.version);
    host_header.msg_id = ntohl(network_header.msg_id);
    host_header.body_len = ntohl(network_header.body_len);
    host_header.request_id = NetworkToHost64(network_header.request_id);

    if (host_header.magic != PacketHeader{}.magic) {
        buffer.clear();
        return false;
    }

    const auto total_size = kPacketHeaderSize + static_cast<std::size_t>(host_header.body_len);
    if (buffer.size() < total_size) {
        return false;
    }

    packet->header = host_header;
    packet->body = buffer.substr(kPacketHeaderSize, host_header.body_len);
    buffer.erase(0, total_size);
    return true;
}

}  // namespace common::net
