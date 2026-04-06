#include "apps/gateway/upstream_response_validator.h"

#include "runtime/protocol/proto_codec.h"
#include "runtime/protocol/proto_mapper.h"
#include "runtime/protocol/error_responder.h"
#include "runtime/protocol/handler_context.h"
#include "runtime/protocol/message_policy_registry.h"

#include "game_backend.pb.h"

namespace services::gateway {

namespace {

bool ValidateResponseContextBinding(const common::net::RequestContext& request_context,
                                    const common::net::RequestContext& response_context,
                                    std::string* error_message) {
    if (response_context.request_id != 0 && response_context.request_id != request_context.request_id) {
        if (error_message != nullptr) {
            *error_message = "upstream response context request_id mismatch";
        }
        return false;
    }
    if (!request_context.auth_token.empty() && response_context.auth_token != request_context.auth_token) {
        if (error_message != nullptr) {
            *error_message = "upstream response context auth_token mismatch";
        }
        return false;
    }
    if (request_context.player_id != 0 && response_context.player_id != request_context.player_id) {
        if (error_message != nullptr) {
            *error_message = "upstream response context player_id mismatch";
        }
        return false;
    }
    if (request_context.account_id != 0 && response_context.account_id != request_context.account_id) {
        if (error_message != nullptr) {
            *error_message = "upstream response context account_id mismatch";
        }
        return false;
    }
    return true;
}

bool ValidateLoginResponseBinding(const common::net::RequestContext& response_context,
                                  const common::net::Packet& upstream_response,
                                  std::string* error_message) {
    game_backend::proto::LoginResponse response;
    if (!common::net::ParseMessage(upstream_response.body, &response)) {
        if (error_message != nullptr) {
            *error_message = "failed to parse login response";
        }
        return false;
    }
    if (response_context.auth_token != response.auth_token() || response_context.player_id != response.player_id() ||
        response_context.account_id != response.account_id()) {
        if (error_message != nullptr) {
            *error_message = "upstream login response context mismatch";
        }
        return false;
    }
    return true;
}

}  // namespace

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

    common::net::RequestContext response_context;
    if (!common::net::ExtractResponseContext(*upstream_message_id, upstream_response.body, &response_context)) {
        return framework::protocol::BuildErrorResponse(
            context.request,
            common::error::ErrorCode::kUpstreamResponseInvalid,
            "failed to parse upstream response context");
    }

    std::string error_message;
    if (message_id == common::net::MessageId::kLoginRequest &&
        *upstream_message_id == common::net::MessageId::kLoginResponse) {
        if (!ValidateLoginResponseBinding(response_context, upstream_response, &error_message)) {
            return framework::protocol::BuildErrorResponse(
                context.request, common::error::ErrorCode::kUpstreamResponseInvalid, error_message);
        }
    } else if (!ValidateResponseContextBinding(context.request, response_context, &error_message)) {
        return framework::protocol::BuildErrorResponse(
            context.request, common::error::ErrorCode::kUpstreamResponseInvalid, error_message);
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
