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
    kStageNotFound,
    kStageLocked,
    kStaminaNotEnough,
    kBattleNotFound,
    kBattleMismatch,
    kBattleAlreadySettled,
    kInvalidStageId,
    kInvalidStar,
    kStorageError,
    kServiceUnavailable,
    kUpstreamTimeout,
    kBadGateway,
    kRateLimited,
    kRequestContextInvalid,
    kMessageNotSupported,
    kTrustedGatewayInvalid,
    kUpstreamResponseInvalid,
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
    case ErrorCode::kStageNotFound:
        return "STAGE_NOT_FOUND";
    case ErrorCode::kStageLocked:
        return "STAGE_LOCKED";
    case ErrorCode::kStaminaNotEnough:
        return "STAMINA_NOT_ENOUGH";
    case ErrorCode::kBattleNotFound:
        return "BATTLE_NOT_FOUND";
    case ErrorCode::kBattleMismatch:
        return "BATTLE_MISMATCH";
    case ErrorCode::kBattleAlreadySettled:
        return "BATTLE_ALREADY_SETTLED";
    case ErrorCode::kInvalidStageId:
        return "INVALID_STAGE_ID";
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
    case ErrorCode::kRateLimited:
        return "RATE_LIMITED";
    case ErrorCode::kRequestContextInvalid:
        return "REQUEST_CONTEXT_INVALID";
    case ErrorCode::kMessageNotSupported:
        return "MESSAGE_NOT_SUPPORTED";
    case ErrorCode::kTrustedGatewayInvalid:
        return "TRUSTED_GATEWAY_INVALID";
    case ErrorCode::kUpstreamResponseInvalid:
        return "UPSTREAM_RESPONSE_INVALID";
    }

    return "UNKNOWN";
}

}  // namespace common::error
