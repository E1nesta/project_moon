#include "runtime/protocol/error_responder.h"

#include "runtime/protocol/proto_mapper.h"

namespace framework::protocol {

common::net::Packet BuildErrorResponse(const common::net::RequestContext& context,
                                       common::error::ErrorCode error_code,
                                       const std::string& error_message) {
    return common::net::BuildErrorPacket(context, error_code, error_message);
}

}  // namespace framework::protocol
