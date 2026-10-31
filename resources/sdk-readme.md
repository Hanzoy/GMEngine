# GM Engine 0.10.0

0.10.0 提供通用材质、自定义 Shader、mipmap、BC3 / ASTC / ETC2、HDR 后处理和基础 3D 渲染。参见 `docs/rendering-v010.md`。安装包可包含 `tools/shaderc.exe`；内置材质和发布后的 Player 无需该工具。场景二进制 v3 可读取原 v2，SDK ABI 保持 10。

0.9.1 优化 2D 渲染：空间查询、共享图片与小图集、静态 GPU 几何缓存、位置插值、性能面板和 `runtime.render.stats`。保持 ABI 10 与资源格式，0.9.0 模块可继续使用。详见 `docs/rendering-v091.md`。

0.9.0 提供原生局域网联机、后台任务、音频、异步资源、独立预制体、UI 流布局 / 字符串绑定 / 虚拟列表、动作映射和多点触摸。Android 最小 Player 和构建脚本在引擎源码中，已生成调试 APK，尚未真机验证。Steam 后端未实现。详细使用及限制见 `docs/engine-v09.md`。

当前 SDK 为 **ABI 10**，ABI 10 模块可继续使用；ABI 8 / 9 模块需重新构建。Frame::input 包括键鼠、触摸和手柄；Context::service 查询 World 服务。Player 在后台继续模拟，最小化时不渲染。下方 ABI 8 / 9 描述属于历史记录。

0.8.4 保持 ABI 9，新增可选 `gm_game_extensions` 导出、System 私有状态 checkpoint、存储服务和模态输入路由。动态数字与静态文本共享图集，纯色图形可与文本按原顺序合批。接入说明和边界见 `docs/dsp-feedback-v084.md`。

双击 `bin/gm_editor.exe` 打开项目选择器。可以新建项目、打开 `.gmproject`，或继续最近项目。“编辑器界面工程”用于编辑编辑器自身 UI。

新项目自带已编译的 `bin/windows-x64/Game.dll`，可以立即编辑场景、运行和打包，无需安装 CMake。0.8.0 新建失败的项目，如果模板 C++ 与 CMake 未修改，打开时会自动补齐缺失模块，保留场景与资源。

本目录是完整的应用发行版，`sdk/` 只是其中供游戏 C++ 模块使用的公开开发接口。

| 目录 | 内容 |
| --- | --- |
| `bin/` | Editor、Player、gmctl、UI 与场景编译器 |
| `sdk/include/gm/game.hpp` | 原生组件、System、运行时实体、事件、输入、Canvas 接口，ABI 10 |
| `sdk/include/gm/extensions.hpp` | 可选服务查询、存储与私有 System 存档回调 |
| `sdk/include/gm/ui_input.hpp` | Canvas 项目的模态、焦点、指针捕获和输入消费 |
| `sdk/include/gm/network.hpp`、`jobs.hpp`、`audio.hpp` | 原生网络、游戏后台任务、音频服务 |
| `sdk/include/gm/resources.hpp`、`ui.hpp`、`input.hpp` | 异步资源 / 预制体、UI 服务、动作映射 / 触摸适配 |
| `sdk/cmake/` | 独立游戏模块的 CMake 接入 |
| `sdk/templates/Basic/` | 新建项目模板 |
| `editor/`、`resources/` | 编辑器界面及恢复资源 |
| `licenses/` | 第三方许可 |

```powershell
./bin/gm_editor.exe
./bin/gm_editor.exe --project D:/Games/MyGame/game.gmproject
'{"op":"project.create","directory":"D:/Games/MyGame","name":"My Game"}' | ./bin/gmctl.exe --hub
'{"op":"project.build"}' | ./bin/gmctl.exe --project D:/Games/MyGame/game.gmproject
'{"op":"project.package"}' | ./bin/gmctl.exe --project D:/Games/MyGame/game.gmproject
```

发布目录为项目的 `dist/`。完整保留 Player、game.dll、game.gmscene、scenes / prefabs 目录及目录表、assets 音频与许可证。

Windows x64 运行程序静态链接 MSVC 运行库。修改 C++ 脚本并重新构建，需要 CMake 与 Visual Studio 2022 C++ 工具；可设置 `GM_ENGINE_CMAKE` 为 `cmake.exe` 完整路径。打开含有自定义代码、但缺少已编译 DLL 的项目也需要工具链。最近项目和默认会话位于 `%LOCALAPPDATA%/GMEngine`。`--session` 可指定实时命令会话目录。安装目录可整体移动，游戏项目通过 `sdk/cmake` 接入，不需要引擎私有源码。

0.8.2：项目构建、资源导入和打包在后台执行，状态栏与项目页显示耗时并支持取消。任务期间可浏览场景，保存、修改资源、启动模拟和切换项目需等任务结束。关闭编辑器会取消其任务及构建子进程。实时 CLI 默认等待最终结果；使用 `async:true` 后，通过 `project.task.status` / `project.task.cancel` 查询和取消，离线 CLI 保持同步。游戏 SDK 仍为 ABI 8，ECS System 尚未并行执行。
