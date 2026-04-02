#include "framework/protocol/error_responder.h"

#include "common/net/proto_mapper.h"

namespace framework::protocol {

common::net::Packet BuildErrorResponse(const common::net::RequestContext& context,
                                       common::error::ErrorCode error_code,
                                       const std::string& error_message) {
    return common::net::BuildErrorPacket(context, error_code, error_message);
}

}  // namespace framework::protocol
