#include "gateway/session_binding_authorizer.h"
#include "login_server/session/in_memory_session_repository.h"

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
    login_server::session::InMemorySessionRepository session_repository;
    const auto session = session_repository.Create(10001, 20001);
    gateway::SessionBindingAuthorizer authorizer(session_repository);

    common::net::RequestContext context;
    context.session_id = session.session_id;
    context.player_id = session.player_id;

    const auto restored = authorizer.ValidateOrRestore(1, context);
    if (!Expect(restored.status == gateway::SessionBindingAuthorizer::Status::kRestored,
                "expected first request to restore binding from session")) {
        return 1;
    }

    const auto rebound = authorizer.ValidateOrRestore(1, context);
    if (!Expect(rebound.status == gateway::SessionBindingAuthorizer::Status::kBound,
                "expected second request to use local binding")) {
        return 1;
    }

    common::net::RequestContext invalid_context = context;
    invalid_context.player_id = 99999;
    const auto invalid = authorizer.ValidateOrRestore(1, invalid_context);
    if (!Expect(invalid.status == gateway::SessionBindingAuthorizer::Status::kInvalid,
                "expected mismatched binding to fail")) {
        return 1;
    }

    authorizer.Unbind(1);
    common::net::RequestContext missing_context;
    missing_context.session_id = "missing";
    missing_context.player_id = 20001;
    const auto missing_session = authorizer.ValidateOrRestore(2, missing_context);
    if (!Expect(missing_session.status == gateway::SessionBindingAuthorizer::Status::kInvalid,
                "expected missing session to fail restore")) {
        return 1;
    }

    return 0;
}
