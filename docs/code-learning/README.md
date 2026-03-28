# 项目代码学习导读

这组文档不是架构设计文档，而是给“第一次读这套代码的人”准备的学习入口。

如果你的目标是理解当前项目代码，建议按下面顺序阅读：

1. 先看 [项目总 README](../../README.md)，了解项目目标、可执行目标和当前阶段。
2. 再看 [代码地图](./code-map.md)，先建立目录和模块的整体感。
3. 然后看 [流程走读](./flow-walkthrough.md)，顺着 `demo_flow` 把登录和角色加载代码串起来。
4. 最后再回到具体 `.cpp/.h` 文件逐个细读。

## 当前项目处于什么阶段

目前这套代码还是“学习型骨架”阶段，特点是：

- 已经有清晰的目录和模块边界。
- 已经有登录、会话、角色加载的业务骨架。
- 目前仓储层仍然是内存版实现，目的是先把业务流程讲清楚。
- 真正的 MySQL / Redis DAO 还没有接入。

所以你在阅读时要带着一个认知：

**现在的重点是理解代码组织方式和调用链，而不是研究复杂底层实现。**

## 先从哪里看最容易懂

如果你只想最快进入状态，建议先读这 5 个文件：

1. [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)
2. [login_server/login_service.cpp](../../login_server/login_service.cpp)
3. [game_server/player/player_service.cpp](../../game_server/player/player_service.cpp)
4. [common/src/bootstrap/service_app.cpp](../../common/src/bootstrap/service_app.cpp)
5. [CMakeLists.txt](../../CMakeLists.txt)

原因很简单：

- `demo_flow` 是整条学习链路的“入口”
- `login_service` 是第一个真正有业务判断的地方
- `player_service` 是第二个真正有业务判断的地方
- `service_app` 体现了服务进程如何启动
- `CMakeLists.txt` 告诉你工程是怎么被拼装起来的

## 推荐阅读方法

### 方法 1：按“执行路径”读

最适合新手。

从 `demo_flow/main.cpp` 开始，看程序如何：

- 读取配置
- 创建仓储对象
- 创建业务服务对象
- 发起登录
- 发起角色加载
- 输出结果

### 方法 2：按“分层”读

更适合你已经看过一遍后做第二轮理解。

可以按下面顺序读：

- 公共基础层：`common`
- 业务层：`login_server`、`game_server`
- 演示入口：`tools/demo_flow`
- 构建与运行：`CMakeLists.txt`、`scripts/`

## 阅读时要特别留意的 3 个问题

1. 这个文件是在“定义数据”，还是“处理业务”，还是“启动程序”？
2. 这个类是“抽象接口”，还是“临时内存实现”，还是“真正业务服务”？
3. 当前代码里哪些地方以后会被真实 MySQL / Redis 替换？

如果你能在阅读过程中一直回答这 3 个问题，理解速度会快很多。

## 学完这套代码后，你应该能讲清楚什么

读完当前项目，你至少应该能自己讲清楚下面这些点：

- 这个项目的目录是怎么分的
- `common`、`login_server`、`game_server`、`demo_flow` 各自负责什么
- 登录流程当前是如何被组织起来的
- 角色加载流程当前是如何被组织起来的
- 为什么现在先用内存仓储
- 为什么后面再替换成 MySQL / Redis DAO

如果你能把这些讲顺，说明你已经不是“看过代码”，而是“理解了这套代码”。
