# 流程走读：从双入口理解当前代码

这套项目现在有两条理解路径：

- `demo_flow`：最短业务路径
- `demo_client -> gateway -> login/game/dungeon`：真实系统路径

建议先读第一条，再读第二条。

## 1. 先看 `demo_flow`

入口：

- [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)

它把整个业务闭环直接串起来：

1. 读配置
2. 初始化 MySQL / Redis
3. 创建登录服务依赖
4. 创建角色服务依赖
5. 创建副本服务依赖
6. 执行登录
7. 执行角色加载
8. 执行进入副本
9. 执行结算
10. 执行异常用例

这条路径最适合回答：

- 业务规则到底是什么
- Redis 和 MySQL 在哪里参与
- 哪些地方做了幂等和一致性保护

## 2. 登录流程怎么走

登录业务核心在：

- [login_server/login_service.cpp](../../login_server/login_service.cpp)

逻辑很短，但边界很清楚：

- 按账号名查账号
- 校验账号状态和密码
- 创建 Redis session
- 返回默认角色 ID

网络版登录是在：

- [login_server/login_network_server.cpp](../../login_server/login_network_server.cpp)

它不重新实现业务规则，只做三件事：

- 解析 protobuf
- 调 `LoginService`
- 把结果转成 protobuf 响应

## 3. 角色加载怎么走

角色加载业务核心在：

- [game_server/player/player_service.cpp](../../game_server/player/player_service.cpp)

它先校验 session，再决定：

- 命中 Redis 快照直接返回
- 未命中时从 MySQL 读取，再回填 Redis

网络版入口在：

- [game_server/game_network_server.cpp](../../game_server/game_network_server.cpp)

它负责把 `LoadPlayerRequest` 映射成现有 service 调用。

## 4. 进入副本和结算怎么走

核心业务在：

- [dungeon_server/dungeon/dungeon_service.cpp](../../dungeon_server/dungeon/dungeon_service.cpp)

进入副本阶段：

1. 校验 session
2. 校验副本配置
3. 获取 `player:lock:{player_id}`
4. 从 MySQL 读取角色状态
5. 校验等级和体力
6. 调仓储事务扣体力并写 `dungeon_battle`
7. 写 Redis `battle:ctx:{battle_id}`
8. 失效角色快照

结算阶段：

1. 校验 session
2. 校验副本配置和星级
3. 获取玩家锁
4. 优先从 Redis 读 `battle:ctx`
5. Redis miss 时回源 MySQL
6. 校验 `battle_id / player_id / dungeon_id`
7. 调仓储事务更新 battle 状态、资源、副本进度、奖励流水
8. 删除 `battle:ctx`
9. 失效角色快照

## 5. 再看真实网络链路

真实入口在：

- [tools/demo_client/main.cpp](../../tools/demo_client/main.cpp)

客户端先连：

- [gateway/gateway_server.cpp](../../gateway/gateway_server.cpp)

网关负责：

- 收包
- 解析 `msg_id`
- 校验连接上的 `session_id / player_id` 绑定
- 转发到对应业务服

然后分别由：

- [login_server/login_network_server.cpp](../../login_server/login_network_server.cpp)
- [game_server/game_network_server.cpp](../../game_server/game_network_server.cpp)
- [dungeon_server/dungeon_network_server.cpp](../../dungeon_server/dungeon_network_server.cpp)

把 protobuf 请求映射到原有 service 层。

## 6. 为什么还保留 `demo_flow`

这是现在最值得理解的一点。

项目补了真实 `gateway` 和四进程闭环后，没有删除 `demo_flow`，因为它仍然有两个价值：

- 做无网络干扰的业务回归
- 面试时先讲业务，再讲系统演进

所以现在你可以把这套代码理解成：

- `demo_flow` 负责证明“业务做对了”
- `demo_client + gateway + 三服` 负责证明“系统也搭起来了”
