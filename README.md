# Mobile Game Backend

本仓库是一个基于 `C++17` 的手游后端项目，当前目标不是继续堆演示材料，而是收敛为一套可维护、可扩展、可商业化演进的服务端工程。

正式文档统一放在 [docs/README.md](/home/wang/main/project/server/docs/README.md)。

## 项目定位

- 多服务后端：`gateway_server`、`login_server`、`player_server`、`dungeon_server`
- 自定义 TCP 长连接协议，消息定义使用 `protobuf`
- 数据层以 `MySQL + Redis` 为核心
- 工程目标从“演示可跑”升级到“工程可持续”

## 文档入口

- [文档总览](/home/wang/main/project/server/docs/README.md)
- [系统架构](/home/wang/main/project/server/docs/02-architecture.md)
- [系统设计](/home/wang/main/project/server/docs/03-system-design.md)
- [开发流程](/home/wang/main/project/server/docs/04-development-workflow.md)
- [编码规范](/home/wang/main/project/server/docs/05-engineering-standards.md)

## 代码主目录

```text
.
|-- common/
|-- framework/
|-- services/
|-- login_server/
|-- game_server/      # player 领域与应用代码
|-- dungeon_server/
|-- proto/
|-- configs/
|-- deploy/
|-- scripts/
|-- tests/
`-- docs/
```

## 常用命令

构建：

```bash
cmake -S . -B build
cmake --build build -j
```

## 当前文档策略

- 根 `README` 只保留项目入口和导航
- 正式文档收敛为少量主文档，不按主题过度拆分
- 当前阶段先聚焦架构、设计、开发和编码规范
- 后续新增方案优先补正式文档，不再叠加零散说明文
