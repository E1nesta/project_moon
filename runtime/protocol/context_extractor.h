#pragma once

#include "runtime/protocol/message_id.h"
#include "runtime/protocol/packet.h"
#include "runtime/protocol/request_context.h"

#include <optional>
#include <string>

namespace framework::protocol {

std::optional<common::net::RequestContext> ExtractRequestContext(common::net::MessageId message_id,
                                                                 const common::net::Packet& packet,
                                                                 std::string* error_message);

}  // namespace framework::protocol
