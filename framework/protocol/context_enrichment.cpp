#include "framework/protocol/context_enrichment.h"

#include "common/net/proto_codec.h"
#include "framework/protocol/message_policy_registry.h"

#include "game_backend.pb.h"

namespace framework::protocol {

namespace {

bool ExtractPlayerIdFromBody(common::net::MessageId message_id,
                             const common::net::Packet& packet,
                             std::int64_t* player_id,
                             std::string* error_message) {
    if (player_id == nullptr) {
        if (error_message != nullptr) {
            *error_message = "player_id output is null";
        }
        return false;
    }

    switch (message_id) {
    case common::net::MessageId::kLoadPlayerRequest: {
        game_backend::proto::LoadPlayerRequest request;
        if (!common::net::ParseMessage(packet.body, &request)) {
            if (error_message != nullptr) {
                *error_message = "invalid load player request";
            }
            return false;
        }
        *player_id = request.player_id();
        return true;
    }
    case common::net::MessageId::kEnterDungeonRequest: {
        game_backend::proto::EnterDungeonRequest request;
        if (!common::net::ParseMessage(packet.body, &request)) {
            if (error_message != nullptr) {
                *error_message = "invalid enter dungeon request";
            }
            return false;
        }
        *player_id = request.player_id();
        return true;
    }
    case common::net::MessageId::kSettleDungeonRequest: {
        game_backend::proto::SettleDungeonRequest request;
        if (!common::net::ParseMessage(packet.body, &request)) {
            if (error_message != nullptr) {
                *error_message = "invalid settle dungeon request";
            }
            return false;
        }
        *player_id = request.player_id();
        return true;
    }
    default:
        *player_id = 0;
        return true;
    }
}

}  // namespace

bool EnrichContext(common::net::MessageId message_id,
                   const common::net::Packet& packet,
                   HandlerContext* context,
                   std::string* error_message) {
    if (context == nullptr) {
        if (error_message != nullptr) {
            *error_message = "handler context is null";
        }
        return false;
    }

    const auto policy = MessagePolicyRegistry::Find(message_id);
    if (!policy.has_value() || !policy->allow_player_id_from_body || context->request.player_id != 0) {
        return true;
    }

    std::int64_t player_id = 0;
    if (!ExtractPlayerIdFromBody(message_id, packet, &player_id, error_message)) {
        return false;
    }

    if (player_id != 0) {
        context->request.player_id = player_id;
    }
    return true;
}

}  // namespace framework::protocol
