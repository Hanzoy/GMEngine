# GM Engine 0.9：合作联机与运行时服务

本版先实现局域网合作建造。编辑器仍面向人，`gmctl` 面向脚本和 AI；项目组件、UI、运行时输入和预制体操作共用引擎接口。网络、音频和资源属于原生运行时服务，游戏负责玩法规则。SDK 升至 **ABI 10**，旧游戏 DLL 需要重编译，编辑期资源需要重新构建。Player 继续读取二进制场景和 UI，不解析 JSON。

## 已交付与边界

| 方向 | 本版实现 | 仍需继续完善 |
| --- | --- | --- |
| 游戏 UI | Row / Column / Grid、padding / gap、锚点 / pivot、fill、字符串绑定、虚拟列表、滚动、按钮事件、Tab / Enter、触摸输入消费；编辑器可编辑这些属性 | 文本输入框、完整滚动条、列表键盘导航、自动换行、UI 模板继承与覆盖 |
| 输入 | 可重绑 ActionMap、上下文 / 模态、Windows XInput、10 点触摸、固定步长输入边沿保留 | Android 手柄和软键盘、项目设置中的可视化按键映射 |
| 音频 | WAV / MP3 / FLAC、音效复用、流式音乐、Master / Effects / Music / UI 总线、暂停恢复和回收 | 空间音效、编辑器混音面板、目标手机音频延迟实测 |
| 资源 / 预制体 | 异步文件读取、跨会话共享不可变缓冲、引用释放、独立 `.prefab.json` 编译、运行时实例化 | GPU 资源跨 World 池、细粒度纹理卸载、预制体变体 / 覆盖编辑 |
| 性能 / 多线程 | System 总 / 均 / 最大耗时、碰撞 / 提取 / UI 分项、120 帧 P50 / P95；游戏可提交后台任务 | ECS System 仍串行；尚未引入冲突分析与并行 ECS 调度 |
| 联机 | 原生 LAN 建房 / 加入 / 发现、可靠有序消息、限流和超时；合作建造完整例子 | Steam 传输、好友邀请 / 大厅 / 中继、主机迁移、自动状态复制、动作游戏预测 / 回滚 |
| Android | ARM64 NativeActivity、bgfx OpenGL ES、多点触摸、安全内容区、生命周期、私有存档、可签名 APK | 尚无真机验证；没有通用设备部署面板、AAB 商店发布流程、iOS 实现 |

这里的“预制体卸载”释放定义；已经生成的实体继续存在。其图片在当前 World 存活期间保留，防止仍在使用图片的实体失效。资源共享服务管理文件缓冲，尚未自动统一所有 GPU 资源的生命周期。

## 服务与所有权

公开头文件位于 `sdk/include/gm/`。`gm_game_extensions(version, host)` 中通过 `host->query(Id, Version)` 获取 `NetworkService`、`AudioService`、`ResourceService`、`JobService`。检查返回指针、`structSize` 和 `version`；会话由 System 创建、持有并在 `destroy` 中关闭。

`Context::service<UiService>()`、`Context::service<PrefabService>()`、`Context::service<ContentService>()` 查询当前 World 服务。调用服务时传 `context->world`。UI 在 System 创建时可用；Prefab / Content 应在首次 update 中使用，不能在构造过程中依赖尚未绑定的 World 所有者。

- 网络：所有 API 在创建会话的线程调用；`poll` 消息借用到下一次 poll / close。保留消息需要复制。
- 后台任务：输入复制，输出写入有界 ByteSink；回到主线程 poll 后再修改 ECS。工作线程不能借用 World、UI 或可变游戏状态。System 销毁必须 `JobService::close`，它取消并等待回调结束；工作函数应检查取消信号。
- 资源：请求路径相对内容根目录；同路径 / 大小 / 修改时间的并发请求共享数据。释放最后一个持有者后回收或取消，借用指针同时失效。
- 音频：会话拥有 clips 和 voices。播放中的 clip 不能卸载；先 stop 或 collect。解码音效复用 PCM，流式音乐按需读取。Android 暂停 / 恢复会协调当前线程创建的音频会话。
- 这些服务不会自动为游戏建立会话或保存游戏规则；网络对象、音频设备、后台任务句柄不写入 ECS 二进制存档。

容量是明确的：网络单消息 64 KiB、每连接发送队列 1 MiB、接收事件负载 1 MiB；每个资源文件 64 MiB、全局资源缓冲预算 256 MiB；音效单 clip 解码 64 MiB、会话 128 MiB、128 clips / 128 voices；任务每组 64 个未领取结果、单个输入 / 输出各 8 MiB；列表每控件 10,000 行 / 2 MiB，整个 UI 文档 100,000 行 / 8 MiB。超限返回错误，游戏必须处理，不应忽略。

