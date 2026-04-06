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
    kEnterBattleRequest = 1201,
    kEnterBattleResponse = 1202,
    kSettleBattleRequest = 1203,
    kSettleBattleResponse = 1204,
    kGetRewardGrantStatusRequest = 1205,
    kGetRewardGrantStatusResponse = 1206,
    kGetActiveBattleRequest = 1207,
    kGetActiveBattleResponse = 1208,
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
        return MessageId::kEnterBattleRequest;
    case 1202:
        return MessageId::kEnterBattleResponse;
    case 1203:
        return MessageId::kSettleBattleRequest;
    case 1204:
        return MessageId::kSettleBattleResponse;
    case 1205:
        return MessageId::kGetRewardGrantStatusRequest;
    case 1206:
        return MessageId::kGetRewardGrantStatusResponse;
    case 1207:
        return MessageId::kGetActiveBattleRequest;
    case 1208:
        return MessageId::kGetActiveBattleResponse;
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
    case MessageId::kEnterBattleRequest:
        return "ENTER_BATTLE_REQUEST";
    case MessageId::kEnterBattleResponse:
        return "ENTER_BATTLE_RESPONSE";
    case MessageId::kSettleBattleRequest:
        return "SETTLE_BATTLE_REQUEST";
    case MessageId::kSettleBattleResponse:
        return "SETTLE_BATTLE_RESPONSE";
    case MessageId::kGetRewardGrantStatusRequest:
        return "GET_REWARD_GRANT_STATUS_REQUEST";
    case MessageId::kGetRewardGrantStatusResponse:
        return "GET_REWARD_GRANT_STATUS_RESPONSE";
    case MessageId::kGetActiveBattleRequest:
        return "GET_ACTIVE_BATTLE_REQUEST";
    case MessageId::kGetActiveBattleResponse:
        return "GET_ACTIVE_BATTLE_RESPONSE";
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
    case MessageId::kEnterBattleRequest:
        return MessageId::kEnterBattleResponse;
    case MessageId::kSettleBattleRequest:
        return MessageId::kSettleBattleResponse;
    case MessageId::kGetRewardGrantStatusRequest:
        return MessageId::kGetRewardGrantStatusResponse;
    case MessageId::kGetActiveBattleRequest:
        return MessageId::kGetActiveBattleResponse;
    default:
        return std::nullopt;
    }
}

}  // namespace common::net
