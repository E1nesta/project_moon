# 代码地图

这份文档的目标是帮你回答一个问题：

**这个项目每个目录、每个关键文件，分别负责什么？**

---

## 1. 顶层目录

### [CMakeLists.txt](../../CMakeLists.txt)

工程构建入口。

你可以把它理解成“项目装配表”：

- 定义 `common` 静态库
- 定义 `gateway` 可执行程序
- 定义 `login_server` 可执行程序
- 定义 `game_server` 可执行程序
- 定义 `demo_flow` 演示程序

如果你想知道“哪些 `.cpp` 最终会被编译进去”，这里是第一入口。

### [README.md](../../README.md)

项目说明入口。

主要告诉你：

- 项目目标是什么
- 当前做到哪个阶段
- 怎么构建
- 怎么跑自检

### [configs/](../../configs)

配置文件目录。

这里目前有：

- `gateway.conf`
- `login_server.conf`
- `game_server.conf`

当前的 demo 账号、角色数据也先放在配置里，后续会替换成数据库。

### [scripts/](../../scripts)

脚本目录。

目前最有用的是：

- [run_local.ps1](../../scripts/run_local.ps1)
- [run_local.sh](../../scripts/run_local.sh)

它们负责：

- 配置 CMake
- 编译工程
- 运行服务自检
- 运行 `demo_flow`

---

## 2. 公共层：`common`

`common` 的定位是：**所有服务都能复用的公共能力**。

### [common/src/bootstrap/service_app.cpp](../../common/src/bootstrap/service_app.cpp)

服务启动骨架。

它负责：

- 读取配置文件
- 初始化日志服务名
- 打印关键配置摘要
- 执行 `--check`
- 进入心跳循环

这不是业务代码，但它定义了“一个服务程序怎么启动”。

### [common/src/config/simple_config.cpp](../../common/src/config/simple_config.cpp)

最基础的配置加载器。

当前支持很简单的 `key=value` 形式，足够骨架阶段使用。

### [common/src/log/logger.cpp](../../common/src/log/logger.cpp)

最基础的日志组件。

当前它做的事情也很简单：

- 设置服务名
- 拼时间戳
- 输出日志级别
- 打印到标准输出

### [common/include/common/model/](../../common/include/common/model)

领域数据结构定义目录。

当前有 3 个核心模型：

- [account.h](../../common/include/common/model/account.h)
- [session.h](../../common/include/common/model/session.h)
- [player_profile.h](../../common/include/common/model/player_profile.h)

你可以把这些结构体理解成“业务层之间传递的数据形状”。

---

## 3. 登录服务：`login_server`

`login_server` 负责账号鉴权和会话生成。

### [login_server/main.cpp](../../login_server/main.cpp)

登录服务进程入口。

它本身不做业务，只做参数解析，然后调用公共启动逻辑。

### [login_server/login_service.h](../../login_server/login_service.h)
### [login_server/login_service.cpp](../../login_server/login_service.cpp)

真正的登录业务服务。

这里是当前最值得读的业务代码之一。

它完成了：

- 根据账号名查账号
- 检查账号是否存在
- 检查账号是否禁用
- 校验密码
- 创建会话
- 返回默认角色 ID

### [login_server/auth/account_repository.h](../../login_server/auth/account_repository.h)

账号仓储接口。

它的意义非常重要：

- 业务层不直接依赖具体存储
- 当前可以用内存实现
- 未来可以换成 MySQL DAO

### [login_server/auth/in_memory_account_repository.cpp](../../login_server/auth/in_memory_account_repository.cpp)

账号仓储的内存版实现。

当前只是从配置中构造一个 demo 账号对象。

### [login_server/session/session_repository.h](../../login_server/session/session_repository.h)

会话仓储接口。

未来它很适合被 Redis 版实现替换。

### [login_server/session/in_memory_session_repository.cpp](../../login_server/session/in_memory_session_repository.cpp)

会话仓储的内存版实现。

当前做了两件事：

- 创建 `session_id`
- 把会话对象放进内存 map

---

## 4. 游戏服务：`game_server`

`game_server` 当前负责角色加载。

### [game_server/main.cpp](../../game_server/main.cpp)

游戏服务进程入口。

同样不直接处理业务，只负责启动。

### [game_server/player/player_service.h](../../game_server/player/player_service.h)
### [game_server/player/player_service.cpp](../../game_server/player/player_service.cpp)

角色加载业务服务。

它现在的逻辑很简单：

- 根据 `player_id` 查角色
- 如果没找到，返回失败
- 如果找到，返回角色对象

虽然简单，但这已经体现了业务服务和仓储的分离。

### [game_server/player/player_repository.h](../../game_server/player/player_repository.h)

角色仓储接口。

未来最适合替换成 MySQL DAO。

### [game_server/player/in_memory_player_repository.cpp](../../game_server/player/in_memory_player_repository.cpp)

角色仓储的内存版实现。

当前从配置中构造一个 demo 角色对象。

---

## 5. 演示入口：`demo_flow`

### [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)

这是当前最适合学习的代码入口。

原因是它把多个模块串起来了：

- 加载配置
- 创建账号仓储
- 创建会话仓储
- 创建登录服务
- 发起登录请求
- 创建角色仓储
- 创建角色服务
- 发起角色加载请求
- 输出成功结果

换句话说，它是一个“没有网络层干扰的最小业务演示器”。

---

## 6. 当前代码最核心的设计点

这套代码虽然还不大，但已经体现了几个很重要的工程思想：

- 进程入口和业务逻辑分开
- 业务逻辑和存储实现分开
- 公共能力抽到 `common`
- 用 `demo_flow` 做最小学习闭环

所以你在读代码时，不要只看“逻辑多不多”，更要看“边界划得清不清楚”。
