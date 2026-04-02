#include "common/config/simple_config.h"
#include "common/error/error_code.h"
#include "common/log/logger.h"
#include "common/mysql/mysql_client.h"
#include "common/net/message_id.h"
#include "common/net/proto_codec.h"
#include "common/net/proto_mapper.h"
#include "common/net/tcp_client.h"
#include "common/redis/redis_client.h"
#include "login_server/auth/mysql_account_repository.h"

#include "game_backend.pb.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class DemoScenario {
    kFull,
    kLoginOnly,
    kLoadOnly,
};

struct DemoOptions {
    std::string gateway_config = "configs/gateway.conf";
    std::string login_config = "configs/login_server.conf";
    std::string game_config = "configs/game_server.conf";
    std::string dungeon_config = "configs/dungeon_server.conf";
    std::string account_name = "demo";
    std::string password = "demo123";
    bool reset_demo_state = true;
    bool run_negative_cases = true;
    bool verify_session_recovery = true;
    DemoScenario scenario = DemoScenario::kFull;
    std::string session_id;
    std::int64_t player_id = 0;
    std::optional<common::error::ErrorCode> expected_load_error;
    std::optional<common::error::ErrorCode> expected_enter_error;
};

struct CallResult {
    bool ok = false;
    common::error::ErrorCode error_code = common::error::ErrorCode::kOk;
    std::string error_message;
    common::net::Packet packet;
};

std::optional<common::error::ErrorCode> ParseErrorCode(std::string_view value) {
    using common::error::ErrorCode;

    if (value == "OK") {
        return ErrorCode::kOk;
    }
    if (value == "ACCOUNT_NOT_FOUND") {
        return ErrorCode::kAccountNotFound;
    }
    if (value == "ACCOUNT_DISABLED") {
        return ErrorCode::kAccountDisabled;
    }
    if (value == "INVALID_PASSWORD") {
        return ErrorCode::kInvalidPassword;
    }
    if (value == "SESSION_INVALID") {
        return ErrorCode::kSessionInvalid;
    }
    if (value == "PLAYER_NOT_FOUND") {
        return ErrorCode::kPlayerNotFound;
    }
    if (value == "PLAYER_BUSY") {
        return ErrorCode::kPlayerBusy;
    }
    if (value == "DUNGEON_NOT_FOUND") {
        return ErrorCode::kDungeonNotFound;
    }
    if (value == "DUNGEON_LOCKED") {
        return ErrorCode::kDungeonLocked;
    }
    if (value == "STAMINA_NOT_ENOUGH") {
        return ErrorCode::kStaminaNotEnough;
    }
    if (value == "BATTLE_NOT_FOUND") {
        return ErrorCode::kBattleNotFound;
    }
    if (value == "BATTLE_MISMATCH") {
        return ErrorCode::kBattleMismatch;
    }
    if (value == "BATTLE_ALREADY_SETTLED") {
        return ErrorCode::kBattleAlreadySettled;
    }
    if (value == "INVALID_DUNGEON_ID") {
        return ErrorCode::kInvalidDungeonId;
    }
    if (value == "INVALID_STAR") {
        return ErrorCode::kInvalidStar;
    }
    if (value == "STORAGE_ERROR") {
        return ErrorCode::kStorageError;
    }
    if (value == "SERVICE_UNAVAILABLE") {
        return ErrorCode::kServiceUnavailable;
    }
    if (value == "UPSTREAM_TIMEOUT") {
        return ErrorCode::kUpstreamTimeout;
    }
    if (value == "BAD_GATEWAY") {
        return ErrorCode::kBadGateway;
    }

    return std::nullopt;
}

