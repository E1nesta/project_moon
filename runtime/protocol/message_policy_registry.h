#pragma once

#include "runtime/protocol/message_id.h"
#include "runtime/execution/execution_types.h"

#include <optional>

namespace framework::protocol {

struct MessagePolicy {
    bool requires_auth_token = false;
    bool requires_player = false;
    bool allow_player_id_from_body = false;
    framework::execution::ExecutionKeyKind execution_key_kind =
        framework::execution::ExecutionKeyKind::kDirect;
    std::optional<common::net::MessageId> expected_response;
};

class MessagePolicyRegistry {
public:
    static std::optional<MessagePolicy> Find(common::net::MessageId message_id);
};

}  // namespace framework::protocol
