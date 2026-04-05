FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /src

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ccache \
    git \
    ninja-build \
    pkg-config \
    libboost-dev \
    libboost-system-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libprotobuf-dev \
    libgrpc++-dev \
    default-libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY . .

RUN cmake --preset dev-release -DBUILD_TESTING=OFF && cmake --build --preset dev-release --parallel --target \
    gateway_server \
    login_server \
    player_server \
    player_internal_grpc_server \
    battle_server \
    service_check \
    password_tool

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    protobuf-compiler \
    libprotobuf-dev \
    libgrpc++-dev \
    libboost-dev \
    libboost-system-dev \
    default-libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/dev-release/gateway_server /app/gateway_server
COPY --from=builder /src/build/dev-release/login_server /app/login_server
COPY --from=builder /src/build/dev-release/player_server /app/player_server
COPY --from=builder /src/build/dev-release/player_internal_grpc_server /app/player_internal_grpc_server
COPY --from=builder /src/build/dev-release/battle_server /app/battle_server
COPY --from=builder /src/build/dev-release/service_check /app/service_check
COPY --from=builder /src/build/dev-release/password_tool /app/password_tool
COPY --from=builder /src/configs /app/configs

CMD ["./gateway_server", "--config", "configs/gateway_server.conf"]
