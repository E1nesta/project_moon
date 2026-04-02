FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /src

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    protobuf-compiler \
    libprotobuf-dev \
    default-libmysqlclient-dev \
    libhiredis-dev \
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
    default-libmysqlclient-dev \
    libhiredis-dev \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/gateway /app/gateway
COPY --from=builder /src/build/login_server /app/login_server
COPY --from=builder /src/build/game_server /app/game_server
COPY --from=builder /src/build/dungeon_server /app/dungeon_server
COPY --from=builder /src/build/demo_flow /app/demo_flow
COPY --from=builder /src/configs /app/configs

CMD ["./gateway", "--config", "configs/gateway_1.conf"]
