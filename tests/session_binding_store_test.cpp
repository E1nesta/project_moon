#include "login_server/session/in_memory_session_repository.h"
#include "services/gateway/session_binding_store.h"

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
    services::gateway::SessionBindingStore store(session_repository);

    common::net::RequestContext context;
    context.session_id = session.session_id;

    const auto restored = store.ValidateOrRestore(1, &context);
    if (!Expect(restored.status == services::gateway::SessionBindingStore::Status::kRestored,
                "expected first request to restore binding from session")) {
        return 1;
    }
    if (!Expect(context.player_id == session.player_id,
                "expected restore to backfill player_id into request context")) {
        return 1;
    }

    common::net::RequestContext rebound_context;
    rebound_context.session_id = session.session_id;
    const auto rebound = store.ValidateOrRestore(1, &rebound_context);
    if (!Expect(rebound.status == services::gateway::SessionBindingStore::Status::kBound,
                "expected second request to use local binding")) {
        return 1;
    }
    if (!Expect(rebound_context.player_id == session.player_id,
                "expected local binding to backfill player_id into request context")) {
        return 1;
    }

    common::net::RequestContext invalid_context = context;
    invalid_context.player_id = 99999;
    const auto invalid = store.ValidateOrRestore(1, &invalid_context);
    if (!Expect(invalid.status == services::gateway::SessionBindingStore::Status::kInvalid,
                "expected mismatched binding to fail")) {
        return 1;
    }

    store.Unbind(1);
    common::net::RequestContext missing_context;
    missing_context.session_id = "missing";
    missing_context.player_id = 20001;
    const auto missing_session = store.ValidateOrRestore(2, &missing_context);
    if (!Expect(missing_session.status == services::gateway::SessionBindingStore::Status::kInvalid,
                "expected missing session to fail restore")) {
        return 1;
    }

    return 0;
}
