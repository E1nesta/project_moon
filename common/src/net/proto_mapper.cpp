#include "common/net/proto_mapper.h"

namespace common::net {

RequestContext FromProto(const game_backend::proto::RequestContext& context) {
    RequestContext output;
    output.trace_id = context.trace_id();
    output.request_id = context.request_id();
    output.session_id = context.session_id();
    output.player_id = context.player_id();
    return output;
}

void FillProto(const RequestContext& context, game_backend::proto::RequestContext* output) {
    if (output == nullptr) {
        return;
    }

    output->set_trace_id(context.trace_id);
    output->set_request_id(context.request_id);
    output->set_session_id(context.session_id);
    output->set_player_id(context.player_id);
}

void FillProto(const RequestContext& context, game_backend::proto::ResponseContext* output) {
    if (output == nullptr) {
        return;
    }

    output->set_trace_id(context.trace_id);
    output->set_request_id(context.request_id);
    output->set_session_id(context.session_id);
    output->set_player_id(context.player_id);
}

RequestContext FromProto(const game_backend::proto::ResponseContext& context) {
    RequestContext output;
    output.trace_id = context.trace_id();
    output.request_id = context.request_id();
    output.session_id = context.session_id();
    output.player_id = context.player_id();
    return output;
}

void FillProto(const common::model::Session& session, game_backend::proto::LoginResponse* output) {
    if (output == nullptr) {
        return;
    }

    output->set_session_id(session.session_id);
    output->set_account_id(session.account_id);
    output->set_player_id(session.player_id);
    output->set_created_at_epoch_seconds(session.created_at_epoch_seconds);
}

void FillProto(const common::model::PlayerState& state, game_backend::proto::PlayerState* output) {
    if (output == nullptr) {
        return;
    }

    auto* profile = output->mutable_profile();
    profile->set_player_id(state.profile.player_id);
    profile->set_account_id(state.profile.account_id);
    profile->set_player_name(state.profile.player_name);
    profile->set_level(state.profile.level);
    profile->set_stamina(state.profile.stamina);
    profile->set_gold(state.profile.gold);
    profile->set_diamond(state.profile.diamond);

    output->clear_dungeon_progress();
    for (const auto& progress : state.dungeon_progress) {
        auto* item = output->add_dungeon_progress();
        item->set_dungeon_id(progress.dungeon_id);
        item->set_best_star(progress.best_star);
        item->set_is_first_clear(progress.is_first_clear);
    }
}

void FillProto(const common::model::Reward& reward, game_backend::proto::Reward* output) {
    if (output == nullptr) {
        return;
    }

    output->set_reward_type(reward.reward_type);
    output->set_amount(reward.amount);
}

Packet BuildErrorPacket(const RequestContext& context,
                        common::error::ErrorCode error_code,
                        const std::string& error_message) {
    game_backend::proto::ErrorResponse response;
    FillProto(context, response.mutable_context());
    response.set_error_code(static_cast<int>(error_code));
    response.set_error_name(std::string(common::error::ToString(error_code)));
    response.set_error_message(error_message);
    return BuildPacket(MessageId::kErrorResponse, context.request_id, response);
}

Packet BuildPingResponsePacket(const RequestContext& context, const std::string& message) {
    game_backend::proto::PingResponse response;
    FillProto(context, response.mutable_context());
    response.set_message(message);
    return BuildPacket(MessageId::kPingResponse, context.request_id, response);
}

bool ExtractRequestContext(MessageId message_id, const std::string& body, RequestContext* context) {
    if (context == nullptr) {
        return false;
    }

    switch (message_id) {
    case MessageId::kPingRequest: {
        game_backend::proto::PingRequest request;
        return ParseMessage(body, &request) && (*context = FromProto(request.context()), true);
    }
    case MessageId::kLoginRequest: {
        game_backend::proto::LoginRequest request;
        return ParseMessage(body, &request) && (*context = FromProto(request.context()), true);
    }
    case MessageId::kLoadPlayerRequest: {
        game_backend::proto::LoadPlayerRequest request;
        return ParseMessage(body, &request) && (*context = FromProto(request.context()), true);
    }
    case MessageId::kEnterDungeonRequest: {
        game_backend::proto::EnterDungeonRequest request;
        return ParseMessage(body, &request) && (*context = FromProto(request.context()), true);
    }
    case MessageId::kSettleDungeonRequest: {
        game_backend::proto::SettleDungeonRequest request;
        return ParseMessage(body, &request) && (*context = FromProto(request.context()), true);
    }
    default:
        return false;
    }
}

}  // namespace common::net
