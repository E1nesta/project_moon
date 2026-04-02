# Starter Kit One Pager

## What It Is
A lightweight mobile game backend starter kit focused on ingress scalability, session recovery, containerized deployment, and acceptance automation.

## Architecture
```mermaid
flowchart LR
  Client --> Nginx["Nginx stream ingress"]
  Nginx --> GW1["gateway_1"]
  Nginx --> GW2["gateway_2"]
  GW1 --> Login["login_server"]
  GW1 --> Game["game_server"]
  GW1 --> Dungeon["dungeon_server"]
  GW2 --> Login
  GW2 --> Game
  GW2 --> Dungeon
  Login --> MySQL[(MySQL)]
  Login --> Redis[(Redis)]
  Game --> MySQL
  Game --> Redis
  Dungeon --> MySQL
  Dungeon --> Redis
```

## Fault Recovery
```mermaid
sequenceDiagram
  participant Client
  participant Nginx
  participant GW1 as gateway_1
  participant GW2 as gateway_2
  participant Redis

  Client->>Nginx: login
  Nginx->>GW1: TCP stream
  GW1->>Redis: write session
  Client--xGW1: disconnect
  Client->>Nginx: LoadPlayer(session_id, player_id)
  Nginx->>GW2: TCP stream
  GW2->>Redis: validate session
  GW2-->>Client: restored binding + response
```

## Expandability
- Add more gateway instances behind the same ingress
- Replace demo seed data with customer-specific configuration
- Extend game and dungeon services without changing the ingress contract

## Exclusions
- No multi-tenant SaaS control plane
- No live-ops GM console in this version
- No guarantee for public Internet scale-out by default
