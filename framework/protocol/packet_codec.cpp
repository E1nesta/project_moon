#include "framework/protocol/packet_codec.h"

#include <arpa/inet.h>

#include <cstring>

namespace framework::protocol {

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

std::string EncodePacket(const common::net::Packet& packet) {
    common::net::PacketHeader network_header = packet.header;
    network_header.magic = htonl(network_header.magic);
    network_header.version = htonl(network_header.version);
    network_header.msg_id = htonl(network_header.msg_id);
    network_header.body_len = htonl(network_header.body_len);
    network_header.request_id = HostToNetwork64(network_header.request_id);

    std::string encoded;
    encoded.resize(kPacketHeaderSize + packet.body.size());
    std::memcpy(encoded.data(), &network_header, kPacketHeaderSize);
    std::memcpy(encoded.data() + kPacketHeaderSize, packet.body.data(), packet.body.size());
    return encoded;
}

bool DecodeHeader(std::string_view bytes,
                  common::net::PacketHeader* header,
                  std::string* error_message,
                  std::uint32_t max_body_bytes) {
    if (header == nullptr || bytes.size() != kPacketHeaderSize) {
        if (error_message != nullptr) {
            *error_message = "invalid packet header size";
        }
        return false;
    }

    common::net::PacketHeader network_header{};
    std::memcpy(&network_header, bytes.data(), kPacketHeaderSize);

    common::net::PacketHeader host_header{};
    host_header.magic = ntohl(network_header.magic);
    host_header.version = ntohl(network_header.version);
    host_header.msg_id = ntohl(network_header.msg_id);
    host_header.body_len = ntohl(network_header.body_len);
    host_header.request_id = NetworkToHost64(network_header.request_id);

    if (host_header.magic != common::net::PacketHeader{}.magic) {
        if (error_message != nullptr) {
            *error_message = "invalid packet magic";
        }
        return false;
    }

    if (host_header.body_len > max_body_bytes) {
        if (error_message != nullptr) {
            *error_message = "packet body too large";
        }
        return false;
    }

    *header = host_header;
    return true;
}

bool TryExtractPacket(std::string& buffer, common::net::Packet* packet) {
    if (packet == nullptr || buffer.size() < kPacketHeaderSize) {
        return false;
    }

    common::net::PacketHeader header{};
    if (!DecodeHeader(std::string_view(buffer.data(), kPacketHeaderSize), &header, nullptr, kDefaultMaxPacketBodyBytes)) {
        buffer.clear();
        return false;
    }

    const auto total_size = kPacketHeaderSize + static_cast<std::size_t>(header.body_len);
    if (buffer.size() < total_size) {
        return false;
    }

    packet->header = header;
    packet->body = buffer.substr(kPacketHeaderSize, header.body_len);
    buffer.erase(0, total_size);
    return true;
}

}  // namespace framework::protocol
