#pragma once

#include "apps/gateway/session_binding_service.h"
#include "runtime/foundation/error/error_code.h"
#include "runtime/protocol/message_id.h"
#include "runtime/protocol/packet.h"
#include "runtime/transport/transport_client.h"

namespace framework::protocol {
struct HandlerContext;
}

namespace services::gateway {

class UpstreamResponseValidator {
public:
    explicit UpstreamResponseValidator(SessionBindingService& session_binding_service);

    [[nodiscard]] common::error::ErrorCode MapForwardError(const std::string& error_message) const;
    [[nodiscard]] common::error::ErrorCode MapForwardError(
        framework::transport::TransportFailureCode failure_code,
        const std::string& error_message) const;
    [[nodiscard]] common::net::Packet Validate(common::net::MessageId message_id,
                                               const framework::protocol::HandlerContext& context,
                                               const common::net::Packet& request_packet,
                                               common::net::Packet upstream_response);

private:
    SessionBindingService& session_binding_service_;
};

}  // namespace services::gateway
