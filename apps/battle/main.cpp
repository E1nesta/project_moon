#include "apps/battle/battle_server_app.h"

int main(int argc, char* argv[]) {
    services::battle::BattleServerApp app;
    return app.Main(argc, argv);
}