DemoOptions ParseOptions(int argc, char* argv[]) {
    DemoOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--gateway-config" && index + 1 < argc) {
            options.gateway_config = argv[++index];
        } else if (arg == "--login-config" && index + 1 < argc) {
            options.login_config = argv[++index];
        } else if (arg == "--game-config" && index + 1 < argc) {
            options.game_config = argv[++index];
        } else if (arg == "--dungeon-config" && index + 1 < argc) {
            options.dungeon_config = argv[++index];
        } else if (arg == "--account" && index + 1 < argc) {
            options.account_name = argv[++index];
        } else if (arg == "--password" && index + 1 < argc) {
            options.password = argv[++index];
        } else if (arg == "--no-reset") {
            options.reset_demo_state = false;
        } else if (arg == "--happy-path-only") {
            options.run_negative_cases = false;
        } else if (arg == "--skip-session-recovery") {
            options.verify_session_recovery = false;
        } else if (arg == "--scenario" && index + 1 < argc) {
            const std::string value = argv[++index];
            if (value == "full") {
                options.scenario = DemoScenario::kFull;
            } else if (value == "login-only") {
                options.scenario = DemoScenario::kLoginOnly;
                options.run_negative_cases = false;
                options.verify_session_recovery = false;
            } else if (value == "load-only") {
                options.scenario = DemoScenario::kLoadOnly;
                options.run_negative_cases = false;
                options.verify_session_recovery = false;
                options.reset_demo_state = false;
            }
        } else if (arg == "--session-id" && index + 1 < argc) {
            options.session_id = argv[++index];
        } else if (arg == "--player-id" && index + 1 < argc) {
            options.player_id = std::stoll(argv[++index]);
        } else if (arg == "--expect-load-error" && index + 1 < argc) {
            options.expected_load_error = ParseErrorCode(argv[++index]);
        } else if (arg == "--expect-enter-error" && index + 1 < argc) {
            options.expected_enter_error = ParseErrorCode(argv[++index]);
        }
    }

    return options;
}

bool LoadConfig(const std::string& path, common::config::SimpleConfig& config) {
    if (config.LoadFromFile(path)) {
        return true;
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kError, "failed to load config file: " + path);
    return false;
}

std::string FormatError(common::error::ErrorCode code, const std::string& message) {
    std::ostringstream output;
    output << "error_code=" << common::error::ToString(code);
    if (!message.empty()) {
        output << ", message=" << message;
    }
    return output.str();
}

bool Require(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kError, message);
    return false;
}

bool RequireExpectedError(const CallResult& result,
                          common::error::ErrorCode expected_error,
                          const std::string& label) {
    if (result.ok) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError,
                                            label + " should fail with " +
                                                std::string(common::error::ToString(expected_error)));
        return false;
    }

    if (result.error_code != expected_error) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError,
                                            label + " failed with " + FormatError(result.error_code, result.error_message) +
                                                ", expected error_code=" + std::string(common::error::ToString(expected_error)));
        return false;
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo,
                                        label + " produced expected error_code=" +
                                            std::string(common::error::ToString(expected_error)));
    return true;
}

void LogRewards(const google::protobuf::RepeatedPtrField<game_backend::proto::Reward>& rewards) {
    std::ostringstream output;
    output << "rewards=";
    bool first = true;
    for (const auto& reward : rewards) {
        if (!first) {
            output << ';';
        }
        output << reward.reward_type() << ':' << reward.amount();
        first = false;
    }
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, output.str());
}

common::net::RequestContext BuildContext(std::uint64_t request_id,
                                         const std::string& session_id = "",
                                         std::int64_t player_id = 0) {
    common::net::RequestContext context;
    context.trace_id = "demo-client-" + std::to_string(request_id);
    context.request_id = request_id;
    context.session_id = session_id;
    context.player_id = player_id;
    return context;
}

