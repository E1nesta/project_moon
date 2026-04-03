#include "apps/player/player_server_app.h"

int main(int argc, char* argv[]) {
    services::player::PlayerServerApp app;
    return app.Main(argc, argv);
}
