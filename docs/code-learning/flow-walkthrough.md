# 流程走读：从 `demo_flow` 理解当前代码

这份文档用一条最短路径带你理解当前项目：

**账号登录 -> 创建会话 -> 加载角色**

当前最推荐的阅读入口是：

- [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)

因为它直接把整个链路串起来了。

---

## 1. 程序从哪里开始

程序入口在：

- [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)

先看 `main()` 做了什么。

它的工作顺序大致是：

1. 设置日志服务名
2. 解析命令行参数
3. 读取登录服配置
4. 读取游戏服配置
5. 创建账号仓储和会话仓储
6. 创建登录服务
7. 执行登录
8. 创建角色仓储
9. 创建角色服务
10. 执行角色加载
11. 打印结果

这就是整个学习主线。

---

## 2. 登录阶段是怎么走的

### 第一步：创建仓储

在 `demo_flow` 里，登录相关先创建了两个对象：

- `InMemoryAccountRepository`
- `InMemorySessionRepository`

它们分别代表：

- 账号数据从哪里来
- 会话数据存到哪里去

当前它们都只是内存版实现，但接口设计已经是“可替换”的。

### 第二步：创建业务服务

接着创建：

- [login_server/login_service.cpp](../../login_server/login_service.cpp)

`LoginService` 不关心账号到底存在哪里，也不关心会话到底存在哪里。

它只关心业务规则：

- 有没有这个账号
- 账号是否启用
- 密码对不对
- 登录成功后要不要创建会话

### 第三步：执行登录

`LoginService::Login()` 当前逻辑很短，但非常值得反复看。

它体现了最重要的服务层职责：

- 调仓储拿数据
- 做业务判断
- 组织返回结果

这正是以后接 MySQL / Redis 后仍然不会变的部分。

---

## 3. 角色加载阶段是怎么走的

### 第一步：创建角色仓储

登录成功后，`demo_flow` 会创建：

- `InMemoryPlayerRepository`

它负责根据 `player_id` 提供角色数据。

### 第二步：创建角色服务

接着创建：

- [game_server/player/player_service.cpp](../../game_server/player/player_service.cpp)

`PlayerService` 的职责和 `LoginService` 很像：

- 调仓储
- 判断是否查到玩家
- 返回统一结构

### 第三步：执行角色加载

`PlayerService::LoadPlayer()` 根据 `player_id` 取角色对象。

如果没找到：

- 返回失败

如果找到了：

- 返回 `PlayerProfile`

这就是当前“角色加载”主逻辑。

---

## 4. 为什么现在先用内存仓储

这是当前代码最容易误解的地方。

现在不用真实 MySQL / Redis，不是因为不重要，而是因为当前阶段有一个更优先的目标：

**先把调用关系、分层方式、业务骨架讲清楚。**

内存仓储的价值在于：

- 不需要先处理第三方库接入
- 不需要先处理数据库环境问题
- 可以把注意力放在业务组织方式上
- 后续替换真实 DAO 时，服务层基本不用重写

所以你读这部分代码时，应该重点看：

- 为什么有 `Repository` 接口
- 为什么 `Service` 依赖接口而不是依赖具体实现

---

## 5. 服务进程是怎么启动的

除了业务链路本身，你还应该顺手看：

- [common/src/bootstrap/service_app.cpp](../../common/src/bootstrap/service_app.cpp)

这个文件告诉你：

- 服务程序怎么读配置
- `--check` 是怎么工作的
- 服务为什么会进入心跳循环

它属于“进程启动逻辑”，不是业务逻辑。

这个区别非常重要：

- `service_app.cpp` 决定“程序怎么跑起来”
- `login_service.cpp` / `player_service.cpp` 决定“业务怎么处理”

---

## 6. 现在读代码时最好的提问方式

建议你边读边问自己这几个问题：

1. 这个文件是入口、服务、仓储，还是模型？
2. 这个类是在表达业务规则，还是只是在存取数据？
3. 如果以后换成 MySQL / Redis，实现会改在哪里？业务会不会跟着变？

如果你一直带着这三个问题，当前项目会非常好懂。

---

## 7. 建议你接下来怎么读

如果你准备真正开始啃代码，我建议按这个顺序：

1. [tools/demo_flow/main.cpp](../../tools/demo_flow/main.cpp)
2. [login_server/login_service.cpp](../../login_server/login_service.cpp)
3. [login_server/auth/in_memory_account_repository.cpp](../../login_server/auth/in_memory_account_repository.cpp)
4. [login_server/session/in_memory_session_repository.cpp](../../login_server/session/in_memory_session_repository.cpp)
5. [game_server/player/player_service.cpp](../../game_server/player/player_service.cpp)
6. [game_server/player/in_memory_player_repository.cpp](../../game_server/player/in_memory_player_repository.cpp)
7. [common/src/bootstrap/service_app.cpp](../../common/src/bootstrap/service_app.cpp)
8. [CMakeLists.txt](../../CMakeLists.txt)

按这个顺序读，理解成本最低，也最容易建立“整体 -> 细节”的感觉。
