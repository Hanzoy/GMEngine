# GM Engine v0.7

C++20 / bgfx / Flecs 游戏引擎与人工编辑器。GUI 和 `gmctl` 共用文档校验、编辑、撤销与保存服务；项目脚本通过公开 SDK 注册组件和 System。场景采用 ECS，UI 保持独立控件树；JSON 留在编辑期，Player 使用编译后的二进制资源。

**源码、构建缓存、引擎成品和游戏项目分开存放：**

| 目录 | 内容 |
| --- | --- |
| `D:\Demo\GMEngine` | 引擎源码、SDK 源文件、构建脚本、测试与技术文档 |
| `D:\Demo\GMEngineBuild` | CMake 构建目录、测试输出、旧缓存归档 |
| `D:\Demo\GMEngineSDK` | 构建好的编辑器、Player、CLI、公开 SDK 与运行资源 |
| `D:\Demo\GMEngineGame` | CrystalMeadow、UI 示例、素材、Unity 对照项目、示例脚本与发布包 |

本地游戏工作区另行维护，操作说明在 `../GMEngineGame/README.md`；继承与组合关系见[原生项目架构](docs/project-native-modules-v07.md)。引擎默认构建不读取游戏工作区，也不构建或复制游戏模块。

## 构建与启动

需要 Windows x64、Visual Studio 2022 C++ 工具、CMake 3.21+ 和 PowerShell 7：

```powershell
cd D:\Demo\GMEngine
./scripts/build.ps1
./scripts/launch.ps1
```

未指定项目时编辑器打开安装目录的 `editor/editor.ui.json`，不会写入源码目录。在“项目 → 打开游戏项目”选择 `.gmproject`，或直接指定外部项目：

```powershell
./scripts/launch.ps1 -Project ../GMEngineGame/CrystalMeadow/game.gmproject
# 游戏专用启动脚本还会把会话保存在游戏工作区
../GMEngineGame/scripts/launch.ps1
```

`build.ps1` 会构建、测试并安装。也可手动执行：

```powershell
cmake -S . -B ../GMEngineBuild/build -G "Visual Studio 17 2022" -A x64 -DCMAKE_INSTALL_PREFIX=../GMEngineSDK
cmake --build ../GMEngineBuild/build --config Release
cmake --install ../GMEngineBuild/build --config Release
```

安装后可直接运行 `../GMEngineSDK/bin/gm_editor.exe`。整个 SDK 目录可以复制到其他位置；程序、模板、许可与编辑器资源均按安装目录解析。`GM_ENGINE_BUILD_ROOT`、`GM_ENGINE_ROOT` 环境变量可分别覆盖脚本的构建输出根目录和 SDK 安装目录。源码目录内的 CMake 构建会被拒绝。

当前图形平台已验证 Windows / D3D11；Android、iOS、macOS、Web 后端尚未接入。`-DGM_BUILD_EDITOR=OFF` 可以构建不依赖窗口的核心、编译器和命令行。依赖版本和许可证见 [THIRD_PARTY.md](THIRD_PARTY.md)。

## 目录边界

| 目录 | 职责 |
| --- | --- |
| `src/` | 运行时、编辑服务、GUI、CLI、资源编译与项目管理 |
| `sdk/include/`、`sdk/cmake/` | 游戏项目使用的公开头文件与 CMake 接入 |
| `sdk/templates/Basic/` | 引擎“新建项目”功能的内置模板 |
| `editor/`、`resources/` | 编辑器自身 UI 和恢复模板 |
| `tests/` | 引擎契约、回归和性能测试；夹具不引用游戏素材 |
| `scripts/` | 引擎构建、启动、依赖安装、验证和测量工具 |
| `docs/` | 引擎设计、研究和历史性能报告 |

游戏专属的移动规则、生命、伤害、HUD、插画、示例 C++ 代码和集成测试位于 `../GMEngineGame`。`../GMEngineBuild/build/fixtures/` 中的测试模块和二进制 UI 是引擎测试产物，不会复制到 Player 发布目录。

## 编辑操作

顶部菜单承担文件、编辑、实体/节点、视图、工具与项目操作；中央视口、左侧层级、底部资源/输出和右侧组件检查器组成工作区。检查器还提供 System 顺序与开关、项目信息。没有 Agent 面板。

- Ctrl+S 保存，Ctrl+Z / Ctrl+Y 撤销与重做；支持多文档标签。
- F5 运行当前资源，F7 暂停场景，Shift+F7 单步，F8 停止。
- 点击游戏视图后使用项目定义的方向输入；停止后返回编辑数据。
- 修改项目 C++ 后，停止并保存所有标签，再执行“项目 → 构建项目模块与场景”。
- “工具 → 编辑编辑器界面”可编辑外壳；Ctrl+Shift+F5 应用，F6 恢复默认外壳。

本地 UI 示例说明在 `../GMEngineGame/docs/ui-example.md`；该工作区不属于本源码仓库。

## 命令行

```powershell
'{"op":"describe"}' | ../GMEngineSDK/bin/gmctl.exe --project ../GMEngineGame/CrystalMeadow/game.gmproject

@'
{"op":"preview.run"}
{"op":"runtime.step","frames":10,"x":1}
{"op":"runtime.query"}
{"op":"preview.stop"}
'@ | ../GMEngineSDK/bin/gmctl.exe --project ../GMEngineGame/CrystalMeadow/game.gmproject --stdio
```

`--stdio` 同一进程保留文档、撤销栈和运行世界，一行请求对应一行结果。编辑修改必须执行 `save` 才写盘；`expected_revision` 支持乐观并发检查。`--live SESSION` 可以操作真实编辑器内存文档：引擎启动脚本默认会话为 `../GMEngineSDK/.gmengine/session`，游戏工作区启动脚本默认会话为 `../GMEngineGame/.gmengine/session`。

Player 不包含默认示例。使用 `--project FILE` 指定资源，或在发布目录放置 `game.gmscene + game.dll`，纯 UI 发布目录使用 `main.gmui`。

## 验证

```powershell
ctest --test-dir ../GMEngineBuild/build -C Release --output-on-failure
./scripts/test-cli.ps1
./scripts/test-live.ps1
./scripts/test-project.ps1
./scripts/verify-runtime.ps1
./scripts/verify-sdk.ps1
```

上述引擎测试仅使用专用夹具和 SDK 新项目模板，不需要 `GMEngineGame`。游戏自身的实时和玩法集成测试由该工作区的 `scripts/test-scene-live.ps1`、`scripts/test-native.ps1` 提供。

当前支持 2D ECS、独立原生模块、组件与 System 编辑、UI 和单入口 Windows 发布。尚未实现 3D、物理/音频、Prefab、多场景流式加载、脚本 VM/调试器、运行状态热迁移与移动端打包。
