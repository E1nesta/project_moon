#pragma once

#include "common/error/error_code.h"
#include "common/net/packet.h"
#include "common/net/request_context.h"

#include <string>

namespace framework::protocol {

common::net::Packet BuildErrorResponse(const common::net::RequestContext& context,
                                       common::error::ErrorCode error_code,
                                       const std::string& error_message);

}  // namespace framework::protocol
