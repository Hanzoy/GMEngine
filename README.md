# GM Engine 0.8

C++20 / bgfx / Flecs 游戏引擎。开发者使用图形编辑器，AI 使用 `gmctl`；二者调用相同的编辑与项目服务。场景采用 ECS，游戏 UI 保持独立控件树。JSON、图片导入和资源编译留在编辑期，Player 读取二进制资源。

## 安装和启动

本地发行包位于 `D:\Demo\GMEngineBuild\releases`：

- `GMEngine-0.8.0-Windows-x64-Setup.exe`：按用户安装、开始菜单入口、可选桌面快捷方式、项目文件关联和卸载程序。
- `GMEngine-0.8.0-Windows-x64.zip`：便携版，保留完整目录结构即可移动。

启动 `bin/gm_editor.exe` 会显示项目选择器，可以打开、新建、继续最近项目。SDK 是安装目录中供 C++ 项目使用的 `sdk/` 子目录，完整产品名称是 **GM Engine**。

```powershell
./scripts/build.ps1      # 构建、测试、安装到相邻 GMEngineApp
./scripts/launch.ps1     # 打开项目选择器
./scripts/package-editor.ps1  # 制作安装程序及便携包
```

构建需要 Windows x64、Visual Studio 2022 C++ 工具、CMake 3.21+、PowerShell 7。运行程序静态链接 MSVC 运行库；构建原生游戏模块仍需要 C++ 工具链。

## 目录

| 目录 | 职责 |
| --- | --- |
| `D:\Demo\GMEngine` | 引擎源码、公开 SDK 源文件、编辑器资源、测试和文档 |
| `D:\Demo\GMEngineBuild` | CMake 中间文件、测试输出、安装包、旧产物归档 |
| `D:\Demo\GMEngineApp` | 已构建／安装的 GM Engine 应用 |
| `D:\Demo\GMEngineGame` | 独立游戏工作区；项目在 `projects/`，UI 示例在 `samples/` |

源代码目录中：`src/` 是引擎与工具实现，`sdk/` 是模块 API 和模板，`editor/` 与 `resources/` 是编辑器自身资源，`tests/` 是独立引擎测试，`scripts/` 是构建与验证工具。引擎构建不依赖游戏工作区。

`GM_ENGINE_BUILD_ROOT` 和 `GM_ENGINE_ROOT` 分别覆盖脚本的构建输出根目录和安装目录。用户的最近项目、默认编辑器会话、运行存档放在 `%LOCALAPPDATA%/GMEngine`，不放进安装包。测试可用 `GM_ENGINE_USER_DATA` 隔离用户数据。

## 使用

打开 `D:\Demo\GMEngineGame\projects\CrystalMeadow\game.gmproject`。Ctrl+S 保存，Ctrl+Z / Ctrl+Y 撤销与重做，F5 运行，F7 暂停，Shift+F7 单步，F8 停止。右侧提供组件、System、项目和性能页。修改 C++ 后停止、保存，再执行“项目 → 构建项目模块与场景”。

```powershell
$cli='../GMEngineApp/bin/gmctl.exe'
'{"op":"project.recent"}' | & $cli --hub
'{"op":"project.create","directory":"D:/Games/MyGame","name":"My Game"}' | & $cli --hub
'{"op":"project.build"}' | & $cli --project D:/Games/MyGame/game.gmproject
'{"op":"project.package"}' | & $cli --project D:/Games/MyGame/game.gmproject
```

游戏发布到项目 `dist/`，入口为 `gm_player.exe`，包含原生模块、二进制入口场景及其他场景目录，不包含 JSON、C++ 或原始图片。`--check` 可进行无窗口加载验证。

当前已验证 Windows / D3D11。Android、iOS、macOS、Web 平台层尚未实现；bgfx 的跨平台能力不等于应用平台层已经完成。

详细变更、命令、运行约定、验证和逐模块剩余工作见 [0.8 设计与验收](docs/engine-v08.md)。原生模块的组合方式见 [项目架构说明](docs/project-native-modules-v07.md)，该文中的 ABI 7 为历史版本，当前 ABI 为 8。依赖许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。
