#pragma once

#include "common/error/error_code.h"
#include "common/net/message_id.h"
#include "common/net/packet.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"
#include "framework/protocol/error_responder.h"
#include "framework/protocol/handler_context.h"

#include <string>
#include <utility>

namespace framework::protocol {

// Shared adapter helpers keep service entrypoints focused on parse -> handle -> build.
template <typename ProtoRequest>
bool ParseProtoRequest(const HandlerContext& context,
                       const common::net::Packet& packet,
                       const std::string& invalid_message,
                       ProtoRequest* request,
                       common::net::Packet* error_response) {
    if (request != nullptr && common::net::ParseMessage(packet.body, request)) {
        return true;
    }

    if (error_response != nullptr) {
        *error_response =
            BuildErrorResponse(context.request, common::error::ErrorCode::kBadGateway, invalid_message);
    }
    return false;
}

template <typename ProtoResponse>
void FillCommonResponseFields(const common::net::RequestContext& response_context,
                              bool success,
                              common::error::ErrorCode error_code,
                              const std::string& error_message,
                              ProtoResponse* response) {
    if (response == nullptr) {
        return;
    }

    common::net::FillProto(response_context, response->mutable_context());
    response->set_success(success);
    response->set_error_code(static_cast<int>(error_code));
    response->set_error_name(std::string(common::error::ToString(error_code)));
    response->set_error_message(error_message);
}

template <typename ProtoRequest, typename InvokeFn, typename BuildResponseFn>
common::net::Packet HandleParsedRequest(const HandlerContext& context,
                                        const common::net::Packet& packet,
                                        const std::string& invalid_message,
                                        InvokeFn&& invoke,
                                        BuildResponseFn&& build_response) {
    ProtoRequest request;
    common::net::Packet error_response;
    if (!ParseProtoRequest(context, packet, invalid_message, &request, &error_response)) {
        return error_response;
    }

    auto result = std::forward<InvokeFn>(invoke)(request);
    return std::forward<BuildResponseFn>(build_response)(context, result);
}

}  // namespace framework::protocol
