# mobile_game_backend

当前仓库是一个基于 C++17 的多进程游戏后端示例，核心链路是：

- `gateway_server` 负责客户端接入、限流、会话校验、上游转发
- `login_server` 负责账号登录和 session 签发
- `player_server` 负责玩家状态读取与缓存
- `player_internal_grpc_server` 负责向其他服务暴露玩家域内部写接口
- `dungeon_server` 负责进入战斗、结算、奖励事件落库
- `battle_reward_worker` 负责 RocketMQ 消费、重试和最终奖励发放

## 仓库结构

- `apps/` 进程入口和服务装配
- `modules/` 业务模块，按 `application / domain / infrastructure / interfaces / ports` 分层
- `runtime/` 通用运行时能力，包括配置、日志、协议、传输、执行器、存储适配和 gRPC
- `proto/` 对外协议和内部 gRPC 协议
- `configs/` 本地、demo、delivery、prod 配置
- `deploy/` Dockerfile、compose、Nginx、初始化脚本和示例环境变量
- `scripts/` 本地构建、起停、验收和 demo 脚本
- `tests/` 单元测试和集成测试入口

## 开发依赖和环境依赖

这个项目把依赖分成两类：

- 开发依赖：为了在 WSL 里编译、测试、调试源码而安装
- 环境依赖：为了让服务真正跑起来而提供的数据库、中间件和网关组件

### 需要安装在 WSL 的内容

如果你要在 WSL 里本地开发、编译或跑测试，需要这些工具链：

- `gcc/g++` 或等价的 C/C++ 编译器
- `cmake`，要求至少 `3.25`，见 [CMakePresets.json](/home/love/code/server/CMakePresets.json)
- `ninja-build`
- `ccache`
- `pkg-config`
- `git`
- `python3`，测试阶段会跑架构守卫脚本
- `libboost-dev` 和 `libboost-system-dev`
- `default-libmysqlclient-dev`
- `libhiredis-dev`
- `libssl-dev`

此外还需要本地可用的 gRPC/Protobuf 工具链：

- 直接使用系统安装的 `protoc`、`grpc_cpp_plugin`、`libprotobuf-dev`、`libgrpc++-dev`

也就是说，本地开发时真正需要放在 WSL 的，是“编译工具链 + 头文件/开发库 + gRPC/Protobuf 生成工具”。

### 主要由 Docker 提供的内容

如果你用仓库自带的 compose 跑整套服务，下面这些运行时依赖应该放在 Docker 里，而不是手工装进 WSL：

- MySQL
- Redis
- RocketMQ NameServer
- RocketMQ Broker
- RocketMQ Proxy
- Nginx
- 各个后端服务进程本身

对应编排见 [deploy/docker-compose.yml](/home/love/code/server/deploy/docker-compose.yml) 和 [deploy/docker-compose.delivery.yml](/home/love/code/server/deploy/docker-compose.delivery.yml)。

当前仓库没有可用的根目录 `.env.example` 入口。环境变量示例统一以 `deploy/` 下的文件为准。

### 一个实用的判断标准

- 你要“改代码、编译、跑测试”时，主要依赖 WSL
- 你要“启动整套业务环境”时，主要依赖 Docker

## Docker 和 WSL 是怎么配合的

当前仓库推荐两种工作方式。

### 方式一：WSL 编译，Docker 跑环境

这是最适合日常开发的方式：

1. 在 WSL 安装开发依赖
2. 在 WSL 安装系统版 gRPC/Protobuf 开发包
3. 在 WSL 本地编译二进制
4. 用 Docker 启动 MySQL、Redis、RocketMQ、Nginx 等环境
5. 本地二进制连接 Docker 提供的依赖

相关脚本：

- [scripts/up.sh](/home/love/code/server/scripts/up.sh) 先本地构建，再启动 compose
- [scripts/run_demo.sh](/home/love/code/server/scripts/run_demo.sh) 启动环境后执行 demo 流程
- [scripts/down.sh](/home/love/code/server/scripts/down.sh) 停止 compose

### 方式二：Docker 里同时构建和运行

仓库也支持完全通过 Docker 构建镜像并运行：

- 构建使用 [deploy/Dockerfile](/home/love/code/server/deploy/Dockerfile) 的 `builder` 阶段
- 运行时使用 `runtime` 阶段
- 这时编译器、cmake、ninja、grpc/protobuf、mysqlclient 开发包、hiredis 开发包、openssl 开发包都在镜像里

这种方式更接近交付环境，但不如 WSL 本地增量编译方便。

## 快速启动

### 只用 Docker Compose

```bash
docker compose --env-file deploy/.env.demo -f deploy/docker-compose.yml up -d --wait
```

停止：

```bash
docker compose --env-file deploy/.env.demo -f deploy/docker-compose.yml down --remove-orphans
```

### 用仓库脚本

```bash
./scripts/up.sh
./scripts/run_demo.sh
./scripts/down.sh
```

说明：

- `scripts/up.sh` 默认会先在 WSL 本地构建，再启动 compose
- 如果你只想拉起容器，可以自己直接调用 `docker compose`
- 如果你希望 compose 顺手重建镜像，可以设置 `COMPOSE_BUILD=1`
- `demo` 环境以 [deploy/.env.demo](/home/love/code/server/deploy/.env.demo) 为准
- `delivery` 环境以 [deploy/.env.delivery.example](/home/love/code/server/deploy/.env.delivery.example) 或你自己的 `deploy/.env.delivery` 为准

## 本地开发时常见端口

- `7000`：对外网关或 Nginx 暴露口
- `7100`：`login_server`
- `7200`：`player_server`
- `7300`：`dungeon_server`
- `7400`：`player_internal_grpc_server`
- `3307`：MySQL 映射到宿主机
- `6379`：Redis 映射到宿主机
- `9876`：RocketMQ NameServer
- `8081`：RocketMQ Proxy gRPC/客户端入口

## 配置文件怎么选

- `configs/local/` 适合本地直接运行服务二进制
- `configs/demo/` 主要给 demo compose 场景使用
- `configs/delivery/` 面向交付/准生产容器部署
- `configs/prod/` 面向生产配置模板

示例环境变量文件：

- [deploy/.env.demo](/home/love/code/server/deploy/.env.demo)
- [deploy/.env.delivery.example](/home/love/code/server/deploy/.env.delivery.example)

## 现状说明

当前代码里的主流程已经以 `EnterBattle / SettleBattle / GetRewardGrantStatus` 为主，同时还保留了一部分旧的 `EnterDungeon / SettleDungeon` 兼容字段和消息别名。之前那份根目录 MVP 文档描述的是更早阶段的边界，和现在的协议、服务拆分、异步奖励链路已经不一致，所以已移除，改由本 README 作为当前入口说明。

文档维护原则：

- 以代码、配置和脚本实际行为为准
- 过期说明直接删除，不保留并行旧文档
