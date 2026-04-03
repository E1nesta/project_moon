#include "runtime/foundation/config/simple_config.h"
#include "runtime/foundation/error/error_code.h"
#include "runtime/foundation/log/logger.h"
#include "runtime/observability/structured_log.h"
#include "runtime/storage/mysql/mysql_client.h"
#include "runtime/protocol/message_id.h"
#include "runtime/protocol/proto_codec.h"
#include "runtime/protocol/proto_mapper.h"
#include "runtime/storage/redis/redis_client.h"
#include "runtime/transport/transport_client.h"
#include "runtime/transport/tls_options.h"
#include "modules/login/infrastructure/mysql_account_repository.h"

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
    std::string config_profile = "local";
    std::string gateway_config = "configs/local/gateway_server.conf";
    std::string login_config = "configs/local/login_server.conf";
    std::string player_config = "configs/local/player_server.conf";
    std::string dungeon_config = "configs/local/dungeon_server.conf";
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

void EmitToolLog(common::log::LogLevel level, const framework::observability::LogEntry& entry);
framework::observability::LogEntry NewToolEvent(std::string_view event);

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
    if (value == "RATE_LIMITED") {
        return ErrorCode::kRateLimited;
    }
    if (value == "REQUEST_CONTEXT_INVALID") {
        return ErrorCode::kRequestContextInvalid;
    }
    if (value == "MESSAGE_NOT_SUPPORTED") {
        return ErrorCode::kMessageNotSupported;
    }
    if (value == "TRUSTED_GATEWAY_INVALID") {
        return ErrorCode::kTrustedGatewayInvalid;
    }
    if (value == "UPSTREAM_RESPONSE_INVALID") {
        return ErrorCode::kUpstreamResponseInvalid;
    }

    return std::nullopt;
}

