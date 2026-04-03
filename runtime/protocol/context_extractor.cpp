#include "runtime/protocol/context_extractor.h"

#include "runtime/protocol/proto_mapper.h"

namespace framework::protocol {

std::optional<common::net::RequestContext> ExtractRequestContext(common::net::MessageId message_id,
                                                                 const common::net::Packet& packet,
                                                                 std::string* error_message) {
    common::net::RequestContext context;
    if (common::net::ExtractRequestContext(message_id, packet.body, &context)) {
        return context;
    }

    if (error_message != nullptr) {
        *error_message = "failed to parse request context";
    }
    return std::nullopt;
}

}  // namespace framework::protocol
