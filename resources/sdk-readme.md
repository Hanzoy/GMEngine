# GM Engine 0.8.3

当前 SDK 为 ABI 9，旧游戏模块需重新编译。新增 Frame::input（设计坐标键鼠快照）、Context::draw（有界 Canvas 命令）、runtime.input / runtime.canvas；Player 在后台继续模拟，最小化时不渲染。0.8.2 的 ABI 8 描述属于下方历史记录。

双击 `bin/gm_editor.exe` 打开项目选择器。可以新建项目、打开 `.gmproject`，或继续最近项目。“编辑器界面工程”用于编辑编辑器自身 UI。

新项目自带已编译的 `bin/windows-x64/Game.dll`，可以立即编辑场景、运行和打包，无需安装 CMake。0.8.0 新建失败的项目，如果模板 C++ 与 CMake 未修改，打开时会自动补齐缺失模块，保留场景与资源。

本目录是完整的应用发行版，`sdk/` 只是其中供游戏 C++ 模块使用的公开开发接口。

| 目录 | 内容 |
| --- | --- |
| `bin/` | Editor、Player、gmctl、UI 与场景编译器 |
| `sdk/include/gm/game.hpp` | 原生组件、System、运行时实体与事件接口，ABI 8 |
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

发布目录为项目的 `dist/`。保留 Player、game.dll、game.gmscene、scenes.gmcatalog、scenes/ 和许可证。

Windows x64 运行程序静态链接 MSVC 运行库。修改 C++ 脚本并重新构建，需要 CMake 与 Visual Studio 2022 C++ 工具；可设置 `GM_ENGINE_CMAKE` 为 `cmake.exe` 完整路径。打开含有自定义代码、但缺少已编译 DLL 的项目也需要工具链。最近项目和默认会话位于 `%LOCALAPPDATA%/GMEngine`。`--session` 可指定实时命令会话目录。安装目录可整体移动，游戏项目通过 `sdk/cmake` 接入，不需要引擎私有源码。

0.8.2：项目构建、资源导入和打包在后台执行，状态栏与项目页显示耗时并支持取消。任务期间可浏览场景，保存、修改资源、启动模拟和切换项目需等任务结束。关闭编辑器会取消其任务及构建子进程。实时 CLI 默认等待最终结果；使用 `async:true` 后，通过 `project.task.status` / `project.task.cancel` 查询和取消，离线 CLI 保持同步。游戏 SDK 仍为 ABI 8，ECS System 尚未并行执行。
