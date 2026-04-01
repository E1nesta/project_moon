#pragma once

#include "common/net/message_id.h"
#include "common/net/packet.h"

#include <google/protobuf/message_lite.h>

#include <string>

namespace common::net {

template <typename MessageT>
bool ParseMessage(const std::string& body, MessageT* message) {
    return message != nullptr && message->ParseFromString(body);
}

template <typename MessageT>
Packet BuildPacket(MessageId message_id, std::uint64_t request_id, const MessageT& message) {
    Packet packet;
    packet.header.msg_id = static_cast<std::uint32_t>(message_id);
    packet.header.request_id = request_id;
    message.SerializeToString(&packet.body);
    packet.header.body_len = static_cast<std::uint32_t>(packet.body.size());
    return packet;
}

}  // namespace common::net
