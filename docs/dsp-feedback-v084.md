# DSP 项目反馈落实：GM Engine 0.8.4

日期：2026-10-31。输入来自 [DSP 原生复刻报告](dsp-reproduction-engine-notes.md) 的 17 项问题。本版在 0.8.3 的键鼠、Canvas、后台模拟基础上，补齐可复用的持久化和输入路由，并优化实际工厂界面的渲染。游戏规则仍放在独立项目，不进入引擎。

## 本次解决了什么

| 反馈 | 0.8.4 的处理 | 边界 |
| --- | --- | --- |
| E04 私有 System 状态丢失 | 可选 `SystemState` 回调；引擎 checkpoint 保存 ECS、私有 blob、tick、固定步余量、待分发事件和碰撞接触；支持私有版本迁移和失败回滚 | 项目必须主动序列化自己的容器；不是对任意 C++ 对象做内存转储 |
| E05 存档互相覆盖 | 引擎 `StorageService`：用户目录、持有到关闭的跨进程独占锁、唯一临时文件、Windows 刷盘与原子替换、上一版本 `.bak` | 同一 app/profile 同时只允许一个会话访问；锁冲突需由游戏提示，不能无声覆盖 |
| E08 弹窗穿透 | SDK `UiInputRouter`：父子上下文、启停、模态根、指针捕获、键盘焦点和事件消费 | Canvas 项目使用的通用辅助类；未实现正式 UI 控件树、冒泡事件、触屏多指 |
| E09 文字与色块交错产生大量 draw | 纯色四边形复用文字页的白色像素；数字与静态文字共享页；严格保持原绘制次序 | 图片切换、图集跨页和顶点容量仍可能拆批 |
| E10 动态数值持续生成纹理 | 将混合字符串拆为稳定文本段和数字段；数字图集支持小数、百分号；限制缓存条目与纹理页 | 当前仍用 Windows GDI；不是完整的跨平台 Unicode glyph/shaping 系统 |
| E13 一扩展接口就改 ABI | 增加可选 `gm_game_extensions` 导出、服务版本查询、扩展结构长度 | 核心仍为 ABI 9；ABI 8 模块需要重编译，不能跨 ABI 直接加载 |

## 私有状态的接入方式

`sdk/include/gm/game.hpp` 中的 Module/System/Context 布局保持不变。新增接口在 `sdk/include/gm/extensions.hpp`。模块可以额外导出：

```cpp
GM_GAME_EXPORT const gm::sdk::ModuleExtensions*
gm_game_extensions(uint32_t version, const gm::sdk::HostServices* host);
```

引擎查询版本 1。旧 ABI 9 模块没有该导出仍可正常运行；不认识此导出的旧宿主也可以忽略它。项目应检查 `host->service<StorageService>()` 的返回值，服务不可用时明确降级或报错。

每个有私有状态的 System 注册一个 `SystemState`：

- `system` 必须与已注册的 System 名称一致；`version` 是项目私有数据格式版本。
- `save(state, sink, error)` 向 sink 写入字节，可多次写入；单个 System 最多 64 MiB。
- `restore(context, savedVersion, bytes, size, error)` 验证并迁移数据，返回一份独立的新状态；最终仍由原 System 的 `destroy` 销毁。
- 回调必须在模块内捕获异常，通过错误结果返回。失败不得修改旧状态、写入外部文件或发起不可撤销操作。
- 只保存值和稳定标识，不保存 STL 对象原始内存、指针、GPU 句柄或 ECS 数字句柄。事件 source/target 由引擎重映射；事件 payload 和私有 blob 内的引用仍由项目负责。

执行 `runtime.save` 时，有 `create` 的活跃 System 若没有注册回调，引擎明确报错，避免生成看似成功、实际缺少游戏状态的存档。组件格式仍要求匹配模块 schema；本版支持的是**私有状态版本迁移**，不是任意 ECS schema 自动迁移。

读取时先校验文件，再构建候选 World 和所有 System 状态；全部成功后替换旧 World。校验失败、回调迁移失败、私有状态缺失都会保留旧运行状态。旧版 ECS-only 存档仅能在不含有状态 System 的场景中恢复。

```json
{"op":"runtime.save","slot":"before-expansion"}
{"op":"runtime.load","slot":"before-expansion"}
```

路径保持 `%LOCALAPPDATA%/GMEngine/saves/<模块名>/<slot>.gmsave`。编辑器实时会话与离线 `gmctl` 调用同一个运行时命令服务。场景 JSON 保存仍是编辑资源操作，与游戏存档分开。

新 checkpoint 还原模拟时钟和下一个 tick 待分发的事件，清空输入边沿等瞬态状态。Canvas 命令由下次 tick 重新生成；窗口位置、鼠标悬停、UI 页签等不自动持久化。

动态实体标识改为独立递增序列，并随 checkpoint 保存。读档重建 World 后继续生成实体不会因 ECS 句柄复用而覆盖已有标识；组件或私有状态中的原始实体句柄仍不应直接写入文件。

## 通用存储服务

`host->service<gm::sdk::StorageService>()` 提供 `open/close/read/write/exists`。`open(app, profile, error)` 获取会话，`close` 释放独占写入权。DLL 和宿主之间只传句柄、字节和函数指针，不跨模块传递 STL 所有权。

