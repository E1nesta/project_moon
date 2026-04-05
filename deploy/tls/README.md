Delivery and production TLS assets are expected under this directory.

Required files for the current deployment layout:

- `ca.pem`
- `gateway.crt`
- `gateway.key`
- `login_server.crt`
- `login_server.key`
- `player_server.crt`
- `player_server.key`
- `battle_server.crt`
- `battle_server.key`
- `player_internal_grpc_server.crt`
- `player_internal_grpc_server.key`

Notes:

- External TCP services use the existing transport TLS certificates named after each service.
- Internal `battle_server -> player_internal_grpc_server` gRPC calls reuse `battle_server.crt/key` as the mTLS client certificate.
- `player_internal_grpc_server` must present `player_internal_grpc_server.crt/key`, and that certificate should include a SAN for `player_internal_grpc_server`.
