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
    default-libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY . .

RUN ./scripts/setup_grpc_stack.sh
RUN cmake --preset grpc-stack-release && cmake --build --preset grpc-stack-release --parallel

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libprotobuf-dev \
    libboost-dev \
    libboost-system-dev \
    default-libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /root/.local/toolchains/grpc-stack /opt/grpc-stack
ENV LD_LIBRARY_PATH=/opt/grpc-stack/lib

COPY --from=builder /src/build/grpc-stack-release/gateway_server /app/gateway_server
COPY --from=builder /src/build/grpc-stack-release/login_server /app/login_server
COPY --from=builder /src/build/grpc-stack-release/player_server /app/player_server
COPY --from=builder /src/build/grpc-stack-release/player_internal_grpc_server /app/player_internal_grpc_server
COPY --from=builder /src/build/grpc-stack-release/dungeon_server /app/dungeon_server
COPY --from=builder /src/build/grpc-stack-release/service_check /app/service_check
COPY --from=builder /src/build/grpc-stack-release/password_tool /app/password_tool
COPY --from=builder /src/build/grpc-stack-release/demo_flow /app/demo_flow
COPY --from=builder /src/build/grpc-stack-release/demo_client /app/demo_client
COPY --from=builder /src/configs /app/configs

CMD ["./gateway_server", "--config", "configs/gateway_server.conf"]
