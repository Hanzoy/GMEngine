# GM Engine 0.8.5（开发版）

C++20 / bgfx / Flecs 游戏引擎。开发者使用图形编辑器，AI 使用 `gmctl`；二者调用相同的编辑与项目服务。场景采用 ECS，游戏 UI 保持独立控件树。JSON、图片导入和资源编译留在编辑期，Player 读取二进制资源。

0.8.5 修复新版 MSVC 下字符串与 JSON 比较的编译错误，以及英文 Windows 下中文项目路径的编码错误。文件操作保留原生路径，日志与命令响应统一使用 UTF-8；SDK 仍为 ABI 9。

0.8.4 根据原生工厂项目的反馈增加 System 私有状态存档与版本迁移、事务恢复、独占存储服务、UI 输入路由，并优化动态文本与纯色图形合批。核心保持 ABI 9，通过可选扩展和服务查询接入；ABI 8 模块仍需重编译。实测同一工厂画面的 draw 从 150 降至 1，游戏读档后续跑结果逐字节一致。详细接口、证据与限制见 [0.8.4 反馈落实](docs/dsp-feedback-v084.md)。

0.8.3 已提供键鼠快照、动态 Canvas、CLI 输入注入、场景截图和 Player 后台模拟。制作过程及原始 17 项反馈见 [DSP 复刻报告](docs/dsp-reproduction-engine-notes.md)，输入接口见 [输入与画布 API](docs/input-canvas-v083.md)。独立游戏位于 `D:\Demo\GMEngineGame\projects\DSPNative`。

## 安装和启动

0.8.5 本地发行包位于 `D:\Demo\GMEngineBuild\releases`：

- `GMEngine-0.8.5-Windows-x64-Setup.exe`：按用户安装、开始菜单入口、可选桌面快捷方式、项目文件关联和卸载程序。
- `GMEngine-0.8.5-Windows-x64.zip`：便携版，保留完整目录结构即可移动。

启动 `bin/gm_editor.exe` 会显示项目选择器，可以打开、新建、继续最近项目。SDK 是安装目录中供 C++ 项目使用的 `sdk/` 子目录，完整产品名称是 **GM Engine**。

安装包包含已编译的基础项目模块。新建项目后可以直接编辑、运行和打包，无需安装 CMake 或 Visual Studio；模块放在项目 `bin/windows-x64/Game.dll` 中。0.8.0 新建失败留下的项目，如果模板 C++ 和 CMake 文件未修改，升级后打开时会自动补齐模块，保留已有场景与资源。

```powershell
./scripts/build.ps1      # 构建、测试、安装到相邻 GMEngineApp
./scripts/launch.ps1     # 打开项目选择器
./scripts/package-editor.ps1  # 制作安装程序及便携包
```

构建引擎源码需要 Windows x64、Visual Studio 2022 C++ 工具、CMake 3.21+、PowerShell 7。运行程序静态链接 MSVC 运行库。修改项目 C++ 后重新构建，或打开缺少 DLL 的自定义项目，仍需要 C++ 工具链。引擎查找 PATH、CMake 标准安装目录和 Visual Studio 常见安装目录中的 CMake，也可设置 `GM_ENGINE_CMAKE` 为 `cmake.exe` 的完整路径；Visual Studio 需要“使用 C++ 的桌面开发”和 CMake 工具。自定义项目不会被替换为基础模板。

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

0.8.2 将编辑器的构建、资源导入、打包移到后台，状态栏和项目面板可取消；成功后主线程验证并加载模块。默认实时 CLI 仍等待最终结果，传入 `"async":true` 可立即获取任务 ID，通过 `project.task.status` / `project.task.cancel` 查询和取消。离线 CLI 仍同步执行。

底层增加固定工作线程池和有界队列，Player 场景读取复用线程，支持取消。ECS System 仍串行；场景解码、World 构建、纹理提交仍在主线程。详见 [后台任务与多线程边界](docs/engine-v08.md)。

当前已验证 Windows / D3D11。Android、iOS、macOS、Web 平台层尚未实现；bgfx 的跨平台能力不等于应用平台层已经完成。

详细变更、命令、运行约定、验证和逐模块剩余工作见 [0.8 设计与验收](docs/engine-v08.md)。原生模块的组合方式见 [项目架构说明](docs/project-native-modules-v07.md)，该文中的 ABI 7 为历史版本，当前源码 ABI 为 9。依赖许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。