## 合作建造：运行和开发

独立项目在 `D:\Demo\GMEngineGame\projects\CoopWorkshop`。项目源代码与图片不属于引擎仓库。

```powershell
cd D:\Demo\GMEngineGame\projects\CoopWorkshop
./tools/build.ps1
./tools/run.ps1 -Role host
# 第二台电脑复制完整 dist 目录；也可复制项目后运行：
./tools/run.ps1 -Role client -Address 192.168.1.20
```

将地址改成房主 IPv4，两台电脑使用同一构建版本和端口（默认 27030）。如果 Windows 请求网络访问，允许实际使用的局域网网络。直接运行 `dist/gm_player.exe` 可用 H 创建房间、J 加入默认 127.0.0.1、Esc 离开；跨电脑启动使用上述参数，或设置 `GM_WORKSHOP_ROLE=client`、`GM_WORKSHOP_ADDRESS=房主IPv4` 后启动 Player。示例编辑器中 `session` 的 `workshop.Settings` 组件可设置 role、port 和 IPv4 四段。

左键 / 触摸空格建造，右键拆除。`Scripts/game.cpp` 展示完整数据路径：

1. 客户端发送命令序号、格子、建造 / 拆除操作；不发送 ECS 指针。
2. 房主检查命令序号、范围和操作，并修改唯一权威棋盘。
3. 房主广播带修订号的二进制快照。队列暂满时保留待发标记，后续 pump 重试最新状态。
4. 各端按稳定格子 ID 更新对应实体。未改变的格子不重复设置组件。
5. 后来加入或重连获取当前完整快照。连接 peer ID 会改变，游戏对象 ID 不依赖 peer ID。

示例使用 16×12 棋盘，快照 776 字节，适合验证 2–8 人合作建造流程。大量建筑需要在此协议上增加增量、分块与关注区域；这不是已经实现的大世界同步。示例无持久联机存档、自动重连、主机迁移或玩家账号认证。它允许合作成员拆除任意建筑。引擎没有隐式转发用户消息或复制任意组件内存。

TCP 提供可靠有序消息，UDP 提供局域网发现。`discover` 发送一次广播，之后 pump / poll 获取 Room；刷新需重新发起。游戏暂停时仍需 pump，否则连接会超时；示例 Player 在后台继续模拟，编辑器暂停用于调试时可能使房间断开。默认握手 10 秒、接收空闲 15 秒、心跳 2 秒；每次 pump 限制接收 / 发送工作量。

