# 场景视窗导航（0.10.1）

场景页签使用独立的编辑器相机；游戏页签使用场景里的 Camera3D。浏览、聚焦、平移与飞行不会修改 Transform3D、Camera3D、场景文件、撤销栈或 dirty 标记。即使场景没有 Camera3D，只要有 Transform3D / MeshRenderer，也可以浏览和选择。

操作参考 [Unity 官方 Scene view navigation](https://docs.unity3d.com/Manual/SceneViewNavigation.html)，实现 Windows 下常用的鼠标与键盘导航。

| 操作 | 输入 |
| --- | --- |
| 观察方向 | 在场景画面中按住右键并移动鼠标 |
| 飞行 | 按住右键 + W/S 前后、A/D 左右、Q/E 降升；方向键和 PageUp/PageDown 也可用 |
| 临时加速 | 飞行时按住 Shift，速度为 4 倍 |
| 调整飞行速度 | 按住右键滚动滚轮，速度范围 0.01–10000 单位/秒 |
| 平移 | 中键拖动；或者 Alt+Ctrl+左键拖动 |
| 环绕 | Alt+左键拖动，围绕当前观察中心；先 F 聚焦可将中心设在所选对象上 |
| 拉近 / 拉远 | 滚轮；或者 Alt+右键拖动 |
| 聚焦对象 | F、视窗上方“聚焦”，或双击层级中的实体；包括其可渲染子级 |
| 查看全部 | 视图菜单 → 查看全部 |
| 选择物体 | 左键点击网格，最近的三角面优先；橙色包围框表示选择 |
| 手型工具 | Q 或“视图”按钮，左键拖动平移；“选择”按钮恢复点击选择 |
| 释放导航 | 松开对应鼠标键；Esc、窗口失焦、丢失捕获、切换视图、停止运行也会释放 |

2D 场景保留滚轮缩放、对象编辑；支持中键／右键／手型工具平移，F 聚焦所选。3D 场景页签充满视窗，不受游戏分辨率的宽高比限制；游戏页签保留原来的分辨率与 HUD。运行期间可以切到场景页签自由观察，再切回游戏视角；游戏输入只在游戏页签获取焦点时启用。

输入框编辑时，F/W/Q 等按文本编辑规则处理。中文输入法只在文本编辑期间启用，防止视窗快捷键被输入法组合或 Esc 被候选框截获。3D 导航使用相对鼠标输入，连续转向不受屏幕边缘限制；结束时恢复鼠标位置和可见状态。

## 状态与接口

每个场景的 position / yaw / pitch / distance / speed 以场景绝对路径为键保存到当前编辑器会话父目录的 `workspace.json`。关闭编辑器时写入，重新打开时恢复。第一次打开时从游戏相机的位置和方向初始化；没有游戏相机时框选全部网格。此状态不是游戏资源，也不会进入游戏发布包。

GUI 和 `gmctl --live SESSION` 共用相机控制逻辑。`describe` 可发现以下命令：

```json
{"op":"view.camera"}
{"op":"view.camera","set":{"position":[0,3,-8],"yaw":0,"pitch":15,"distance":8,"speed":5}}
{"op":"view.navigate","action":"orbit","x":60,"y":20}
{"op":"view.navigate","action":"pan","x":30,"y":10}
{"op":"view.navigate","action":"dolly","steps":2}
{"op":"view.navigate","action":"fly","x":0,"y":0,"z":1,"seconds":0.1,"fast":false}
{"op":"view.frame"}
{"op":"view.frame","all":true}
{"op":"view.pick","x":0.5,"y":0.5}
```

`look/orbit` 的 x/y 为逻辑像素，`pan` 为视窗像素；pick 为视窗归一化坐标。fly 单次模拟最多 0.1 秒，避免卡顿后跃迁。camera 返回视窗区域、手势、焦点和文档状态，便于调用方验证。镜头接口属于 live 编辑器，不是无窗口游戏模拟器的接口。

## 验证与范围

- `editor_camera_contract`：环绕中心、观察原地转向、平移方向、缩放中心、帧率无关飞行、斜向速度、卡顿限幅、横竖视窗聚焦、旋转/非等比缩放物体拾取。
- `world_renderer_contract`：实际 GPU 图像验证编辑器相机覆盖、返回游戏视角、无 Camera3D 渲染，以及游戏 World 不变。
- `scripts/test-editor-navigation.ps1 -InputEvents`：临时独立场景，真实 Windows 鼠标/键盘输入、CLI 操作、聚焦子级、无 Transform 的父级、运行隔离、重启恢复、场景文件字节不变。此选项会将测试窗口置前；不传时只执行 live 命令测试。报告位于构建目录 `navigation-verification.json`。

导航复用已有网格/材质 GPU 缓存，只替换视图矩阵，不复制 RenderFrame 的网格列表，也不因移动镜头重编译场景。拾取发生在点击时，以局部包围盒先筛选，再测试三角形；包围框是编辑器覆盖层，不做遮挡隐藏。透明/alpha-cutout 纹理仍按网格表面拾取。当前没有实现 Unity 的轴向 Scene Gizmo、正交切换、Shift+F 锁定跟随，以及 3D 移动/旋转/缩放操纵器。
