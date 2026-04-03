#include "apps/gateway/upstream_response_validator.h"

#include "runtime/protocol/proto_codec.h"
#include "runtime/protocol/error_responder.h"
#include "runtime/protocol/handler_context.h"
#include "runtime/protocol/message_policy_registry.h"

#include "game_backend.pb.h"

namespace services::gateway {

UpstreamResponseValidator::UpstreamResponseValidator(SessionBindingService& session_binding_service)
    : session_binding_service_(session_binding_service) {}

common::error::ErrorCode UpstreamResponseValidator::MapForwardError(const std::string& error_message) const {
    if (error_message == "timeout") {
        return common::error::ErrorCode::kUpstreamTimeout;
    }
    return common::error::ErrorCode::kServiceUnavailable;
}

common::error::ErrorCode UpstreamResponseValidator::MapForwardError(
    framework::transport::TransportFailureCode failure_code,
    const std::string& error_message) const {
    switch (failure_code) {
    case framework::transport::TransportFailureCode::kTimeout:
        return common::error::ErrorCode::kUpstreamTimeout;
    case framework::transport::TransportFailureCode::kProtocolDecodeFailed:
        return common::error::ErrorCode::kUpstreamResponseInvalid;
    case framework::transport::TransportFailureCode::kTlsCertificateValidationFailed:
    case framework::transport::TransportFailureCode::kTlsHandshakeFailed:
    case framework::transport::TransportFailureCode::kTlsSetupFailed:
    case framework::transport::TransportFailureCode::kConnectFailed:
    case framework::transport::TransportFailureCode::kResolveFailed:
    case framework::transport::TransportFailureCode::kNoUpstreamClients:
    case framework::transport::TransportFailureCode::kReadFailed:
    case framework::transport::TransportFailureCode::kWriteFailed:
        return common::error::ErrorCode::kServiceUnavailable;
    case framework::transport::TransportFailureCode::kNone:
        break;
    }
    return MapForwardError(error_message);
}

common::net::Packet UpstreamResponseValidator::Validate(common::net::MessageId message_id,
                                                        const framework::protocol::HandlerContext& context,
                                                        const common::net::Packet& request_packet,
                                                        common::net::Packet upstream_response) {
    if (upstream_response.header.request_id != request_packet.header.request_id) {
        return framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kUpstreamResponseInvalid, "upstream request_id mismatch");
    }

    const auto upstream_message_id = common::net::MessageIdFromInt(upstream_response.header.msg_id);
    if (!upstream_message_id.has_value()) {
        return framework::protocol::BuildErrorResponse(
            context.request,
            common::error::ErrorCode::kUpstreamResponseInvalid,
            "upstream returned unknown message id");
    }

    const auto policy = framework::protocol::MessagePolicyRegistry::Find(message_id);
    if (*upstream_message_id != common::net::MessageId::kErrorResponse) {
        if (!policy.has_value() || !policy->expected_response.has_value() ||
            *policy->expected_response != *upstream_message_id) {
            return framework::protocol::BuildErrorResponse(
                context.request,
                common::error::ErrorCode::kUpstreamResponseInvalid,
                "upstream returned unexpected response type");
        }
    }

    if (message_id == common::net::MessageId::kLoginRequest &&
        *upstream_message_id == common::net::MessageId::kLoginResponse) {
        game_backend::proto::LoginResponse response;
        if (!common::net::ParseMessage(upstream_response.body, &response)) {
            return framework::protocol::BuildErrorResponse(
                context.request, common::error::ErrorCode::kUpstreamResponseInvalid, "failed to parse login response");
        }
        session_binding_service_.Bind(
            context.connection_id, response.auth_token(), response.player_id(), response.account_id());
    }

    return upstream_response;
}

}  // namespace services::gateway
