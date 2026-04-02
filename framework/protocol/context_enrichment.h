#pragma once

#include "common/net/message_id.h"
#include "common/net/packet.h"
#include "framework/protocol/handler_context.h"

#include <string>

namespace framework::protocol {

bool EnrichContext(common::net::MessageId message_id,
                   const common::net::Packet& packet,
                   HandlerContext* context,
                   std::string* error_message);

}  // namespace framework::protocol
