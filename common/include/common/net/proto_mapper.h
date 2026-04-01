#pragma once

#include "common/error/error_code.h"
#include "common/model/player_state.h"
#include "common/model/reward.h"
#include "common/model/session.h"
#include "common/net/message_id.h"
#include "common/net/proto_codec.h"
#include "common/net/request_context.h"

#include "game_backend.pb.h"

#include <optional>
#include <string>

namespace common::net {

RequestContext FromProto(const game_backend::proto::RequestContext& context);
void FillProto(const RequestContext& context, game_backend::proto::RequestContext* output);
void FillProto(const RequestContext& context, game_backend::proto::ResponseContext* output);
RequestContext FromProto(const game_backend::proto::ResponseContext& context);

void FillProto(const common::model::Session& session, game_backend::proto::LoginResponse* output);
void FillProto(const common::model::PlayerState& state, game_backend::proto::PlayerState* output);
void FillProto(const common::model::Reward& reward, game_backend::proto::Reward* output);

Packet BuildErrorPacket(const RequestContext& context,
                        common::error::ErrorCode error_code,
                        const std::string& error_message);
Packet BuildPingResponsePacket(const RequestContext& context, const std::string& message);

bool ExtractRequestContext(MessageId message_id, const std::string& body, RequestContext* context);

}  // namespace common::net
