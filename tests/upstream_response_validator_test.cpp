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

    return 0;
}
