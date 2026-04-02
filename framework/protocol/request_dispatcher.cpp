#include "framework/protocol/request_dispatcher.h"

namespace framework::protocol {

void RequestDispatcher::Register(common::net::MessageId message_id, RouteHandler handler) {
    handlers_[static_cast<std::uint32_t>(message_id)] = std::move(handler);
}

bool RequestDispatcher::CanHandle(common::net::MessageId message_id) const {
    return handlers_.find(static_cast<std::uint32_t>(message_id)) != handlers_.end();
}

std::optional<common::net::Packet> RequestDispatcher::Dispatch(common::net::MessageId message_id,
                                                               const HandlerContext& context,
                                                               const common::net::Packet& packet) const {
    const auto iter = handlers_.find(static_cast<std::uint32_t>(message_id));
    if (iter == handlers_.end()) {
        return std::nullopt;
    }

    return iter->second(context, packet);
}

}  // namespace framework::protocol
