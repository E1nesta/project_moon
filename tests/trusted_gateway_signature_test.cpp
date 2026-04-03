#include "runtime/protocol/proto_mapper.h"

#include <chrono>
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
    common::net::RequestContext context;
    context.trace_id = "trace-42";
    context.request_id = 42;
    context.auth_token = "auth-token-42";
    context.player_id = 20001;
    context.account_id = 10001;

    game_backend::proto::LoadPlayerRequest request;
    common::net::FillProto(context, request.mutable_context());
    request.set_player_id(context.player_id);

    auto packet = common::net::BuildPacket(common::net::MessageId::kLoadPlayerRequest, context.request_id, request);
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    const std::string shared_secret = "trusted-gateway-test-secret";
    std::string error_message;

    if (!Expect(common::net::SignTrustedRequest(
                    common::net::MessageId::kLoadPlayerRequest,
                    now_ms.count(),
                    shared_secret,
                    &packet,
                    &error_message),
                "expected request signing to succeed: " + error_message)) {
        return 1;
    }

    if (!Expect(common::net::ValidateTrustedRequest(
                    common::net::MessageId::kLoadPlayerRequest,
                    10000,
                    shared_secret,
                    packet,
                    &error_message),
                "expected signed request validation to succeed: " + error_message)) {
        return 1;
    }

    game_backend::proto::LoadPlayerRequest tampered_request;
    if (!Expect(common::net::ParseMessage(packet.body, &tampered_request),
                "expected to parse signed request packet")) {
        return 1;
    }
    tampered_request.set_player_id(99999);
    auto tampered_packet =
        common::net::BuildPacket(common::net::MessageId::kLoadPlayerRequest, context.request_id, tampered_request);

    if (!Expect(!common::net::ValidateTrustedRequest(
                    common::net::MessageId::kLoadPlayerRequest,
                    10000,
                    shared_secret,
                    tampered_packet,
                    &error_message),
                "expected tampered request validation to fail")) {
        return 1;
    }

    return 0;
}
