# 面试讲述稿

这份文档给这套求职 demo 一个固定讲法，适合 3 到 5 分钟快速介绍。

## 1. 先讲项目定位

这不是一个追求“全功能”的大型服务端，而是一个聚焦**游戏业务服核心闭环**的 `C++17` 求职 demo。

我重点复现了 4 段最有代表性的业务：

- 登录鉴权
- 角色加载
- 进入副本
- 副本结算

同时保留了两种演示方式：

- `demo_flow`：直接跑业务闭环，适合讲规则、一致性、事务
- `demo_client + gateway + 三服`：真实网络联调，适合讲系统形态、协议、路由和排障

## 2. 再讲系统怎么拆

我把项目拆成 4 个进程和 1 个公共层：

- `gateway`：客户端接入、协议路由、连接与 session 绑定
- `login_server`：账号鉴权和 Redis session
- `game_server`：角色加载、Redis 快照、MySQL 回源
- `dungeon_server`：进入副本、挑战记录、结算发奖
- `common`：配置、日志、MySQL/Redis 封装、TCP/protobuf 公共层、公共模型

这样拆的好处是：

- 业务边界清楚
- 每个服务职责单一
- 可以先用 `demo_flow` 验证业务，再用真实网络链路验证系统

## 3. 为什么这样设计 Redis

Redis 在这个 demo 里主要承担 4 个角色：

- `session:{id}`：登录态
- `player:snapshot:{player_id}`：角色快照
- `player:lock:{player_id}`：玩家串行化锁
- `battle:ctx:{battle_id}`：战斗上下文

我没有把 Redis 当真相库，**MySQL 才是最终真相**。  
Redis 主要负责：

- 提升读取效率
- 降低重复回源
- 给关键写路径加串行化保护

## 4. 为什么体力在进入时扣

如果体力在结算时才扣，客户端可以无成本反复尝试副本。  
把体力扣减放在进入成功时，可以把“挑战资格”和“挑战记录”绑定到一起。

这个 demo 里的进入流程是：

1. 校验 session
2. 校验等级和体力
3. 获取玩家锁
4. MySQL 事务扣体力并写 `dungeon_battle`
5. 写 Redis battle context
6. 失效玩家快照

## 5. 如何防重复结算

我做了两层保护：

- 结算前先拿 `player:lock:{player_id}`
- MySQL 里 `dungeon_battle.status` 从 `0 -> 1` 的更新必须命中 1 行

也就是说，同一个 `battle_id` 再次结算时，不会再重复发奖励。

## 6. 真实网络链路怎么讲

现在客户端链路是：

`demo_client -> gateway -> login/game/dungeon`

这里我重点会讲三件事：

1. `gateway` 只做路由和连接绑定，不承载业务逻辑
2. 三个业务服都只做 protobuf 映射 + 调现有 service 层
3. 保留 `demo_flow`，让业务回归不受网络干扰

这样就能向面试官说明：

- 我不仅会写业务逻辑
- 也能把它包装成真实可联调的服务系统

## 7. 如何排查问题

我希望这套 demo 不只是“能跑”，还要“能讲、能查”。

所以日志里会重点关注：

- `trace_id`
- `request_id`
- `session_id`
- `player_id`
- `battle_id`
- `error_code`

我在演示时一般会让面试官看两类场景：

- happy path
- 失败场景，例如错误密码、无效 session、重复结算、非法星级、后端服务不可用

## 8. 最后讲边界

当前版本我刻意没有做：

- TLS / 加密鉴权
- Nginx 接入层
- RPC 框架
- Go 后台系统
- 邮件、排行、支付、公会

因为这个版本的目标不是堆技术栈，而是把**游戏业务服最值钱的一小段**做得清楚、可信、可演示。
