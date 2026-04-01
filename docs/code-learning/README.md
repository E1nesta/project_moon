# 项目代码学习导读

这组文档面向“第一次读这套代码的人”，重点不是列完所有文件，而是帮你建立两条阅读主线：

1. `demo_flow`：理解业务规则、仓储分层和数据一致性
2. `demo_client -> gateway -> login/game/dungeon`：理解真实网络链路和进程协作

推荐阅读顺序：

1. [项目总 README](../../README.md)
2. [代码地图](./code-map.md)
3. [流程走读](./flow-walkthrough.md)
4. 再回到具体 `.cpp/.h` 文件精读

## 当前项目处于什么阶段

现在这套代码已经不是“只有学习骨架”的阶段，而是“双入口作品阶段”：

- 业务层已经接入真实 MySQL / Redis
- `gateway`、`login_server`、`game_server`、`dungeon_server` 都是可独立启动的 TCP 服务
- `demo_flow` 仍保留，用来做无网络干扰的业务回归
- `demo_client` 用来跑真实联调链路

## 最适合的阅读方式

### 路线 1：先读业务

先看：

1. [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)
2. [login_server/login_service.cpp](../../login_server/login_service.cpp)
3. [game_server/player/player_service.cpp](../../game_server/player/player_service.cpp)
4. [dungeon_server/dungeon/dungeon_service.cpp](../../dungeon_server/dungeon/dungeon_service.cpp)

这条路线最适合理解：

- 登录鉴权
- 角色加载缓存策略
- 进入副本扣体力
- 结算事务与重复领奖拦截

### 路线 2：再读系统

再看：

1. [gateway/gateway_server.cpp](../../gateway/gateway_server.cpp)
2. [login_server/login_network_server.cpp](../../login_server/login_network_server.cpp)
3. [game_server/game_network_server.cpp](../../game_server/game_network_server.cpp)
4. [dungeon_server/dungeon_network_server.cpp](../../dungeon_server/dungeon_network_server.cpp)
5. [common/include/common/net/message_id.h](../../common/include/common/net/message_id.h)
6. [proto/game_backend.proto](../../proto/game_backend.proto)

这条路线最适合理解：

- TCP/protobuf 包是怎么组织的
- `gateway` 怎么做路由和连接绑定
- 三个业务服怎么把 protobuf 请求映射到现有 service 层
- 为什么网络层和业务层仍然保持分离
