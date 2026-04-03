#pragma once

#include "runtime/foundation/error/error_code.h"
#include "runtime/protocol/packet.h"
#include "runtime/protocol/request_context.h"

#include <string>

namespace framework::protocol {

common::net::Packet BuildErrorResponse(const common::net::RequestContext& context,
                                       common::error::ErrorCode error_code,
                                       const std::string& error_message);

}  // namespace framework::protocol
