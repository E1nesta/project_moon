#include "apps/gateway/gateway_server_app.h"

int main(int argc, char* argv[]) {
    services::gateway::GatewayServerApp app;
    return app.Main(argc, argv);
}
