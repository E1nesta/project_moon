#include "services/login/login_server_app.h"

int main(int argc, char* argv[]) {
    services::login::LoginServerApp app;
    return app.Main(argc, argv);
}
