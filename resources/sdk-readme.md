# GM Engine SDK

这是可移动的引擎安装目录，不包含引擎私有源码、编译缓存或游戏项目。请保留整个目录结构。

- `bin/gm_editor.exe`：图形编辑器，默认打开安装目录的编辑器 UI。通过“项目 → 打开游戏项目”选择 `.gmproject`。
- `bin/gmctl.exe`：命令行编辑、项目构建与打包。
- `bin/gm_player.exe`：游戏运行程序。
- `bin/gm_uic.exe`、`bin/gm_scenec.exe`：UI 与场景资源编译器。
- `sdk/`：游戏项目使用的头文件、CMake 配置及新项目模板。
- `editor/`、`resources/`：编辑器运行资源。
- `licenses/`：第三方许可。

```powershell
./bin/gm_editor.exe --project ../GMEngineGame/CrystalMeadow/game.gmproject
'{"op":"project.build"}' | ./bin/gmctl.exe --project ../GMEngineGame/CrystalMeadow/game.gmproject
```

游戏项目的 CMake 配置指向本目录下的 `sdk/cmake`。Editor/CLI 根据自身位置寻找 SDK，因此整个目录可复制到其他位置运行。默认会话保存在本目录 `.gmengine/session`，可通过 `--session` 指定其他位置。

当前版本为 Windows x64。运行需要兼容的 Visual C++ 运行库；构建 C++ 游戏模块还需要 CMake 和 Visual Studio C++ 工具。源码使用独立的 `GMEngine` 目录维护。
