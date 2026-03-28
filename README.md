# Mobile Game Backend Replica

一个面向手游项目复现的 C++ 服务端骨架，当前聚焦以下最小闭环：

- 账号鉴权
- 角色数据加载与保存
- 单人副本进入与结算

当前阶段已完成第 1 阶段骨架：

- `gateway`：接入层与协议路由入口
- `login_server`：账号鉴权与登录态入口
- `game_server`：角色与副本业务入口
- `demo_flow`：基于内存仓储的登录与角色加载演示工具
- `common`：日志、配置、服务启动封装
- `deploy`：MySQL / Redis 本地依赖编排
- `configs`：服务配置样例
- `docs/mobile-game-backend-replica`：架构与任务文档

## 技术选型

- 语言标准：`C++17`
- 网络模型：`Boost.Asio` 风格的事件驱动模型，首版先保持简单
- 协议方案：`protobuf`，先用于消息定义与编解码
- 存储：`MySQL 8 + Redis 7`
- 构建：`CMake`
- 原则：优先做稳定的业务闭环，不额外引入 RPC、消息队列、ORM 等复杂度

## 目录结构

```text
.
|-- CMakeLists.txt
|-- common/
|-- gateway/
|-- login_server/
|-- game_server/
|-- configs/
|-- deploy/
|-- scripts/
`-- docs/
```

## 本地依赖启动

```bash
docker compose -f deploy/docker-compose.yml up -d
```

默认会启动：

- MySQL 8.4，端口 `3306`
- Redis 7，端口 `6379`

## 构建

Linux:

```bash
cmake -S . -B build
cmake --build build -j
```

Windows PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

## 服务自检

`--check` 会加载配置并输出关键启动信息，然后立即退出，适合先验证工程骨架：

```bash
./build/gateway --config configs/gateway.conf --check
./build/login_server --config configs/login_server.conf --check
./build/game_server --config configs/game_server.conf --check
./build/demo_flow --login-config configs/login_server.conf --game-config configs/game_server.conf
```

Windows PowerShell:

```powershell
.\build\Debug\gateway.exe --config configs/gateway.conf --check
.\build\Debug\login_server.exe --config configs/login_server.conf --check
.\build\Debug\game_server.exe --config configs/game_server.conf --check
.\build\Debug\demo_flow.exe --login-config configs/login_server.conf --game-config configs/game_server.conf
```

## 下一阶段

后续建议按以下顺序继续实现：

1. 用真实 MySQL / Redis 实现替换内存仓储。
2. 补账号表、角色表、玩家资源表的数据访问层。
3. 从 demo_flow 过渡到真实网络请求链路。
4. 实现副本进入、挑战记录与结算事务。

如需后续拆服，再评估内部 RPC；首版不引入。
