#include "apps/gateway/upstream_response_validator.h"

#include "runtime/protocol/handler_context.h"
#include "runtime/protocol/proto_mapper.h"
#include "runtime/session/session_store.h"

#include "game_backend.pb.h"

#include <iostream>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    std::cerr << message << '\n';
    return false;
}

class FakeSessionReader final : public common::session::SessionReader {
public:
    [[nodiscard]] std::optional<common::model::Session> FindById(const std::string& /*session_id*/) const override {
        return std::nullopt;
    }
};

common::net::Packet BuildUnexpectedResponse(const common::net::RequestContext& context) {
    return common::net::BuildPingResponsePacket(context, "pong");
}

common::net::Packet BuildLoadPlayerResponsePacket(const common::net::RequestContext& context) {
    game_backend::proto::LoadPlayerResponse response;
    common::net::FillProto(context, response.mutable_context());
    return common::net::BuildPacket(common::net::MessageId::kLoadPlayerResponse, context.request_id, response);
}

common::net::Packet BuildLoginResponsePacket(const common::net::RequestContext& context,
                                             const std::string& auth_token,
                                             std::int64_t account_id,
                                             std::int64_t player_id) {
    game_backend::proto::LoginResponse response;
    common::net::FillProto(context, response.mutable_context());
    response.set_auth_token(auth_token);
    response.set_account_id(account_id);
    response.set_player_id(player_id);
    return common::net::BuildPacket(common::net::MessageId::kLoginResponse, context.request_id, response);
}

}  // namespace

int main() {
    FakeSessionReader session_reader;
    services::gateway::SessionBindingService binding_service(session_reader);
    services::gateway::UpstreamResponseValidator validator(binding_service);

    framework::protocol::HandlerContext context;
    context.connection_id = 42;
    context.request.request_id = 7;
    context.request.trace_id = "trace-7";
    context.message_id = common::net::MessageId::kLoadPlayerRequest;

    common::net::Packet request_packet;
    request_packet.header.request_id = 7;
    request_packet.header.msg_id = static_cast<std::uint32_t>(common::net::MessageId::kLoadPlayerRequest);

    const auto unexpected_response = validator.Validate(
        common::net::MessageId::kLoadPlayerRequest, context, request_packet, BuildUnexpectedResponse(context.request));

    if (!Expect(unexpected_response.header.msg_id ==
                    static_cast<std::uint32_t>(common::net::MessageId::kErrorResponse),
                "unexpected upstream response should become ErrorResponse")) {
        return 1;
    }

    game_backend::proto::ErrorResponse error_response;
    if (!Expect(common::net::ParseMessage(unexpected_response.body, &error_response),
                "validator error response should parse")) {
        return 1;
    }
    if (!Expect(static_cast<common::error::ErrorCode>(error_response.error_code()) ==
                    common::error::ErrorCode::kUpstreamResponseInvalid,
                "unexpected upstream response should map to UPSTREAM_RESPONSE_INVALID")) {
        return 1;
    }
    if (!Expect(error_response.error_name() == "UPSTREAM_RESPONSE_INVALID",
                "validator should emit canonical error name")) {
        return 1;
    }

    auto mismatched_request = request_packet;
    mismatched_request.header.request_id = 8;
    const auto mismatched_response = validator.Validate(
        common::net::MessageId::kLoadPlayerRequest, context, mismatched_request, BuildUnexpectedResponse(context.request));
    if (!Expect(common::net::ParseMessage(mismatched_response.body, &error_response),
                "request id mismatch should also parse as ErrorResponse")) {
        return 1;
    }
    if (!Expect(static_cast<common::error::ErrorCode>(error_response.error_code()) ==
                    common::error::ErrorCode::kUpstreamResponseInvalid,
                "request id mismatch should map to UPSTREAM_RESPONSE_INVALID")) {
        return 1;
    }

    context.request.auth_token = "session-token";
    context.request.player_id = 20001;
    context.request.account_id = 10001;

    common::net::RequestContext mismatched_response_context = context.request;
    mismatched_response_context.player_id = 20002;
    const auto mismatched_context_response = validator.Validate(common::net::MessageId::kLoadPlayerRequest,
                                                                context,
                                                                request_packet,
                                                                BuildLoadPlayerResponsePacket(mismatched_response_context));
    if (!Expect(common::net::ParseMessage(mismatched_context_response.body, &error_response),
                "response context mismatch should parse as ErrorResponse")) {
        return 1;
    }
    if (!Expect(static_cast<common::error::ErrorCode>(error_response.error_code()) ==
                    common::error::ErrorCode::kUpstreamResponseInvalid,
                "response context mismatch should map to UPSTREAM_RESPONSE_INVALID")) {
        return 1;
    }

    framework::protocol::HandlerContext login_context;
    login_context.connection_id = 43;
    login_context.request.request_id = 9;
    login_context.request.trace_id = "trace-9";
    login_context.message_id = common::net::MessageId::kLoginRequest;

    common::net::Packet login_request_packet;
    login_request_packet.header.request_id = 9;
    login_request_packet.header.msg_id = static_cast<std::uint32_t>(common::net::MessageId::kLoginRequest);

    common::net::RequestContext invalid_login_response_context;
    invalid_login_response_context.request_id = 9;
    invalid_login_response_context.trace_id = "trace-9";
    invalid_login_response_context.auth_token = "context-token";
    invalid_login_response_context.account_id = 10001;
    invalid_login_response_context.player_id = 20001;
    const auto invalid_login_response = validator.Validate(common::net::MessageId::kLoginRequest,
                                                           login_context,
                                                           login_request_packet,
                                                           BuildLoginResponsePacket(
                                                               invalid_login_response_context, "body-token", 10001, 20001));
    if (!Expect(common::net::ParseMessage(invalid_login_response.body, &error_response),
                "login response context mismatch should parse as ErrorResponse")) {
        return 1;
    }
    if (!Expect(static_cast<common::error::ErrorCode>(error_response.error_code()) ==
                    common::error::ErrorCode::kUpstreamResponseInvalid,
                "login response context mismatch should map to UPSTREAM_RESPONSE_INVALID")) {
        return 1;
    }

    return 0;
}