DemoOptions ParseOptions(int argc, char* argv[]) {
    DemoOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--config-profile" && index + 1 < argc) {
            options.config_profile = argv[++index];
            if (options.config_profile == "demo") {
                options.gateway_config = "configs/demo/gateway_client.conf";
                options.login_config = "configs/demo/login_server.conf";
                options.player_config = "configs/demo/player_server.conf";
                options.dungeon_config = "configs/demo/dungeon_server.conf";
            } else if (options.config_profile == "delivery") {
                options.gateway_config = "configs/delivery/gateway_client.conf";
                options.login_config = "configs/delivery/login_server.conf";
                options.player_config = "configs/delivery/player_server.conf";
                options.dungeon_config = "configs/delivery/dungeon_server.conf";
            } else {
                options.config_profile = "local";
                options.gateway_config = "configs/local/gateway_server.conf";
                options.login_config = "configs/local/login_server.conf";
                options.player_config = "configs/local/player_server.conf";
                options.dungeon_config = "configs/local/dungeon_server.conf";
            }
        } else if (arg == "--gateway-config" && index + 1 < argc) {
            options.gateway_config = argv[++index];
        } else if (arg == "--login-config" && index + 1 < argc) {
            options.login_config = argv[++index];
        } else if ((arg == "--player-config" || arg == "--game-config") && index + 1 < argc) {
            options.player_config = argv[++index];
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

    auto entry = NewToolEvent("demo_client_config_load_failed");
    entry.Add("config_path", path);
    entry.Add("message", "failed to load config file");
    EmitToolLog(common::log::LogLevel::kError, entry);
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

void EmitToolLog(common::log::LogLevel level, const framework::observability::LogEntry& entry) {
    common::log::Logger::Instance().Log(level, entry.Build());
}

framework::observability::LogEntry NewToolEvent(std::string_view event) {
    framework::observability::LogEntry entry;
    entry.Add("event", event);
    return entry;
}

bool Require(bool condition, const std::string& message) {
    if (condition) {
        return true;
    }

    auto entry = NewToolEvent("demo_client_assertion_failed");
    entry.Add("message", message);
    EmitToolLog(common::log::LogLevel::kError, entry);
    return false;
}

bool RequireExpectedError(const CallResult& result,
                          common::error::ErrorCode expected_error,
                          const std::string& label) {
    if (result.ok) {
        auto entry = NewToolEvent("demo_client_expected_error_missing");
        entry.Add("operation", label);
        entry.Add("expected_error_code", std::string(common::error::ToString(expected_error)));
        EmitToolLog(common::log::LogLevel::kError, entry);
        return false;
    }

    if (result.error_code != expected_error) {
        auto entry = NewToolEvent("demo_client_expected_error_mismatch");
        entry.Add("operation", label);
        entry.Add("error_code", std::string(common::error::ToString(result.error_code)));
        entry.Add("expected_error_code", std::string(common::error::ToString(expected_error)));
        entry.Add("message", result.error_message);
        EmitToolLog(common::log::LogLevel::kError, entry);
        return false;
    }

    auto entry = NewToolEvent("demo_client_expected_error_observed");
    entry.Add("operation", label);
    entry.Add("error_code", std::string(common::error::ToString(expected_error)));
    EmitToolLog(common::log::LogLevel::kInfo, entry);
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
    auto entry = NewToolEvent("demo_client_rewards_observed");
    entry.Add("rewards", output.str().substr(std::string("rewards=").size()));
    EmitToolLog(common::log::LogLevel::kInfo, entry);
}

common::net::RequestContext BuildContext(std::uint64_t request_id,
                                         const std::string& session_id = "",
                                         std::int64_t player_id = 0) {
    common::net::RequestContext context;
    context.trace_id = "demo-client-" + std::to_string(request_id);
    context.request_id = request_id;
    context.auth_token = session_id;
    context.player_id = player_id;
    return context;
}

void ResetDemoState(common::mysql::MySqlClient& mysql_client,
                    common::redis::RedisClient& redis_client,
                    const common::config::SimpleConfig& player_config,
                    const common::config::SimpleConfig& dungeon_config,
                    std::int64_t account_id,
                    std::int64_t player_id) {
    const auto dungeon_id = dungeon_config.GetInt("demo.dungeon_id", 1001);

    mysql_client.Execute("DELETE FROM reward_log WHERE player_id = " + std::to_string(player_id));
    mysql_client.Execute("DELETE FROM dungeon_battle WHERE player_id = " + std::to_string(player_id));
    mysql_client.Execute("DELETE FROM player_dungeon WHERE player_id = " + std::to_string(player_id) +
                         " AND dungeon_id = " + std::to_string(dungeon_id));

    std::ostringstream asset_sql;
    asset_sql << "UPDATE player_asset SET stamina = " << player_config.GetInt("demo.stamina", 120)
              << ", gold = " << player_config.GetInt("demo.gold", 1000)
              << ", diamond = " << player_config.GetInt("demo.diamond", 100)
              << " WHERE player_id = " << player_id;
    mysql_client.Execute(asset_sql.str());

    const auto active_session_key = "account:session:" + std::to_string(account_id);
    if (const auto active_session = redis_client.Get(active_session_key); active_session.has_value()) {
        redis_client.Del("session:" + *active_session);
    }
    redis_client.Del(active_session_key);
    redis_client.Del("player:snapshot:" + std::to_string(player_id));
    redis_client.Del("player:lock:" + std::to_string(player_id));

    auto entry = NewToolEvent("demo_client_state_reset_completed");
    entry.Add("account_id", account_id);
    entry.Add("player_id", player_id);
    entry.Add("dungeon_id", static_cast<std::int64_t>(dungeon_id));
    EmitToolLog(common::log::LogLevel::kInfo, entry);
}

template <typename RequestT>
CallResult SendMessage(framework::transport::TransportClient& client,
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
    common::config::SimpleConfig player_config;
    common::config::SimpleConfig dungeon_config;
    if (!LoadConfig(options.gateway_config, gateway_config) ||
        !LoadConfig(options.login_config, login_config) ||
        !LoadConfig(options.player_config, player_config) ||
        !LoadConfig(options.dungeon_config, dungeon_config)) {
        return 1;
    }

    if (options.reset_demo_state) {
        common::mysql::MySqlClient mysql_client(common::mysql::ReadConnectionOptions(player_config));
        common::redis::RedisClient redis_client(common::redis::ReadConnectionOptions(player_config));
        std::string error_message;
        if (!mysql_client.Connect(&error_message)) {
            auto entry = NewToolEvent("demo_client_dependency_connect_failed");
            entry.Add("dependency", "mysql");
            entry.Add("message", error_message);
            EmitToolLog(common::log::LogLevel::kError, entry);
            return 1;
        }
        if (!redis_client.Connect(&error_message)) {
            auto entry = NewToolEvent("demo_client_dependency_connect_failed");
            entry.Add("dependency", "redis");
            entry.Add("message", error_message);
            EmitToolLog(common::log::LogLevel::kError, entry);
            return 1;
        }

        login_server::auth::MySqlAccountRepository account_repository(mysql_client);
        const auto demo_account = account_repository.FindByName(options.account_name);
        if (!Require(demo_account.has_value(), "demo account not found in mysql")) {
            return 1;
        }

        ResetDemoState(
            mysql_client, redis_client, player_config, dungeon_config, demo_account->account_id, demo_account->default_player_id);
    }

    const auto gateway_host = gateway_config.GetString("client.host", "127.0.0.1");
    const auto gateway_port = gateway_config.GetInt("port", 7000);
    const auto gateway_timeout_ms = gateway_config.GetInt("client.timeout_ms", 3000);
    auto gateway_tls = framework::transport::ReadTlsOptions(gateway_config, "client.tls.");
    if (gateway_tls.server_name.empty()) {
        gateway_tls.server_name = gateway_host;
    }

    framework::transport::TransportClient primary_client(gateway_host, gateway_port, gateway_timeout_ms, gateway_tls);
    framework::transport::TransportClient recovery_client(gateway_host, gateway_port, gateway_timeout_ms, gateway_tls);
    framework::transport::TransportClient* active_client = &primary_client;

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
        auto entry = NewToolEvent("demo_client_load_only_succeeded");
        entry.Add("player_id", load_response.player_state().profile().player_id());
        entry.Add("cache", load_response.loaded_from_cache() ? "hit" : "miss");
        EmitToolLog(common::log::LogLevel::kInfo, entry);
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
    auto login_entry = NewToolEvent("demo_client_login_succeeded");
    login_entry.Add("account_name", options.account_name);
    login_entry.Add("auth_token", framework::observability::MaskAuthToken(login_response.auth_token()));
    login_entry.Add("account_id", login_response.account_id());
    login_entry.Add("player_id", login_response.player_id());
    EmitToolLog(common::log::LogLevel::kInfo, login_entry);

    if (options.scenario == DemoScenario::kLoginOnly) {
        std::cout << "SESSION_ID=" << login_response.auth_token() << '\n';
        std::cout << "ACCOUNT_ID=" << login_response.account_id() << '\n';
        std::cout << "PLAYER_ID=" << login_response.player_id() << '\n';
        return 0;
    }

    game_backend::proto::LoadPlayerRequest load_request;
    load_request.set_player_id(login_response.player_id());
    if (options.verify_session_recovery) {
        const auto recovery_call = SendMessage(recovery_client,
                                               common::net::MessageId::kLoadPlayerRequest,
                                               BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
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
        auto recovery_entry = NewToolEvent("demo_client_session_recovery_succeeded");
        recovery_entry.Add("player_id", recovery_response.player_state().profile().player_id());
        recovery_entry.Add("cache", recovery_response.loaded_from_cache() ? "hit" : "miss");
        EmitToolLog(common::log::LogLevel::kInfo, recovery_entry);

        primary_client.Close();
        active_client = &recovery_client;
    }

    const auto load_call = SendMessage(*active_client,
                                       common::net::MessageId::kLoadPlayerRequest,
                                       BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
                                       &load_request);
    if (!Require(load_call.ok, "load player failed: " + FormatError(load_call.error_code, load_call.error_message))) {
        return 1;
    }

    game_backend::proto::LoadPlayerResponse load_response;
    if (!Require(common::net::ParseMessage(load_call.packet.body, &load_response), "failed to parse load player response")) {
        return 1;
    }
    auto player_entry = NewToolEvent("demo_client_load_player_succeeded");
    player_entry.Add("cache", load_response.loaded_from_cache() ? "hit" : "miss");
    player_entry.Add("player_id", load_response.player_state().profile().player_id());
    player_entry.Add("level", load_response.player_state().profile().level());
    player_entry.Add("stamina", load_response.player_state().profile().stamina());
    player_entry.Add("gold", load_response.player_state().profile().gold());
    player_entry.Add("diamond", load_response.player_state().profile().diamond());
    EmitToolLog(common::log::LogLevel::kInfo, player_entry);

    game_backend::proto::LoadPlayerRequest second_load_request;
    second_load_request.set_player_id(login_response.player_id());
    const auto second_load_call = SendMessage(*active_client,
                                              common::net::MessageId::kLoadPlayerRequest,
                                              BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
                                              &second_load_request);
    if (!Require(second_load_call.ok, "second load failed: " + FormatError(second_load_call.error_code, second_load_call.error_message))) {
        return 1;
    }

    game_backend::proto::LoadPlayerResponse second_load_response;
    if (!Require(common::net::ParseMessage(second_load_call.packet.body, &second_load_response),
                 "failed to parse second load player response")) {
        return 1;
    }
    if (!Require(second_load_response.loaded_from_cache(),
                 "second load should hit cache")) {
        return 1;
    }

    const auto dungeon_id = dungeon_config.GetInt("demo.dungeon_id", 1001);
    game_backend::proto::EnterDungeonRequest enter_request;
    enter_request.set_player_id(login_response.player_id());
    enter_request.set_dungeon_id(dungeon_id);
    const auto enter_call = SendMessage(*active_client,
                                        common::net::MessageId::kEnterDungeonRequest,
                                        BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
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
    auto enter_entry = NewToolEvent("demo_client_enter_dungeon_succeeded");
    enter_entry.Add("player_id", login_response.player_id());
    enter_entry.Add("dungeon_id", static_cast<std::int64_t>(dungeon_id));
    enter_entry.Add("battle_id", enter_response.battle_id());
    enter_entry.Add("remain_stamina", enter_response.remain_stamina());
    EmitToolLog(common::log::LogLevel::kInfo, enter_entry);

    game_backend::proto::SettleDungeonRequest settle_request;
    settle_request.set_player_id(login_response.player_id());
    settle_request.set_battle_id(enter_response.battle_id());
    settle_request.set_dungeon_id(dungeon_id);
    settle_request.set_star(3);
    settle_request.set_result(true);
    const auto settle_call = SendMessage(*active_client,
                                         common::net::MessageId::kSettleDungeonRequest,
                                         BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
                                         &settle_request);
    if (!Require(settle_call.ok, "settle dungeon failed: " + FormatError(settle_call.error_code, settle_call.error_message))) {
        return 1;
    }

    game_backend::proto::SettleDungeonResponse settle_response;
    if (!Require(common::net::ParseMessage(settle_call.packet.body, &settle_response), "failed to parse settle response")) {
        return 1;
    }
    auto settle_entry = NewToolEvent("demo_client_settle_dungeon_succeeded");
    settle_entry.Add("player_id", login_response.player_id());
    settle_entry.Add("dungeon_id", static_cast<std::int64_t>(dungeon_id));
    settle_entry.Add("battle_id", enter_response.battle_id());
    settle_entry.Add("first_clear", settle_response.first_clear() ? "true" : "false");
    EmitToolLog(common::log::LogLevel::kInfo, settle_entry);
    LogRewards(settle_response.rewards());

    game_backend::proto::LoadPlayerRequest refresh_request;
    refresh_request.set_player_id(login_response.player_id());
    const auto refresh_call = SendMessage(*active_client,
                                          common::net::MessageId::kLoadPlayerRequest,
                                          BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
                                          &refresh_request);
    if (!Require(refresh_call.ok, "reload player failed: " + FormatError(refresh_call.error_code, refresh_call.error_message))) {
        return 1;
    }

    game_backend::proto::LoadPlayerResponse refresh_response;
    if (!Require(common::net::ParseMessage(refresh_call.packet.body, &refresh_response), "failed to parse reload response")) {
        return 1;
    }
    auto refresh_entry = NewToolEvent("demo_client_reload_player_succeeded");
    refresh_entry.Add("player_id", login_response.player_id());
    refresh_entry.Add("stamina", refresh_response.player_state().profile().stamina());
    refresh_entry.Add("gold", refresh_response.player_state().profile().gold());
    refresh_entry.Add("diamond", refresh_response.player_state().profile().diamond());
    refresh_entry.Add("dungeon_progress_count", static_cast<std::int64_t>(refresh_response.player_state().dungeon_progress_size()));
    EmitToolLog(common::log::LogLevel::kInfo, refresh_entry);

    if (options.run_negative_cases) {
        game_backend::proto::LoginRequest wrong_password_request;
        wrong_password_request.set_account_name(options.account_name);
        wrong_password_request.set_password("bad-password");
        const auto wrong_password_call =
            SendMessage(*active_client,
                        common::net::MessageId::kLoginRequest,
                        BuildContext(request_id++),
                        &wrong_password_request);
        if (!RequireExpectedError(
                wrong_password_call, common::error::ErrorCode::kInvalidPassword, "wrong password request")) {
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
                                                       BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
                                                       &duplicate_settle_request);
        if (!RequireExpectedError(duplicate_settle_call,
                                  common::error::ErrorCode::kBattleAlreadySettled,
                                  "duplicate settle request")) {
            return 1;
        }

        game_backend::proto::EnterDungeonRequest invalid_star_enter_request;
        invalid_star_enter_request.set_player_id(login_response.player_id());
        invalid_star_enter_request.set_dungeon_id(dungeon_id);
        const auto invalid_star_enter_call = SendMessage(*active_client,
                                                         common::net::MessageId::kEnterDungeonRequest,
                                                         BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
                                                         &invalid_star_enter_request);
        if (!Require(invalid_star_enter_call.ok, "second enter for invalid-star case transport failed")) {
            return 1;
        }
        game_backend::proto::EnterDungeonResponse invalid_star_enter_response;
        if (!Require(common::net::ParseMessage(invalid_star_enter_call.packet.body, &invalid_star_enter_response),
                     "failed to parse invalid star enter response")) {
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
                                                          BuildContext(request_id++, login_response.auth_token(), login_response.player_id()),
                                                          &invalid_star_settle_request);
        if (!RequireExpectedError(
                invalid_star_settle_call, common::error::ErrorCode::kInvalidStar, "invalid star settle request")) {
            return 1;
        }
    }

    auto done_entry = NewToolEvent("demo_client_completed");
    done_entry.Add("account_name", options.account_name);
    EmitToolLog(common::log::LogLevel::kInfo, done_entry);
    return 0;
}
