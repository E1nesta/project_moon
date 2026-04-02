#pragma once

#include "common/net/message_id.h"
#include "common/net/packet.h"
#include "framework/protocol/handler_context.h"

#include <functional>
#include <optional>
#include <unordered_map>

namespace framework::protocol {

using RouteHandler = std::function<common::net::Packet(const HandlerContext&, const common::net::Packet&)>;

class RequestDispatcher {
public:
    void Register(common::net::MessageId message_id, RouteHandler handler);
    [[nodiscard]] bool CanHandle(common::net::MessageId message_id) const;
    [[nodiscard]] std::optional<common::net::Packet> Dispatch(common::net::MessageId message_id,
                                                              const HandlerContext& context,
                                                              const common::net::Packet& packet) const;

private:
    std::unordered_map<std::uint32_t, RouteHandler> handlers_;
};

}  // namespace framework::protocol
