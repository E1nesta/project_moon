#pragma once

#include "runtime/protocol/message_id.h"
#include "runtime/protocol/packet.h"
#include "runtime/protocol/handler_context.h"

#include <string>

namespace framework::protocol {

bool EnrichContext(common::net::MessageId message_id,
                   const common::net::Packet& packet,
                   HandlerContext* context,
                   std::string* error_message);

}  // namespace framework::protocol
