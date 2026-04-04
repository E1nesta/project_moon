#include "apps/login/login_server_app.h"

#include "runtime/protocol/proto_mapper.h"
#include "runtime/session/redis_session_store.h"
#include "runtime/protocol/adapter_utils.h"

#include "game_backend.pb.h"

namespace services::login {

namespace {

void FillProtoSession(const common::model::Session& session, game_backend::proto::LoginResponse* output) {
    if (output == nullptr) {
        return;
    }

    output->set_auth_token(session.session_id);
    output->set_account_id(session.account_id);
    output->set_player_id(session.player_id);
    output->set_expires_at_epoch_seconds(session.expires_at_epoch_seconds);
}

common::net::Packet BuildLoginResponsePacket(const framework::protocol::HandlerContext& context,
                                             const login_server::LoginResponse& result) {
    game_backend::proto::LoginResponse response;
    common::net::RequestContext response_context = context.request;
    response_context.auth_token = result.session.session_id;
    response_context.player_id = result.default_player_id;
    response_context.account_id = result.session.account_id;
    framework::protocol::FillResponseContext(response_context, &response);
    response.set_player_id(result.default_player_id);
    response.set_account_id(result.session.account_id);
    FillProtoSession(result.session, &response);

    return common::net::BuildPacket(
        common::net::MessageId::kLoginResponse, context.request.request_id, response);
}

}  // namespace

LoginServerApp::LoginServerApp()
    : framework::service::ServiceApp("login_server", "configs/login_server.conf") {}

bool LoginServerApp::BuildDependencies(std::string* error_message) {
    account_mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(Config(), "storage.account.mysql."),
        static_cast<std::size_t>(Config().GetInt("storage.account.mysql.pool_size", 4)));
    player_mysql_pool_ = std::make_unique<common::mysql::MySqlClientPool>(
        common::mysql::ReadConnectionOptions(Config(), "storage.player.mysql."),
        static_cast<std::size_t>(Config().GetInt("storage.player.mysql.pool_size", 4)));
    redis_pool_ = std::make_unique<common::redis::RedisClientPool>(
        common::redis::ReadConnectionOptions(Config(), "storage.account.redis."),
        static_cast<std::size_t>(Config().GetInt("storage.account.redis.pool_size", 4)));
    if (!account_mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!player_mysql_pool_->Initialize(error_message)) {
        return false;
    }
    if (!redis_pool_->Initialize(error_message)) {
        return false;
    }

    account_repository_ =
        std::make_unique<login_server::auth::MySqlAccountRepository>(*account_mysql_pool_, *player_mysql_pool_);
    session_repository_ = std::make_unique<common::session::RedisSessionStore>(
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
