#include "services/login/login_server_app.h"

#include "common/net/proto_mapper.h"
#include "framework/protocol/adapter_utils.h"

#include "game_backend.pb.h"

namespace services::login {

namespace {

common::net::Packet BuildLoginResponsePacket(const framework::protocol::HandlerContext& context,
                                             const login_server::LoginResponse& result) {
    game_backend::proto::LoginResponse response;
    common::net::RequestContext response_context = context.request;
    response_context.session_id = result.session.session_id;
    response_context.player_id = result.default_player_id;
    framework::protocol::FillCommonResponseFields(
        response_context, result.success, result.error_code, result.error_message, &response);
    response.set_player_id(result.default_player_id);
    if (result.success) {
        common::net::FillProto(result.session, &response);
    }

    return common::net::BuildPacket(
        common::net::MessageId::kLoginResponse, context.request.request_id, response);
}

}  // namespace

LoginServerApp::LoginServerApp()
    : framework::service::ServiceApp("login_server", "configs/login_server.conf") {}

bool LoginServerApp::BuildDependencies(std::string* error_message) {
    mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(Config()),
        static_cast<std::size_t>(Config().GetInt("storage.mysql.pool_size", 4)));
    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config()),
        static_cast<std::size_t>(Config().GetInt("storage.redis.pool_size", 4)));
    if (!mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    account_repository_ = std::make_unique<login_server::auth::MySqlAccountRepository>(*mysql_pool_);
    session_repository_ = std::make_unique<login_server::session::RedisSessionRepository>(
        *redis_pool_, Config().GetInt("storage.session.ttl_seconds", 3600));
    login_service_ = std::make_unique<login_server::LoginService>(*account_repository_, *session_repository_);
    return true;
}

void LoginServerApp::RegisterRoutes() {
    Routes().Register(common::net::MessageId::kLoginRequest,
                      [this](const framework::protocol::HandlerContext& context, const common::net::Packet& packet) {
                          return HandleLoginRequest(context, packet);
                      });
}

common::net::Packet LoginServerApp::HandleLoginRequest(const framework::protocol::HandlerContext& context,
                                                       const common::net::Packet& packet) const {
    return framework::protocol::HandleParsedRequest<game_backend::proto::LoginRequest>(
        context,
        packet,
        "invalid login request",
        [this](const game_backend::proto::LoginRequest& request) {
            return login_service_->Login({request.account_name(), request.password()});
        },
        BuildLoginResponsePacket);
}

}  // namespace services::login