void ResetDemoState(common::mysql::MySqlClient& mysql_client,
                    common::redis::RedisClient& redis_client,
                    const common::config::SimpleConfig& game_config,
                    const common::config::SimpleConfig& dungeon_config,
                    std::int64_t account_id,
                    std::int64_t player_id) {
    const auto dungeon_id = dungeon_config.GetInt("demo.dungeon_id", 1001);

    mysql_client.Execute("DELETE FROM reward_log WHERE player_id = " + std::to_string(player_id));
    mysql_client.Execute("DELETE FROM dungeon_battle WHERE player_id = " + std::to_string(player_id));
    mysql_client.Execute("DELETE FROM player_dungeon WHERE player_id = " + std::to_string(player_id) +
                         " AND dungeon_id = " + std::to_string(dungeon_id));

    std::ostringstream asset_sql;
    asset_sql << "UPDATE player_asset SET stamina = " << game_config.GetInt("demo.stamina", 120)
              << ", gold = " << game_config.GetInt("demo.gold", 1000)
              << ", diamond = " << game_config.GetInt("demo.diamond", 100)
              << " WHERE player_id = " << player_id;
    mysql_client.Execute(asset_sql.str());

    const auto active_session_key = "account:session:" + std::to_string(account_id);
    if (const auto active_session = redis_client.Get(active_session_key); active_session.has_value()) {
        redis_client.Del("session:" + *active_session);
    }
    redis_client.Del(active_session_key);
    redis_client.Del("player:snapshot:" + std::to_string(player_id));
    redis_client.Del("player:lock:" + std::to_string(player_id));

    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, "demo state reset complete");
}

