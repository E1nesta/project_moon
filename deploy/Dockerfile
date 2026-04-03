FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /src

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    protobuf-compiler \
    libprotobuf-dev \
    libboost-dev \
    libboost-system-dev \
    libspdlog-dev \
    default-libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY . .

RUN cmake -S . -B build && cmake --build build -j

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libprotobuf-dev \
    libboost-dev \
    libboost-system-dev \
    libspdlog-dev \
    default-libmysqlclient-dev \
    libhiredis-dev \
    libssl-dev \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/gateway_server /app/gateway_server
COPY --from=builder /src/build/login_server /app/login_server
COPY --from=builder /src/build/player_server /app/player_server
COPY --from=builder /src/build/dungeon_server /app/dungeon_server
COPY --from=builder /src/build/service_check /app/service_check
COPY --from=builder /src/build/password_tool /app/password_tool
COPY --from=builder /src/build/demo_flow /app/demo_flow
COPY --from=builder /src/build/demo_client /app/demo_client
COPY --from=builder /src/configs /app/configs

CMD ["./gateway_server", "--config", "configs/gateway_server.conf"]
