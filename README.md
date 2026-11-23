# GM Engine

GM Engine 是用 C++20 编写的 2D / 3D 游戏引擎，包含 Windows 编辑器、独立的游戏播放器（Player）和 C++ 游戏模块 SDK。编辑器和命令行工具 `gmctl` 共用场景、UI 和项目服务，开发者可以在界面中编辑，也可以用 JSON 命令自动化相同的操作。

场景用 Flecs 的实体组件系统组织，游戏 UI 使用独立的控件树，图形渲染由 bgfx 提供。场景、UI 和素材在开发时编辑和导入，Player 加载编译后的场景、资源和游戏模块。

## 在 Windows 上构建并启动

准备 Windows x64、Git、PowerShell 7、CMake 3.21 或更新版本，以及 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载和 Windows SDK。在 PowerShell 7 中运行以下命令。

```powershell
git clone https://github.com/Hanzoy/GMEngine.git
Set-Location GMEngine
./scripts/build.ps1
./scripts/launch.ps1
```

首次构建会下载固定版本的依赖、编译、运行测试，并将引擎安装到源码目录旁的 `GMEngineApp/`。最后一条命令打开项目选择器。已有安装目录时，可以直接启动其中的 `bin/gm_editor.exe`。

## 创建并打包第一个游戏

1. 在项目选择器里点“新建项目”，选一个已有的父目录，输入新文件夹名，例如 `MyGame`。引擎会创建 `game.gmproject`、基础场景和 C++ 模块，并打开项目。
2. 按 `F5` 运行，按 `F8` 停止。基础模板带有已编译的 Windows 模块，可以直接试玩。修改 `Scripts/` 中的 C++ 后，保存并选择“项目 → 构建项目模块与场景”。
3. 保存修改，选择“项目 → 生成 Windows 运行包”。打开游戏项目的 `dist/gm_player.exe` 即可试玩打包结果。分发游戏时复制整个 `dist/` 目录。

同样的构建和打包操作也可以由 `gmctl` 执行。安装目录中的 README 提供可复制的命令。[安装版使用指南](resources/sdk-readme.md)

## 源码、安装和游戏项目放在哪里

| 目录 | 用途 |
| --- | --- |
| `GMEngine/` | 本仓库。包含引擎源码、SDK 接口与模板、编辑器资源、构建脚本和引擎自身的测试。 |
| `GMEngineBuild/` | 构建引擎时生成。`build/` 存放 CMake 中间文件，`releases/` 存放制作好的 Windows 安装程序、便携 ZIP 和校验文件。 |
| `GMEngineApp/` | 源码构建后的默认安装目录。`bin/` 里是编辑器、Player 和命令行程序，`sdk/` 供游戏的 C++ 模块使用。 |
| `GMEngineGame/projects/<游戏名>/` | 本地游戏工作区的一种布局。每个游戏有自己的清单、代码、素材、项目专用工具、测试和对照资料，也可以放在其他位置。 |

恢复一个游戏项目时，保留 `game.gmproject`、`CMakeLists.txt`、`Scripts/`、原始素材和项目使用的依赖。素材旁已有的 `.gmmeta` 保存稳定的资源 ID，应和素材一起保留。导入索引 `.gm/assets.json`、编译缓存以及 `build/`、`dist/` 中的生成物可重新生成。

需要制作引擎安装程序时，在完成默认 Release 构建后，从源码目录运行 `./scripts/package-editor.ps1`。`GM_ENGINE_BUILD_ROOT` 和 `GM_ENGINE_ROOT` 可分别覆盖构建输出根目录和安装目录。

## 平台支持

| 平台 | 当前可以做什么 |
| --- | --- |
| Windows x64 | 使用图形编辑器、Direct3D 11 预览和独立 Player，生成 Windows 游戏运行包。 |
| Android ARM64 | 从引擎源码调用脚本，为原生 C++ 项目生成调试签名 APK。已有构建、签名和对齐验证记录，真机运行仍待验证。 |

Android 工具链将由引擎统一管理。规划中的安装程序可勾选 Android，一并安装 GM 打包组件和所需工具。已有 SDK、NDK 的用户可在“引擎设置 → 平台工具链”选择目录，由引擎检查并记住路径，供同机的游戏复用。游戏项目只保存目标平台和打包参数。目前可用的是源码打包脚本。[Android 打包步骤与工具链规划](docs/engine-v09.md#android)

## 开发参考

- 编写游戏组件与系统可以从基础模板和公开头文件入手。[Basic 模板](sdk/templates/Basic) · [C++ 模块接口](sdk/include/gm/game.hpp) · [原生模块设计记录](docs/project-native-modules-v07.md)
- 编辑器支持场景相机飞行、环绕、平移和聚焦。[场景视窗操作](docs/editor-navigation.md)
- 脚本和 agent 可以通过 `gmctl` 向目标项目或会话发送 `{"op":"describe"}`，查询该版本支持的命令。
- 源码版本记录在 `CMakeLists.txt`，已安装的引擎版本和模块 ABI 记录在安装目录的 `engine-sdk.json`。渲染、运行时服务和历次验证记录可按主题查阅。[技术文档](docs/)

第三方组件的版本、来源与许可列在依赖说明中。仓库目前没有为 GM Engine 自身指定许可证。[第三方依赖与许可](THIRD_PARTY.md)