- 默认 Windows 路径：`%LOCALAPPDATA%/GMEngine/saves/<app>/<profile>/`。
- 测试可以用 `GM_ENGINE_USER_DATA` 重定向整个用户根目录。
- 名称只允许字母、数字、点、下划线、连字符；禁止路径穿越，单文件上限 256 MiB。
- 新内容先写入唯一临时文件，Windows 使用 `FlushFileBuffers`，备份旧完整文件后原子替换主文件。失败时抛出／返回错误，旧主文件保留。
- `.bak` 是上一份完整存档，加载坏文件不会自动采用备份，以免隐藏损坏；恢复备份由项目明确发起。
- 独占锁持续到会话关闭或进程退出。第二实例应明确进入不可保存状态或拒绝进入；不能把失败当成成功。
- POSIX 路径实现了锁和 rename，但未做 fsync 或设备测试；本版发行与耐故障验证仅针对 Windows。

“星环工厂”已接入此服务。游戏槽在 `saves/DSPNative/factory/`，引擎 checkpoint 在 `saves/DSPNative/`。首次启动会尝试迁移旧 `%LOCALAPPDATA%/GMEngine/DSPNative/autosave.dsp`，原文件保留。无写入权时界面明确提示。离线生产按游戏存档恢复策略结算；引擎 checkpoint 恢复则按保存的模拟时间继续，不再次追加离线时间。

## 输入路由

`sdk/include/gm/ui_input.hpp` 的 `UiInputRouter` 不依赖编辑器。先定义父上下文，再定义子上下文；后定义的合格区域优先命中。每个模拟 tick 调用 `begin(frame.input)`，每个上下文通过 `sample(id)` 获取经过过滤的快照。

- `modal(root)` 只允许根及其子上下文获得输入；`modal(0)` 关闭模态限制。
- `capture(id)` 只能由当前指针拥有者调用，拖到区域外仍能收到释放；失焦清除捕获。
- `focus(id)` 控制键盘接收者；无显式焦点时选择最上层合格键盘上下文。
- `enable(id, false)` 连同子上下文一起禁用；`consumePointer/consumeKeyboard` 防止同一帧重复消费。

星环工厂的制造弹窗和帮助弹窗使用该路由。回归验证制造按钮不会触发背后的科技页，弹窗打开时空格不会暂停背后的工厂。

## 实测与验收

同一 Windows / D3D11 环境、同一 1600×900 工厂场景、同样 600 tick 输入，使用 `gm_scene_capture` 预热后的计数：

| 指标 | 0.8.3 基线 | 0.8.4 |
| --- | ---: | ---: |
| Canvas 命令 | 342 | 342 |
| 四边形 | 442 | 536 |
| Draw calls | 150 | 1 |
| 测量帧文字上传 | 未记录 | 0 |
| 文字纹理页占用 | 未记录 | 4 MiB |

数字拆分增加了四边形，但显著减少纹理切换。此结果是 draw 次数优化，**不是 FPS 提高 150 倍，也不是 GPU 耗时测试**。画面检查确认布局、文字与遮挡正确，未声称逐像素一致。

新版本科技、戴森、制造页预热后也均为 1 draw、0 次文字上传。渲染契约连续 300 帧、每帧 30 个变化的中文＋数值＋单位标签，验证没有新文字纹理上传，并检查缓存预算；这不等同数小时挂机验收。

另用 1,200 个不同的静态字符串混合图集和大纹理路径，强制触发回收，验证最多 4 个图集页、2,048 条缓存记录，所统计文字纹理不超过 42 MiB。

- 11 组 CTest：覆盖原测试及新存档、迁移回滚、跨进程独占、备份失败、UI 模态/捕获/焦点。
- 游戏 351 项领域断言通过；完整输入脚本验证手工制造、科技、建造、物流、F6/F9。
- 游戏保存 checkpoint 后运行 137 帧，再读回并运行同样 137 帧，两份完整 checkpoint **逐字节相同**，覆盖私有生产状态与模拟时钟。
- 无工具链新建/编辑/运行/打包、稳定资源 ID/增量编译、多场景发布、独立模块扩展均通过脚本回归。
- 编辑器实时命令、撤销重做、恢复界面与后台构建/取消通过；后台任务 21 项、CrystalMeadow 实时场景 41 项检查通过。原有示例的生命恢复私有状态也已接入存档扩展。

源码测试入口：`tests/persistence_tests.cpp`、`tests/render_tests.cpp`。真实项目验收入口在独立游戏的 `tools/verify.ps1`。截图与完整运行输出在该项目 `docs/`，基线截图在 `D:/Demo/GMEngineBuild/feedback-084/`。

## 接下来仍需完成

E03 的正式游戏 UI（字符串绑定、列表、滚动、布局与事件）、E14 的项目自定义计数器/阶段计时、E15 的游戏模块任务接口是下一批重点。动作映射、触屏、后台策略配置、跨平台字体/应用层、内容表和音频仍未完成。本版没有引入 ECS 并行调度，也没有把游戏生产规则搬入引擎。
