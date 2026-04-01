# 代码地图

这份文档回答一个问题：

**当前项目里，每个目录和关键文件分别负责什么？**

## 顶层目录

### [CMakeLists.txt](../../CMakeLists.txt)

工程装配入口。

现在它除了原有 `common / gateway / login_server / game_server / dungeon_server / demo_flow` 之外，还接入了：

- protobuf 代码生成
- 网络层公共代码
- `demo_client`

### [README.md](../../README.md)

项目说明入口。

重点看两类运行方式：

- 离线回归：`demo_flow`
- 真实联调：`demo_client + gateway + 三服`

### [configs/](../../configs)

配置目录。

关键点：

- `gateway.conf` 现在除了监听端口，还包含上游服务地址和客户端超时配置
- 三个业务服配置仍同时承载 MySQL / Redis 连接参数和 demo 数据参数

### [proto/](../../proto)

协议定义目录。

- [game_backend.proto](../../proto/game_backend.proto)

这里定义了登录、角色加载、进入副本、结算、错误响应和心跳的 protobuf 消息。

## 公共层：`common`

### 配置与日志

- [simple_config.cpp](../../common/src/config/simple_config.cpp)
- [logger.cpp](../../common/src/log/logger.cpp)
- [service_app.cpp](../../common/src/bootstrap/service_app.cpp)

这部分还是整个工程的基础设施：配置加载、日志和 `--check` 启动骨架。

### 数据库与缓存

- [mysql_client.cpp](../../common/src/mysql/mysql_client.cpp)
- [redis_client.cpp](../../common/src/redis/redis_client.cpp)

MySQL 和 Redis 的最小封装，供所有服务复用。

### 网络层

- [message_id.h](../../common/include/common/net/message_id.h)
- [packet.h](../../common/include/common/net/packet.h)
- [tcp_server.cpp](../../common/src/net/tcp_server.cpp)
- [tcp_client.cpp](../../common/src/net/tcp_client.cpp)
- [proto_mapper.cpp](../../common/src/net/proto_mapper.cpp)

这一层负责：

- 自定义包头
- epoll TCP server
- 长连接请求-响应 client
- protobuf 与领域对象的基础映射

### 公共模型

`common/include/common/model/` 下放的是业务层共享的数据结构，例如：

- `Session`
- `PlayerProfile`
- `PlayerState`
- `Reward`
- `BattleContext`

## Gateway

### [gateway/gateway_server.cpp](../../gateway/gateway_server.cpp)

当前网关的真实实现。

它负责：

- 接收客户端 TCP 请求
- 根据 `msg_id` 路由到不同业务服
- 维护 `connection_id -> session_id -> player_id` 绑定
- 把上游不可用、超时、非法响应映射为统一错误

### [gateway/main.cpp](../../gateway/main.cpp)

网关进程入口。

`--check` 仍走公共启动骨架；正常启动时会加载配置并启动真实 TCP 服务。

## 登录服：`login_server`

### [login_service.cpp](../../login_server/login_service.cpp)

真正的登录业务规则。

### [login_network_server.cpp](../../login_server/login_network_server.cpp)

登录服网络包装层：

- 解析 protobuf 登录请求
- 调 `LoginService`
- 把结果转成 protobuf 响应

### 仓储

- [mysql_account_repository.cpp](../../login_server/auth/mysql_account_repository.cpp)
- [redis_session_repository.cpp](../../login_server/session/redis_session_repository.cpp)

登录服现在默认走真实 MySQL / Redis，而不是内存仓储。

## 游戏服：`game_server`

### [player_service.cpp](../../game_server/player/player_service.cpp)

角色加载核心逻辑：

- 校验 session
- 先读 Redis 快照
- miss 再回源 MySQL

### [game_network_server.cpp](../../game_server/game_network_server.cpp)

游戏服网络包装层，只负责协议映射和调用 `PlayerService`。

### 仓储

- [mysql_player_repository.cpp](../../game_server/player/mysql_player_repository.cpp)
- [redis_player_cache_repository.cpp](../../game_server/player/redis_player_cache_repository.cpp)

## 副本服：`dungeon_server`

### [dungeon_service.cpp](../../dungeon_server/dungeon/dungeon_service.cpp)

当前最核心的业务代码之一：

- 进入副本前拿玩家锁
- 校验等级、体力、副本配置
- 结算前校验 `battle_id / player_id / dungeon_id`
- 结算成功后失效玩家快照和 battle context

### [mysql_dungeon_repository.cpp](../../dungeon_server/dungeon/mysql_dungeon_repository.cpp)

副本进入和结算的事务落库核心：

- 进入时扣体力 + 写 `dungeon_battle`
- 结算时更新 battle 状态、资源、副本进度、奖励流水

### [dungeon_network_server.cpp](../../dungeon_server/dungeon_network_server.cpp)

副本服网络包装层。

## 两个演示入口

### [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)

离线业务回归入口。

最适合讲：

- service/repository 分层
- 业务主链路
- 一致性和事务

### [tools/demo_client/main.cpp](../../tools/demo_client/main.cpp)

真实网络联调入口。

最适合讲：

- `gateway -> 三服` 请求链路
- protobuf 协议
- 会话绑定
- 上游错误映射