Steam 目前没有后端，不提供虚假的 Steam 开关。后续在同一会话接口下接 `ISteamNetworkingSockets`，另加大厅 / 邀请服务。开放源码 GameNetworkingSockets 与 Steam 平台账号、大厅和 Valve 中继不是一回事，接入需遵循项目的 Steamworks 配置。[Steam 官方网络说明](https://partner.steamgames.com/doc/features/multiplayer/networking)、[Sockets API](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets)。

## UI 源格式、编译与命令

仍使用 `gm.ui` JSON 编辑源；二进制 UI 升至 v3，并保留旧 v1 / v2 读取。容器示例：

```json
{
  "id":"sidebar", "type":"panel", "x":0, "y":0, "w":320, "h":400,
  "layout":"column", "padding":16, "gap":12,
  "children":[
    {"id":"status", "type":"label", "x":0, "y":0, "w":288, "h":28, "bindText":"status"},
    {"id":"log", "type":"list", "x":0, "y":0, "w":288, "h":280, "fillX":true, "rowHeight":28, "event":"game.choose"}
  ]
}
```

布局字段还包括 `anchorX/Y`、`pivotX/Y`、`fillX/Y`、`columns`。它们是当前点锚 / 流布局规则，不能等同于完整 Flexbox。隐藏节点不占流布局空间。静态几何缓存复用，单纯字符串更新不触发布局重建。列表存储行字符串，只为可见行绘制；图文混排和可复用行控件尚未实现。

按钮 `event` 变成 SDK Event type，payload 为 `UiEventData {node, row}`，下一模拟帧交给游戏。数字 / 生命值继续使用场景 bindings。键鼠与第一触点经过 HUD 命中和捕获，其他触点独立保留。

`gmctl --project ... --stdio` 可连续调用：

```json
{"op":"preview.run"}
{"op":"runtime.ui.query"}
{"op":"runtime.ui.text","name":"status","text":"正在建造"}
{"op":"runtime.ui.list","node":"log","rows":["第一行","第二行"]}
{"op":"runtime.ui.scroll","node":"log","pixels":28}
{"op":"runtime.ui.visible","node":"log","visible":true}
```

`runtime.activate` 接节点索引和可选 row。`runtime.input` 支持 `touches:[{id,x,y,down,pressed,released}]` 和 `gamepads:[{connected,buttons,axes}]`；其坐标为场景设计坐标。键码沿用已有数字定义，手柄轴为 LX / LY / RX / RY / LT / RT，Y 向下。

## 预制体与打包

`assets/*.prefab.json` 使用 gm.scene 组件格式，要求一个根实体位于索引 0、无 HUD 内容或绑定。与场景一样通过稳定 `.gmmeta` ID 编译，生成 `prefabs/<id>.gmprefab` 和 `prefabs.gmcatalog`。模块 / schema 必须匹配。

```json
{"op":"runtime.prefab.load","file":"prefabs/资源ID.gmprefab"}
{"op":"runtime.prefab.spawn","prefab":1}
{"op":"runtime.prefab.unload","prefab":1}
```

file 相对项目 build 目录，使用实际 load 返回的句柄。运行中实例化遵循 ECS 的延迟结构变更规则。`.wav/.mp3/.flac` 复制到构建和发布目录的 `assets/`；它们是运行时音频文件，不参与 JSON 解析。项目打包包含已编译场景、预制体、原生模块、音频和许可证。资源索引继续跟踪源资产依赖，未改变场景复用编译结果。

## Android Touch Lab

独立项目 `D:\Demo\GMEngineGame\projects\TouchLab` 使用与 Windows 相同的 ECS、UI、输入映射和二进制资源。NativeActivity 静态链接游戏模块和 libc++，bgfx 使用 OpenGL ES。首次安装或内容版本变化时解包内容；之后启动复用私有内容目录。UI 按 NativeActivity contentRect 避开系统内容区，触点映射回设计坐标。暂停时尝试保存 `android-suspend`，恢复重置模拟时钟；自定义 System 必须提供存档回调才能保存其私有状态。

```powershell
cd D:\Demo\GMEngine
./scripts/build-android.ps1 -Project D:/Demo/GMEngineGame/projects/TouchLab/game.gmproject
# 连接并授权 Android 设备后：
D:/Demo/GMEngineBuild/tools/Android/platform-tools/adb.exe install -r D:/Demo/GMEngineGame/projects/TouchLab/dist/android/com.gmengine.touchlab-debug.apk
```

构建需要 Windows 游戏构建工具、PowerShell 7、Python 3、JDK 17、Android NDK r29、API 35 平台、Build Tools 35 和 Ninja。脚本可传 `-NdkRoot`、`-BuildTools`、`-AndroidJar`、`-Ninja`、`-AppPackage`，不依赖本机固定盘符。默认指向相邻 GMEngineBuild/tools 中的已安装工具。当前脚本面向简单 `Scripts/*.cpp` 项目；自定义第三方库 / 额外 CMake 依赖还需补平台配置。

本机已生成 **ARM64 / Android 8.0+ / target 35 的调试签名 APK**，验证 v2 / v3 签名和 16 KiB 库对齐。尚未连接 Android 设备，未宣称触摸、音频、GPU 或生命周期已通过真机测试。默认 Roboto 支持拉丁字符；中文项目应提供有授权的 `assets/font.ttf`。APK 是开发验证包，尚非商店发布配置。

## 验证记录（2026-10-31）

- 13 项 CTest：网络 / 服务 / 任务 / 进程 / 场景 / 生命周期 / 输入画布 / 存档 / 空间查询 / 编辑 / 二进制 UI / 图片 / 渲染全部通过。
- 网络测试覆盖两个客户端、版本拒绝、队列背压、顺序和分帧、畸形超长包、发现、断开 / 重连、线程约束。
- 服务测试覆盖真实 WAV 离线 PCM 能量、静音 / 暂停 / 流式回收、任务复制 / 取消 / 等待、触点 ID、异步共享、预制体、帧统计。10,000 行列表渲染测试限制可见四边形数量。
- CoopWorkshop 三个独立原生进程验证同一建筑状态、迟加入、拆除、重连；原生 GPU 截图确认实际画面。本机该静态画面 143 quads / 1 draw，不作为其他场景或设备性能保证。
- 无 CMake 的新建 / 编辑 / 运行 / 打包流程、增量导入、多场景和独立模块扩展回归通过。
- DSPNative 重新构建为 ABI 10；351 项玩法断言、模态输入、建造 / 传送带、私有状态存档后逐字节一致续跑，以及四个 GPU 截图通过。
- Android 原生编译、APK 签名 / 对齐通过，设备运行验证待完成。

优先的后续工作是 Android 真机测试和更大建造存档的同步量测；动作联机再独立增加输入序号、快照插值、预测和校正。只有测量说明瓶颈位于 ECS 系统调度时，再引入并行 ECS。
