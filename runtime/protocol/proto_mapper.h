#pragma once

#include "runtime/foundation/error/error_code.h"
#include "runtime/session/session.h"
#include "runtime/protocol/message_id.h"
#include "runtime/protocol/proto_codec.h"
#include "runtime/protocol/request_context.h"

#include "game_backend.pb.h"

#include <optional>
#include <string>

namespace common::net {

RequestContext FromProto(const game_backend::proto::RequestContext& context);
void FillProto(const RequestContext& context, game_backend::proto::RequestContext* output);
void FillProto(const RequestContext& context, game_backend::proto::ResponseContext* output);
RequestContext FromProto(const game_backend::proto::ResponseContext& context);
bool RewriteRequestContext(MessageId message_id, const RequestContext& context, Packet* packet);
bool SignTrustedRequest(MessageId message_id,
                        std::int64_t gateway_timestamp_ms,
                        const std::string& shared_secret,
                        Packet* packet,
                        std::string* error_message);
bool ValidateTrustedRequest(MessageId message_id,
                            std::int64_t max_clock_skew_ms,
                            const std::string& shared_secret,
                            const Packet& packet,
                            std::string* error_message);

Packet BuildErrorPacket(const RequestContext& context,
                        common::error::ErrorCode error_code,
                        const std::string& error_message);
Packet BuildPingResponsePacket(const RequestContext& context, const std::string& message);

bool ExtractRequestContext(MessageId message_id, const std::string& body, RequestContext* context);
bool ExtractResponseContext(MessageId message_id, const std::string& body, RequestContext* context);

}  // namespace common::net
