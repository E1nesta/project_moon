#include "runtime/protocol/message_policy_registry.h"

namespace framework::protocol {

std::optional<MessagePolicy> MessagePolicyRegistry::Find(common::net::MessageId message_id) {
    using common::net::MessageId;
    using framework::execution::ExecutionKeyKind;

    switch (message_id) {
    case MessageId::kPingRequest:
        return MessagePolicy{false, false, false, ExecutionKeyKind::kDirect, MessageId::kPingResponse};
    case MessageId::kLoginRequest:
        return MessagePolicy{false, false, false, ExecutionKeyKind::kConnection, MessageId::kLoginResponse};
    case MessageId::kLoadPlayerRequest:
        return MessagePolicy{true, true, true, ExecutionKeyKind::kPlayer, MessageId::kLoadPlayerResponse};
    case MessageId::kEnterBattleRequest:
        return MessagePolicy{true, true, true, ExecutionKeyKind::kPlayer, MessageId::kEnterBattleResponse};
    case MessageId::kSettleBattleRequest:
        return MessagePolicy{true, true, true, ExecutionKeyKind::kPlayer, MessageId::kSettleBattleResponse};
    case MessageId::kGetRewardGrantStatusRequest:
        return MessagePolicy{true, true, true, ExecutionKeyKind::kPlayer, MessageId::kGetRewardGrantStatusResponse};
    default:
        return std::nullopt;
    }
}

}  // namespace framework::protocol
