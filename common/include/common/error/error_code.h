#pragma once

#include <string_view>

namespace common::error {

enum class ErrorCode {
    kOk = 0,
    kAccountNotFound,
    kAccountDisabled,
    kInvalidPassword,
    kSessionInvalid,
    kPlayerNotFound,
    kPlayerBusy,
    kDungeonNotFound,
    kDungeonLocked,
    kStaminaNotEnough,
    kBattleNotFound,
    kBattleMismatch,
    kBattleAlreadySettled,
    kInvalidDungeonId,
    kInvalidStar,
    kStorageError,
    kServiceUnavailable,
    kUpstreamTimeout,
    kBadGateway,
};

inline std::string_view ToString(ErrorCode code) {
    switch (code) {
    case ErrorCode::kOk:
        return "OK";
    case ErrorCode::kAccountNotFound:
        return "ACCOUNT_NOT_FOUND";
    case ErrorCode::kAccountDisabled:
        return "ACCOUNT_DISABLED";
    case ErrorCode::kInvalidPassword:
        return "INVALID_PASSWORD";
    case ErrorCode::kSessionInvalid:
        return "SESSION_INVALID";
    case ErrorCode::kPlayerNotFound:
        return "PLAYER_NOT_FOUND";
    case ErrorCode::kPlayerBusy:
        return "PLAYER_BUSY";
    case ErrorCode::kDungeonNotFound:
        return "DUNGEON_NOT_FOUND";
    case ErrorCode::kDungeonLocked:
        return "DUNGEON_LOCKED";
    case ErrorCode::kStaminaNotEnough:
        return "STAMINA_NOT_ENOUGH";
    case ErrorCode::kBattleNotFound:
        return "BATTLE_NOT_FOUND";
    case ErrorCode::kBattleMismatch:
        return "BATTLE_MISMATCH";
    case ErrorCode::kBattleAlreadySettled:
        return "BATTLE_ALREADY_SETTLED";
    case ErrorCode::kInvalidDungeonId:
        return "INVALID_DUNGEON_ID";
    case ErrorCode::kInvalidStar:
        return "INVALID_STAR";
    case ErrorCode::kStorageError:
        return "STORAGE_ERROR";
    case ErrorCode::kServiceUnavailable:
        return "SERVICE_UNAVAILABLE";
    case ErrorCode::kUpstreamTimeout:
        return "UPSTREAM_TIMEOUT";
    case ErrorCode::kBadGateway:
        return "BAD_GATEWAY";
    }

    return "UNKNOWN";
}

}  // namespace common::error
