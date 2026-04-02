#include "framework/protocol/packet_codec.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

}  // namespace

int main() {
    common::net::Packet packet;
    packet.header.msg_id = 1203;
    packet.header.request_id = 42;
    packet.body = "payload";
    packet.header.body_len = static_cast<std::uint32_t>(packet.body.size());

    const auto encoded = framework::protocol::EncodePacket(packet);
    common::net::PacketHeader decoded_header{};
    std::string error_message;
    if (!Expect(framework::protocol::DecodeHeader(
                    std::string_view(encoded.data(), framework::protocol::kPacketHeaderSize),
                    &decoded_header,
                    &error_message),
                "expected header decode to succeed")) {
        return 1;
    }

    if (!Expect(decoded_header.request_id == 42, "expected request_id to be preserved")) {
        return 1;
    }

    std::string buffer = encoded.substr(0, 5);
    common::net::Packet extracted;
    if (!Expect(!framework::protocol::TryExtractPacket(buffer, &extracted),
                "partial packet should not be extracted")) {
        return 1;
    }

    buffer.append(encoded.substr(5));
    if (!Expect(framework::protocol::TryExtractPacket(buffer, &extracted),
                "complete packet should be extracted")) {
        return 1;
    }
    if (!Expect(extracted.header.request_id == packet.header.request_id && extracted.body == packet.body,
                "expected extracted packet to match original")) {
        return 1;
    }

    buffer = encoded + encoded;
    if (!Expect(framework::protocol::TryExtractPacket(buffer, &extracted),
                "first sticky packet should be extracted")) {
        return 1;
    }
    if (!Expect(framework::protocol::TryExtractPacket(buffer, &extracted),
                "second sticky packet should be extracted")) {
        return 1;
    }

    std::string invalid = encoded;
    invalid[0] = '\0';
    if (!Expect(!framework::protocol::TryExtractPacket(invalid, &extracted),
                "invalid magic should fail extraction")) {
        return 1;
    }

    common::net::Packet oversized_header_packet;
    oversized_header_packet.header.msg_id = 1203;
    oversized_header_packet.header.request_id = 77;
    oversized_header_packet.header.body_len = framework::protocol::kDefaultMaxPacketBodyBytes + 1U;
    const auto oversized = framework::protocol::EncodePacket(oversized_header_packet);
    if (!Expect(!framework::protocol::DecodeHeader(
                    std::string_view(oversized.data(), framework::protocol::kPacketHeaderSize),
                    &decoded_header,
                    &error_message),
                "oversized packet body should be rejected")) {
        return 1;
    }

    return 0;
}
