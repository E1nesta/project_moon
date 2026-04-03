#include "runtime/foundation/error/error_code.h"
#include "runtime/protocol/proto_mapper.h"
#include "runtime/protocol/error_responder.h"
#include "runtime/protocol/request_dispatcher.h"

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

}  // namespace

int main() {
    framework::protocol::RequestDispatcher dispatcher;
    dispatcher.Register(common::net::MessageId::kPingRequest,
                        [](const framework::protocol::HandlerContext& context, const common::net::Packet&) {
                            return common::net::BuildPingResponsePacket(context.request, "pong");
                        });

    framework::protocol::HandlerContext context;
    context.request.request_id = 7;
    context.request.trace_id = "trace-7";
    context.message_id = common::net::MessageId::kPingRequest;

    common::net::Packet request;
    request.header.request_id = 7;
    request.header.msg_id = static_cast<std::uint32_t>(common::net::MessageId::kPingRequest);

    const auto response = dispatcher.Dispatch(common::net::MessageId::kPingRequest, context, request);
    if (!Expect(response.has_value(), "registered route should dispatch")) {
        return 1;
    }
    if (!Expect(response->header.request_id == 7, "response should preserve request_id")) {
        return 1;
    }

    const auto missing = dispatcher.Dispatch(common::net::MessageId::kLoginRequest, context, request);
    if (!Expect(!missing.has_value(), "missing route should return empty result")) {
        return 1;
    }

    const auto error_packet = framework::protocol::BuildErrorResponse(
        context.request, common::error::ErrorCode::kBadGateway, "bad request");
    if (!Expect(error_packet.header.request_id == 7, "error response should preserve request_id")) {
        return 1;
    }
    if (!Expect(error_packet.header.msg_id == static_cast<std::uint32_t>(common::net::MessageId::kErrorResponse),
                "error response should use error message id")) {
        return 1;
    }

    game_backend::proto::ErrorResponse error_response;
    if (!Expect(common::net::ParseMessage(error_packet.body, &error_response),
                "error response should parse as ErrorResponse")) {
        return 1;
    }
    if (!Expect(error_response.error_name() == "BAD_GATEWAY",
                "error response should derive error_name from error code")) {
        return 1;
    }
    if (!Expect(error_response.error_message() == "bad request",
                "error response should preserve error message")) {
        return 1;
    }

    return 0;
}
