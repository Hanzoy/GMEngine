# GM Engine 安装版使用指南

从安装目录启动 `bin/gm_editor.exe` 即可进入项目选择器。选择一个已有的 `.gmproject`，或点“新建项目”，在已有的父目录下输入新文件夹名。引擎会复制 Basic 模板并打开新项目。

## 编辑、试玩与打包

1. 按 `Ctrl+S` 保存，按 `F5` 试玩，按 `F8` 停止。新项目自带编译好的 `bin/windows-x64/Game.dll`，第一次试玩或生成 Windows 运行包可以直接使用。
2. 修改游戏的 `Scripts/` 后，停止试玩、保存并选择“项目 → 构建项目模块与场景”。重新编译 C++ 需要 CMake 和 Visual Studio 2022 的 C++ 桌面开发工具。自定义模块从源码恢复时也需要这套工具。需要指定 CMake 时，将 `GM_ENGINE_CMAKE` 设为 `cmake.exe` 的完整路径。
3. 选择“项目 → 生成 Windows 运行包”。在项目的 `dist/` 中启动 `gm_player.exe`，分发时保留整个 `dist/`，其中含模块、场景、运行资源和许可。

安装程序支持为当前用户注册 `.gmproject` 文件关联。已完成关联时，双击清单即可打开。使用便携版时，可启动 `bin/gm_editor.exe`，在项目选择器中打开清单。

## 用命令行创建游戏

以下命令从引擎安装目录运行。示例在当前用户的 `Games` 下创建 `MyGame`，请为新项目选择尚不存在的文件夹名。

```powershell
$game = Join-Path $env:USERPROFILE 'Games/MyGame'
$manifest = Join-Path $game 'game.gmproject'
$create = @{
    op = 'project.create'
    directory = $game
    name = 'MyGame'
}
$create | ConvertTo-Json -Compress | ./bin/gmctl.exe --hub
'{"op":"project.package"}' | ./bin/gmctl.exe --project $manifest
```

`gmctl` 返回 JSON，`ok` 为 `true` 表示操作成功。修改 C++ 后，在打包前执行模块构建。以下命令沿用上面的 `$manifest`。

```powershell
'{"op":"project.build"}' | ./bin/gmctl.exe --project $manifest
'{"op":"project.package"}' | ./bin/gmctl.exe --project $manifest
```

要无窗口检查 Windows 运行包，在游戏项目的 `dist/` 目录执行 `./gm_player.exe --check`，成功时退出码为 0。

## 在安装目录中查找

| 位置 | 内容 |
| --- | --- |
| `bin/` | 编辑器、Player、`gmctl`、场景编译器 `gm_scenec` 和 UI 编译器 `gm_uic`。 |
| `sdk/include/gm/`、`sdk/cmake/` | 游戏模块使用的 C++ 接口和 CMake 配置。 |
| `sdk/templates/Basic/` | 创建游戏时复制的模板，包括源码、场景和预编译模块。 |
| `engine-sdk.json` | 当前安装的引擎版本和游戏模块 ABI。 |
| `docs/`、`licenses/` | 随版本提供的技术说明和第三方许可。 |

脚本和 agent 可向 `gmctl` 发送 `describe` 查询目标版本提供的接口。

Android 调试 APK 目前需要从对应版本的引擎源码运行打包脚本。规划中的安装程序可选装 Android 组件和所需工具，也可稍后在“引擎设置 → 平台工具链”导入已有 SDK、NDK，供同机的各个游戏共用。当前脚本参数见本安装的 `docs/engine-v09.md`。[GM Engine 源码与构建说明](https://github.com/Hanzoy/GMEngine)
