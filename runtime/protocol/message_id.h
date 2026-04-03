#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace common::net {

enum class MessageId : std::uint32_t {
    kPingRequest = 1,
    kPingResponse = 2,
    kLoginRequest = 1001,
    kLoginResponse = 1002,
    kLoadPlayerRequest = 1101,
    kLoadPlayerResponse = 1102,
    kEnterDungeonRequest = 1201,
    kEnterDungeonResponse = 1202,
    kSettleDungeonRequest = 1203,
    kSettleDungeonResponse = 1204,
    kErrorResponse = 9000,
};

inline std::optional<MessageId> MessageIdFromInt(std::uint32_t value) {
    switch (value) {
    case 1:
        return MessageId::kPingRequest;
    case 2:
        return MessageId::kPingResponse;
    case 1001:
        return MessageId::kLoginRequest;
    case 1002:
        return MessageId::kLoginResponse;
    case 1101:
        return MessageId::kLoadPlayerRequest;
    case 1102:
        return MessageId::kLoadPlayerResponse;
    case 1201:
        return MessageId::kEnterDungeonRequest;
    case 1202:
        return MessageId::kEnterDungeonResponse;
    case 1203:
        return MessageId::kSettleDungeonRequest;
    case 1204:
        return MessageId::kSettleDungeonResponse;
    case 9000:
        return MessageId::kErrorResponse;
    default:
        return std::nullopt;
    }
}

inline std::string_view ToString(MessageId message_id) {
    switch (message_id) {
    case MessageId::kPingRequest:
        return "PING_REQUEST";
    case MessageId::kPingResponse:
        return "PING_RESPONSE";
    case MessageId::kLoginRequest:
        return "LOGIN_REQUEST";
    case MessageId::kLoginResponse:
        return "LOGIN_RESPONSE";
    case MessageId::kLoadPlayerRequest:
        return "LOAD_PLAYER_REQUEST";
    case MessageId::kLoadPlayerResponse:
        return "LOAD_PLAYER_RESPONSE";
    case MessageId::kEnterDungeonRequest:
        return "ENTER_DUNGEON_REQUEST";
    case MessageId::kEnterDungeonResponse:
        return "ENTER_DUNGEON_RESPONSE";
    case MessageId::kSettleDungeonRequest:
        return "SETTLE_DUNGEON_REQUEST";
    case MessageId::kSettleDungeonResponse:
        return "SETTLE_DUNGEON_RESPONSE";
    case MessageId::kErrorResponse:
        return "ERROR_RESPONSE";
    }

    return "UNKNOWN";
}

inline std::optional<MessageId> ExpectedResponseMessageId(MessageId request_id) {
    switch (request_id) {
    case MessageId::kPingRequest:
        return MessageId::kPingResponse;
    case MessageId::kLoginRequest:
        return MessageId::kLoginResponse;
    case MessageId::kLoadPlayerRequest:
        return MessageId::kLoadPlayerResponse;
    case MessageId::kEnterDungeonRequest:
        return MessageId::kEnterDungeonResponse;
    case MessageId::kSettleDungeonRequest:
        return MessageId::kSettleDungeonResponse;
    default:
        return std::nullopt;
    }
}

}  // namespace common::net
