#include "apps/dungeon/dungeon_server_app.h"

int main(int argc, char* argv[]) {
    services::dungeon::DungeonServerApp app;
    return app.Main(argc, argv);
}
