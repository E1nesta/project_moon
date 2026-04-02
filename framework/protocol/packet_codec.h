#pragma once

#include "common/net/packet.h"

#include <string>
#include <string_view>

namespace framework::protocol {

constexpr std::size_t kPacketHeaderSize = common::net::kPacketHeaderSize;
constexpr std::uint32_t kDefaultMaxPacketBodyBytes = 4U * 1024U * 1024U;

std::string EncodePacket(const common::net::Packet& packet);
bool DecodeHeader(std::string_view bytes,
                  common::net::PacketHeader* header,
                  std::string* error_message = nullptr,
                  std::uint32_t max_body_bytes = kDefaultMaxPacketBodyBytes);
bool TryExtractPacket(std::string& buffer, common::net::Packet* packet);

}  // namespace framework::protocol
