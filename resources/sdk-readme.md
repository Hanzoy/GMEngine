# GM Engine 0.8

双击 `bin/gm_editor.exe` 打开项目选择器。可以新建项目、打开 `.gmproject`，或继续最近项目。“编辑器界面工程”用于编辑编辑器自身 UI。

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

Windows x64 运行程序静态链接 MSVC 运行库。开发 C++ 项目需要 CMake 与 Visual Studio 2022 C++ 工具。最近项目和默认会话位于 `%LOCALAPPDATA%/GMEngine`。`--session` 可指定实时命令会话目录。安装目录可整体移动，游戏项目通过 `sdk/cmake` 接入，不需要引擎私有源码。
