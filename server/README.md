# pvz-lobby — 联机大厅服务端

Go 1.24，**零第三方依赖**。当前只做一件事：给 1v1 房间提供房间列表、进出房、准备/开局、踢人、掉线处理和中继转发。玩法逻辑全部在客户端，服务器不模拟游戏。

设计文档见 [`../doc/06-PvZPortable/03-联机功能设计.md`](../doc/06-PvZPortable/03-联机功能设计.md)。

## 运行

```bash
cd server
go run ./cmd/pvz-lobby -listen 127.0.0.1:7777
```

参数：

| 参数 | 默认 | 说明 |
|---|---|---|
| `-listen` | `:7777` | TCP 监听地址 |
| `-proto` | `1` | 接受的协议版本，不符返回 1001 |
| `-res` | `1073-cn` | 当前资源版本，进房时双方必须一致 |
| `-max-rooms` | `5000` | 房间上限 |
| `-idle` | `30s` | 超过此时长没有任何帧就断开 |
| `-pick` | `60s` | 选卡时限，到点未提交的自动随机补满 |

测试客户端（交互式）：

```bash
go run ./cmd/pvz-cli -nick 玩家A -addr 127.0.0.1:7777
```

命令：`rooms` `create [名字]` `join 房间码` `joinlast` `ready` `unready` `start` `kick pid` `leave` `chat 文本` `sleep 毫秒` `rtt` `quit`。

一键演示（Windows，自动起服务 + 房主 + 客机并打印日志）：

```powershell
powershell -File scripts/demo.ps1
```

## 协议 v1

帧：`len u32(LE，含类型字节) | type u8 | payload`，单帧上限 64 KiB。

| type | 名称 | 方向 | 说明 |
|---|---|---|---|
| 1 | JSON | 双向 | 控制消息，见下表 |
| 2 | INPUT | C→S→C | 对局输入帧，服务器原样转发给房内另一人 |
| 3 | PING | C→S | 8 字节：`seq u32 | 毫秒时间戳 u32` |
| 4 | PONG | S→C | 原样回显，客户端据此算 RTT |
| 5 | STATE | S→C | 预留 |
| 6 | HASH | C→S→C | 一致性哈希帧，按 INPUT 处理 |

控制消息（`t` 字段）：

| `t` | 方向 | 关键字段 |
|---|---|---|
| `hello` | C→S | `ver` `proto` `res` `nick` |
| `welcome` | S→C | `pid` `proto` `res` `serverTime` |
| `rooms` | C↔S | `list[]`：`code name host mode n maxp state res` |
| `create` | C→S | `name` `mode` `maxp` `role`（`host_plant` 默认 / `host_zombie` / `random`） |
| `join` | C→S | `code` |
| `room` | S→C | `code name host maxp state res seed members[]` |
| `ready` | C→S | `ready`（bool） |
| `start` | C→S | 房主发起；两名玩家都就绪后进入选卡阶段 |
| `start` | S→C | `code seed startAt res picks{plant[],zombie[]}` |
| `pick_start` | S→C | `role`（我的角色）`pool{plant[],zombie[]}` `slots` `deadline`（毫秒时间戳） |
| `pick` | C→S | `role` `cards[]`（6 个 SeedType 名），截止前可反复提交，以最后一次为准 |
| `pick_state` | S→C | `done[]`：已提交的 pid，**不含卡组内容** |
| `kick` | C→S | `pid`（房主专用） |
| `leave` | C→S | — |
| `left` | S→C | `reason`：`leave` / `kick` / `timeout` / `disconnect`，附 `pid` |
| `chat` | C↔S | `text`，广播给房内所有人 |
| `prtt` | C→S | `ping`：客户端上报自己测得的 RTT，供房内其他人显示 |
| `err` | S→C | `err` 错误码 + `text` 中文提示 |

错误码：1001 协议不符、1002 资源不符、1003 房间不存在、1004 房间已满、1005 已在房间/对局已开始、1006 非房主、1007 请求非法、1008 超时、1009 有人未准备、1010 人数不足、1011 限流、1012 选卡非法。

## 行为约定

- 房间码 6 位，字符集 `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`（去掉 I/O/0/1）。
- 状态机 `idle → ready → picking → playing`；两人都准备才进入 `ready`，房主才能 `start`。
- `start` 只生成种子并进入 `picking`：服务器下发卡池与角色，双方各提交 6 张卡（Plant 池 18 选 6、Zombie 池 12 选 6，卡名与 `src/ConstEnums.h` 一致）。
- 选卡校验：数量 6、不允许重复、必须在池内、角色必须与房间分配一致，否则 1012。
- 两人都提交或到 `-pick` 时限（未提交者从剩余卡池随机补满）→ 服务器把最终卡组放进 `start.picks` 广播，`startAt = now + 2000ms`。卡组只走这条消息，不进对局帧，锁步协议不受影响。
- 选卡阶段有人离开/被踢 → 房间回 `idle`、清空卡组，不判负。
- 房主离开或掉线 → `host` 迁移到房间里最早加入的其他人；房间空了就删除。
- 对局中断线 → 剩下的人收到 `left{reason:"disconnect"|"timeout"}`，房间解散。
- 每个连接一个写协程 + 256 帧队列；队列满视为呆滞连接，直接断开，不拖慢大厅。
- 对局帧限速 240 帧/秒/连接。

## 测试

```bash
go vet ./...
go test ./... -race -count=1
```

覆盖：房间生命周期与开局种子一致、输入帧透传、踢人、房主迁移、掉线解散、协议/资源版本不符、房间列表与离开、选卡全流程（角色/卡池/回显）、非法卡组（少卡/重复/越池/角色不符）、选卡超时随机补满、开局后拒绝选卡、选卡阶段离场回退。

## 未实现（M1 之后）

HTTP 资源清单（7778）、房间密码、观战、匹配、鉴权、WebSocket 网关（网页版需要）。