template <typename RequestT>
CallResult SendMessage(common::net::PersistentTcpClient& client,
                       common::net::MessageId message_id,
                       const common::net::RequestContext& context,
                       RequestT* request) {
    common::net::FillProto(context, request->mutable_context());
    const auto packet = common::net::BuildPacket(message_id, context.request_id, *request);

    CallResult result;
    std::string error_message;
    if (!client.SendAndReceive(packet, &result.packet, &error_message)) {
        result.ok = false;
        result.error_code = error_message == "timeout" ? common::error::ErrorCode::kUpstreamTimeout
                                                        : common::error::ErrorCode::kServiceUnavailable;
        result.error_message = error_message;
        return result;
    }

    const auto maybe_response_id = common::net::MessageIdFromInt(result.packet.header.msg_id);
    if (!maybe_response_id.has_value()) {
        result.ok = false;
        result.error_code = common::error::ErrorCode::kBadGateway;
        result.error_message = "unknown response message id";
        return result;
    }

    if (*maybe_response_id == common::net::MessageId::kErrorResponse) {
        game_backend::proto::ErrorResponse error_response;
        if (!common::net::ParseMessage(result.packet.body, &error_response)) {
            result.ok = false;
            result.error_code = common::error::ErrorCode::kBadGateway;
            result.error_message = "failed to parse error response";
            return result;
        }

        result.ok = false;
        result.error_code = static_cast<common::error::ErrorCode>(error_response.error_code());
        result.error_message = error_response.error_message();
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    common::log::Logger::Instance().SetServiceName("demo_client");

    const auto options = ParseOptions(argc, argv);

    common::config::SimpleConfig gateway_config;
    common::config::SimpleConfig login_config;
    common::config::SimpleConfig game_config;
    common::config::SimpleConfig dungeon_config;
    if (!LoadConfig(options.gateway_config, gateway_config) ||
        !LoadConfig(options.login_config, login_config) ||
        !LoadConfig(options.game_config, game_config) ||
        !LoadConfig(options.dungeon_config, dungeon_config)) {
        return 1;
    }

    if (options.reset_demo_state) {
        common::mysql::MySqlClient mysql_client(common::mysql::ReadConnectionOptions(game_config));
        common::redis::RedisClient redis_client(common::redis::ReadConnectionOptions(game_config));
        std::string error_message;
        if (!mysql_client.Connect(&error_message)) {
            common::log::Logger::Instance().Log(common::log::LogLevel::kError, "mysql connect failed: " + error_message);
            return 1;
        }
        if (!redis_client.Connect(&error_message)) {
            common::log::Logger::Instance().Log(common::log::LogLevel::kError, "redis connect failed: " + error_message);
            return 1;
        }

        login_server::auth::MySqlAccountRepository account_repository(mysql_client);
        const auto demo_account = account_repository.FindByName(options.account_name);
        if (!Require(demo_account.has_value(), "demo account not found in mysql")) {
            return 1;
        }

        ResetDemoState(
            mysql_client, redis_client, game_config, dungeon_config, demo_account->account_id, demo_account->default_player_id);
    }

    const auto gateway_host = gateway_config.GetString("client.host", "127.0.0.1");
    const auto gateway_port = gateway_config.GetInt("port", 7000);
    const auto gateway_timeout_ms = gateway_config.GetInt("client.timeout_ms", 3000);

    common::net::PersistentTcpClient primary_client(gateway_host, gateway_port, gateway_timeout_ms);
    common::net::PersistentTcpClient recovery_client(gateway_host, gateway_port, gateway_timeout_ms);
    common::net::PersistentTcpClient* active_client = &primary_client;

    std::uint64_t request_id = 1;

    if (options.scenario == DemoScenario::kLoadOnly) {
        if (!Require(!options.session_id.empty(), "load-only scenario requires --session-id")) {
            return 1;
        }
        if (!Require(options.player_id != 0, "load-only scenario requires --player-id")) {
            return 1;
        }

        game_backend::proto::LoadPlayerRequest load_request;
        load_request.set_player_id(options.player_id);
        const auto load_call = SendMessage(primary_client,
                                           common::net::MessageId::kLoadPlayerRequest,
                                           BuildContext(request_id++, options.session_id, options.player_id),
                                           &load_request);
        if (options.expected_load_error.has_value()) {
            return RequireExpectedError(load_call, *options.expected_load_error, "load-only request") ? 0 : 1;
        }
        if (!Require(load_call.ok, "load-only request failed: " + FormatError(load_call.error_code, load_call.error_message))) {
            return 1;
        }

        game_backend::proto::LoadPlayerResponse load_response;
        if (!Require(common::net::ParseMessage(load_call.packet.body, &load_response),
                     "failed to parse load-only response")) {
            return 1;
        }
        if (!Require(load_response.success(),
                     "load-only request failed: " +
                         FormatError(static_cast<common::error::ErrorCode>(load_response.error_code()),
                                     load_response.error_message()))) {
            return 1;
        }

        std::ostringstream summary;
        summary << "load-only success"
                << ", player_id=" << load_response.player_state().profile().player_id()
                << ", cache=" << (load_response.loaded_from_cache() ? "hit" : "miss");
        common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, summary.str());
        return 0;
    }

    game_backend::proto::LoginRequest login_request;
    login_request.set_account_name(options.account_name);
    login_request.set_password(options.password);
    const auto login_call =
        SendMessage(primary_client, common::net::MessageId::kLoginRequest, BuildContext(request_id++), &login_request);
    if (!Require(login_call.ok, "login failed: " + FormatError(login_call.error_code, login_call.error_message))) {
        return 1;
    }

    game_backend::proto::LoginResponse login_response;
    if (!Require(common::net::ParseMessage(login_call.packet.body, &login_response), "failed to parse login response")) {
        return 1;
    }
    if (!Require(login_response.success(),
                 "login failed: " +
                     FormatError(static_cast<common::error::ErrorCode>(login_response.error_code()),
                                 login_response.error_message()))) {
        return 1;
    }

    std::ostringstream login_summary;
    login_summary << "login success"
                  << ", session_id=" << login_response.session_id()
                  << ", account_id=" << login_response.account_id()
                  << ", player_id=" << login_response.player_id();
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, login_summary.str());

    if (options.scenario == DemoScenario::kLoginOnly) {
        std::cout << "SESSION_ID=" << login_response.session_id() << '\n';
        std::cout << "ACCOUNT_ID=" << login_response.account_id() << '\n';
        std::cout << "PLAYER_ID=" << login_response.player_id() << '\n';
        return 0;
    }

    game_backend::proto::LoadPlayerRequest load_request;
    load_request.set_player_id(login_response.player_id());
    if (options.verify_session_recovery) {
        const auto recovery_call = SendMessage(recovery_client,
                                               common::net::MessageId::kLoadPlayerRequest,
                                               BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                               &load_request);
        if (!Require(recovery_call.ok,
                     "session recovery load failed: " +
                         FormatError(recovery_call.error_code, recovery_call.error_message))) {
            return 1;
        }

        game_backend::proto::LoadPlayerResponse recovery_response;
        if (!Require(common::net::ParseMessage(recovery_call.packet.body, &recovery_response),
                     "failed to parse session recovery load response")) {
            return 1;
        }
        if (!Require(recovery_response.success(),
                     "session recovery load failed: " +
                         FormatError(static_cast<common::error::ErrorCode>(recovery_response.error_code()),
                                     recovery_response.error_message()))) {
            return 1;
        }

        std::ostringstream recovery_summary;
        recovery_summary << "session recovery success"
                         << ", player_id=" << recovery_response.player_state().profile().player_id()
                         << ", cache=" << (recovery_response.loaded_from_cache() ? "hit" : "miss");
        common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, recovery_summary.str());

        primary_client.Close();
        active_client = &recovery_client;
    }

    const auto load_call = SendMessage(*active_client,
                                       common::net::MessageId::kLoadPlayerRequest,
                                       BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                       &load_request);
    if (!Require(load_call.ok, "load player failed: " + FormatError(load_call.error_code, load_call.error_message))) {
        return 1;
    }

    game_backend::proto::LoadPlayerResponse load_response;
    if (!Require(common::net::ParseMessage(load_call.packet.body, &load_response), "failed to parse load player response")) {
        return 1;
    }
    if (!Require(load_response.success(),
                 "load player failed: " +
                     FormatError(static_cast<common::error::ErrorCode>(load_response.error_code()),
                                 load_response.error_message()))) {
        return 1;
    }

    std::ostringstream player_summary;
    player_summary << "load player success"
                   << ", cache=" << (load_response.loaded_from_cache() ? "hit" : "miss")
                   << ", player_id=" << load_response.player_state().profile().player_id()
                   << ", level=" << load_response.player_state().profile().level()
                   << ", stamina=" << load_response.player_state().profile().stamina()
                   << ", gold=" << load_response.player_state().profile().gold()
                   << ", diamond=" << load_response.player_state().profile().diamond();
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, player_summary.str());

    game_backend::proto::LoadPlayerRequest second_load_request;
    second_load_request.set_player_id(login_response.player_id());
    const auto second_load_call = SendMessage(*active_client,
                                              common::net::MessageId::kLoadPlayerRequest,
                                              BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                              &second_load_request);
    if (!Require(second_load_call.ok, "second load failed: " + FormatError(second_load_call.error_code, second_load_call.error_message))) {
        return 1;
    }

    game_backend::proto::LoadPlayerResponse second_load_response;
    if (!Require(common::net::ParseMessage(second_load_call.packet.body, &second_load_response),
                 "failed to parse second load player response")) {
        return 1;
    }
    if (!Require(second_load_response.success() && second_load_response.loaded_from_cache(),
                 "second load should hit cache")) {
        return 1;
    }

    const auto dungeon_id = dungeon_config.GetInt("demo.dungeon_id", 1001);
    game_backend::proto::EnterDungeonRequest enter_request;
    enter_request.set_player_id(login_response.player_id());
    enter_request.set_dungeon_id(dungeon_id);
    const auto enter_call = SendMessage(*active_client,
                                        common::net::MessageId::kEnterDungeonRequest,
                                        BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                        &enter_request);
    if (options.expected_enter_error.has_value()) {
        return RequireExpectedError(enter_call, *options.expected_enter_error, "enter dungeon request") ? 0 : 1;
    }
    if (!Require(enter_call.ok, "enter dungeon failed: " + FormatError(enter_call.error_code, enter_call.error_message))) {
        return 1;
    }

    game_backend::proto::EnterDungeonResponse enter_response;
    if (!Require(common::net::ParseMessage(enter_call.packet.body, &enter_response), "failed to parse enter dungeon response")) {
        return 1;
    }
    if (!Require(enter_response.success(),
                 "enter dungeon failed: " +
                     FormatError(static_cast<common::error::ErrorCode>(enter_response.error_code()),
                                 enter_response.error_message()))) {
        return 1;
    }

    std::ostringstream enter_summary;
    enter_summary << "enter dungeon success"
                  << ", battle_id=" << enter_response.battle_id()
                  << ", remain_stamina=" << enter_response.remain_stamina();
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, enter_summary.str());

    game_backend::proto::SettleDungeonRequest settle_request;
    settle_request.set_player_id(login_response.player_id());
    settle_request.set_battle_id(enter_response.battle_id());
    settle_request.set_dungeon_id(dungeon_id);
    settle_request.set_star(3);
    settle_request.set_result(true);
    const auto settle_call = SendMessage(*active_client,
                                         common::net::MessageId::kSettleDungeonRequest,
                                         BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                         &settle_request);
    if (!Require(settle_call.ok, "settle dungeon failed: " + FormatError(settle_call.error_code, settle_call.error_message))) {
        return 1;
    }

    game_backend::proto::SettleDungeonResponse settle_response;
    if (!Require(common::net::ParseMessage(settle_call.packet.body, &settle_response), "failed to parse settle response")) {
        return 1;
    }
    if (!Require(settle_response.success(),
                 "settle dungeon failed: " +
                     FormatError(static_cast<common::error::ErrorCode>(settle_response.error_code()),
                                 settle_response.error_message()))) {
        return 1;
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo,
                                        "settle dungeon success, first_clear=" +
                                            std::string(settle_response.first_clear() ? "true" : "false"));
    LogRewards(settle_response.rewards());

    game_backend::proto::LoadPlayerRequest refresh_request;
    refresh_request.set_player_id(login_response.player_id());
    const auto refresh_call = SendMessage(*active_client,
                                          common::net::MessageId::kLoadPlayerRequest,
                                          BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                          &refresh_request);
    if (!Require(refresh_call.ok, "reload player failed: " + FormatError(refresh_call.error_code, refresh_call.error_message))) {
        return 1;
    }

    game_backend::proto::LoadPlayerResponse refresh_response;
    if (!Require(common::net::ParseMessage(refresh_call.packet.body, &refresh_response), "failed to parse reload response")) {
        return 1;
    }
    if (!Require(refresh_response.success(), "reload player after settlement failed")) {
        return 1;
    }

    std::ostringstream refresh_summary;
    refresh_summary << "reload player success"
                    << ", stamina=" << refresh_response.player_state().profile().stamina()
                    << ", gold=" << refresh_response.player_state().profile().gold()
                    << ", diamond=" << refresh_response.player_state().profile().diamond()
                    << ", dungeon_progress_count=" << refresh_response.player_state().dungeon_progress_size();
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, refresh_summary.str());

    if (options.run_negative_cases) {
        game_backend::proto::LoginRequest wrong_password_request;
        wrong_password_request.set_account_name(options.account_name);
        wrong_password_request.set_password("bad-password");
        const auto wrong_password_call =
            SendMessage(*active_client,
                        common::net::MessageId::kLoginRequest,
                        BuildContext(request_id++),
                        &wrong_password_request);
        if (!Require(wrong_password_call.ok, "wrong password request transport failed")) {
            return 1;
        }
        game_backend::proto::LoginResponse wrong_password_response;
        if (!Require(common::net::ParseMessage(wrong_password_call.packet.body, &wrong_password_response),
                     "failed to parse wrong password response")) {
            return 1;
        }
        if (!Require(!wrong_password_response.success() &&
                         static_cast<common::error::ErrorCode>(wrong_password_response.error_code()) ==
                             common::error::ErrorCode::kInvalidPassword,
                     "invalid password case should be rejected")) {
            return 1;
        }

        game_backend::proto::LoadPlayerRequest invalid_session_request;
        invalid_session_request.set_player_id(login_response.player_id());
        const auto invalid_session_call = SendMessage(*active_client,
                                                      common::net::MessageId::kLoadPlayerRequest,
                                                      BuildContext(request_id++, "invalid-session", login_response.player_id()),
                                                      &invalid_session_request);
        if (!Require(!invalid_session_call.ok &&
                         invalid_session_call.error_code == common::error::ErrorCode::kSessionInvalid,
                     "invalid session should be rejected")) {
            return 1;
        }

        game_backend::proto::SettleDungeonRequest duplicate_settle_request;
        duplicate_settle_request.set_player_id(login_response.player_id());
        duplicate_settle_request.set_battle_id(enter_response.battle_id());
        duplicate_settle_request.set_dungeon_id(dungeon_id);
        duplicate_settle_request.set_star(3);
        duplicate_settle_request.set_result(true);
        const auto duplicate_settle_call = SendMessage(*active_client,
                                                       common::net::MessageId::kSettleDungeonRequest,
                                                       BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                                       &duplicate_settle_request);
        if (!Require(duplicate_settle_call.ok, "duplicate settle transport failed")) {
            return 1;
        }
        game_backend::proto::SettleDungeonResponse duplicate_settle_response;
        if (!Require(common::net::ParseMessage(duplicate_settle_call.packet.body, &duplicate_settle_response),
                     "failed to parse duplicate settle response")) {
            return 1;
        }
        if (!Require(!duplicate_settle_response.success() &&
                         static_cast<common::error::ErrorCode>(duplicate_settle_response.error_code()) ==
                             common::error::ErrorCode::kBattleAlreadySettled,
                     "duplicate settlement should be rejected")) {
            return 1;
        }

        game_backend::proto::EnterDungeonRequest invalid_star_enter_request;
        invalid_star_enter_request.set_player_id(login_response.player_id());
        invalid_star_enter_request.set_dungeon_id(dungeon_id);
        const auto invalid_star_enter_call = SendMessage(*active_client,
                                                         common::net::MessageId::kEnterDungeonRequest,
                                                         BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                                         &invalid_star_enter_request);
        if (!Require(invalid_star_enter_call.ok, "second enter for invalid-star case transport failed")) {
            return 1;
        }
        game_backend::proto::EnterDungeonResponse invalid_star_enter_response;
        if (!Require(common::net::ParseMessage(invalid_star_enter_call.packet.body, &invalid_star_enter_response),
                     "failed to parse invalid star enter response")) {
            return 1;
        }
        if (!Require(invalid_star_enter_response.success(), "second enter for invalid-star case should succeed")) {
            return 1;
        }

        game_backend::proto::SettleDungeonRequest invalid_star_settle_request;
        invalid_star_settle_request.set_player_id(login_response.player_id());
        invalid_star_settle_request.set_battle_id(invalid_star_enter_response.battle_id());
        invalid_star_settle_request.set_dungeon_id(dungeon_id);
        invalid_star_settle_request.set_star(99);
        invalid_star_settle_request.set_result(true);
        const auto invalid_star_settle_call = SendMessage(*active_client,
                                                          common::net::MessageId::kSettleDungeonRequest,
                                                          BuildContext(request_id++, login_response.session_id(), login_response.player_id()),
                                                          &invalid_star_settle_request);
        if (!Require(invalid_star_settle_call.ok, "invalid star settle transport failed")) {
            return 1;
        }
        game_backend::proto::SettleDungeonResponse invalid_star_settle_response;
        if (!Require(common::net::ParseMessage(invalid_star_settle_call.packet.body, &invalid_star_settle_response),
                     "failed to parse invalid star settle response")) {
            return 1;
        }
        if (!Require(!invalid_star_settle_response.success() &&
                         static_cast<common::error::ErrorCode>(invalid_star_settle_response.error_code()) ==
                             common::error::ErrorCode::kInvalidStar,
                     "invalid star should be rejected")) {
            return 1;
        }
    }

    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, "demo client flow completed successfully");
    return 0;
}
