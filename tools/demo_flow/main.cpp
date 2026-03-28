#include "common/config/simple_config.h"
#include "common/log/logger.h"
#include "game_server/player/in_memory_player_repository.h"
#include "game_server/player/player_service.h"
#include "login_server/auth/in_memory_account_repository.h"
#include "login_server/login_service.h"
#include "login_server/session/in_memory_session_repository.h"

#include <cstdint>
#include <sstream>
#include <string>

namespace {

struct DemoOptions {
    std::string login_config = "configs/login_server.conf";
    std::string game_config = "configs/game_server.conf";
    std::string account_name = "demo";
    std::string password = "demo123";
    std::int64_t player_id_override = 0;
};

DemoOptions ParseOptions(int argc, char* argv[]) {
    DemoOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--login-config" && index + 1 < argc) {
            options.login_config = argv[++index];
        } else if (arg == "--game-config" && index + 1 < argc) {
            options.game_config = argv[++index];
        } else if (arg == "--account" && index + 1 < argc) {
            options.account_name = argv[++index];
        } else if (arg == "--password" && index + 1 < argc) {
            options.password = argv[++index];
        } else if (arg == "--player-id" && index + 1 < argc) {
            options.player_id_override = std::stoll(argv[++index]);
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

}  // namespace

int main(int argc, char* argv[]) {
    common::log::Logger::Instance().SetServiceName("demo_flow");

    const auto options = ParseOptions(argc, argv);

    common::config::SimpleConfig login_config;
    common::config::SimpleConfig game_config;
    if (!LoadConfig(options.login_config, login_config) || !LoadConfig(options.game_config, game_config)) {
        return 1;
    }

    auto account_repository = login_server::auth::InMemoryAccountRepository::FromConfig(login_config);
    login_server::session::InMemorySessionRepository session_repository;
    login_server::LoginService login_service(account_repository, session_repository);

    const auto login_response = login_service.Login({options.account_name, options.password});
    if (!login_response.success) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError, "demo login failed: " + login_response.error_message);
        return 1;
    }

    std::ostringstream login_summary;
    login_summary << "login success, session_id=" << login_response.session.session_id
                  << ", account_id=" << login_response.session.account_id
                  << ", default_player_id=" << login_response.default_player_id;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, login_summary.str());

    auto player_repository = game_server::player::InMemoryPlayerRepository::FromConfig(game_config);
    game_server::player::PlayerService player_service(player_repository);

    const auto player_id = options.player_id_override > 0 ? options.player_id_override : login_response.default_player_id;
    const auto player_response = player_service.LoadPlayer(player_id);
    if (!player_response.success) {
        common::log::Logger::Instance().Log(common::log::LogLevel::kError, "demo player load failed: " + player_response.error_message);
        return 1;
    }

    std::ostringstream player_summary;
    player_summary << "player load success, player_id=" << player_response.player_profile.player_id
                   << ", name=" << player_response.player_profile.player_name
                   << ", level=" << player_response.player_profile.level
                   << ", stamina=" << player_response.player_profile.stamina
                   << ", gold=" << player_response.player_profile.gold
                   << ", diamond=" << player_response.player_profile.diamond;
    common::log::Logger::Instance().Log(common::log::LogLevel::kInfo, player_summary.str());

    return 0;
}
