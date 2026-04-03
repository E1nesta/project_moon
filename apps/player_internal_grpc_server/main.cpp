#include "apps/player_internal_grpc_server/player_internal_grpc_server_app.h"

int main(int argc, char* argv[]) {
    services::player::PlayerInternalGrpcServerApp app(
        "player_internal_grpc_server", "configs/player_internal_grpc_server.conf");
    return app.Main(argc, argv);
}
