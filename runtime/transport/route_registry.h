#pragma once

#include "runtime/protocol/request_dispatcher.h"

namespace framework::service {

class RouteRegistry {
public:
    void Register(common::net::MessageId message_id, framework::protocol::RouteHandler handler) {
        dispatcher_.Register(message_id, std::move(handler));
    }

    [[nodiscard]] const framework::protocol::RequestDispatcher& Dispatcher() const {
        return dispatcher_;
    }

private:
    framework::protocol::RequestDispatcher dispatcher_;
};

}  // namespace framework::service
