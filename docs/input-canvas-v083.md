# ABI 9 输入与动态 Canvas

`Frame::input` 是一次固定模拟 tick 的输入快照。`mouseX/mouseY` 是场景设计坐标，已去除窗口留黑；场景之外可能为负数。`focused` 表示宿主是否向游戏提供输入，失焦时得到清空的快照。`buttons/pressed/released` 的位分别为左键 1、右键 2、中键 4。`wheel` 以一格为单位，可带小数。`held(key)` / `hit(key)` 查询按住和首次按下，当前 Windows 后端使用 VK 值，例如 R=82、Esc=27、F6=117。

SceneRuntime::advance 保留没有模拟步的帧上收到的边沿，在下一次 tick 消费一次。多步补帧不会重复按下/释放/滚轮。直接调用 tick 则由调用者提供该步输入。快照不保留同一宿主帧内多个点击各自的坐标；完整输入事件队列和平台无关动作映射仍待实现。

`Context::draw(CanvasCommand)` 把指令复制到所属 World，每次 tick 开始清空。绘制发生在 ECS Sprite 之后、已有静态 HUD 之前，Player 和编辑器运行视图共用 SceneView。Canvas 使用场景设计坐标，不受 Camera2D 影响，游戏自行处理世界画布坐标。

| CanvasKind | 字段 |
| --- | --- |
| Rect | x/y/w/h，RGBA packed color（R 在低 8 位） |
| Border | 同上，size 为边框宽度 |
| Line | x/y 是起点，w/h **是终点坐标**，size 为线宽 |
| Text | x/y 为左上，w 为最大宽度，size 为字号，text 为最多 255 字节的 UTF-8 |
| Clip | x/y/w/h 替换当前裁剪，并与场景留黑范围相交 |
| ResetClip | 恢复场景范围裁剪；不是嵌套 push/pop |

单 tick 最多 65536 条指令，数值必须有限，文本必须有终止符。没有图像、材质、动态 UI 控件树、无障碍语义或自动命中；图像继续使用 SpriteRenderer / 图集。Canvas 是可用的基础绘制接口，不是完整 UI 框架。

## 无窗口回归

`gmctl --project game.gmproject --stdio` 按行接收：

```json
{"op":"preview.run"}
{"op":"runtime.input","mouseX":400,"mouseY":250,"pressed":1,"buttons":1,"frames":1}
{"op":"runtime.input","released":1,"frames":30}
{"op":"runtime.input","keyPress":[82]}
{"op":"runtime.canvas"}
{"op":"runtime.stats"}
```

runtime.input 立即推进 1..10000 个固定步，pressed/released/wheel/keyPress 只作用于第一步，keys/buttons 作用于整组。使用设计坐标，**不会向 OS 发送输入**。runtime.canvas 返回当前绘制指令。实时编辑器调试时建议先暂停，以使脚本结果可重复。

源码构建额外提供 `gm_scene_capture`（开发工具，不加入游戏发布包）：

```powershell
../GMEngineBuild/build/Release/gm_scene_capture.exe game.gmscene Game.dll output.png inputs.json 1600 900
```

inputs.json 是 runtime.input 等命令的 JSON 数组，可用 `-` 表示无命令。工具创建自己的隐藏 D3D11 渲染窗口，使用完整 SceneRuntime / SceneView 渲染，从 bgfx 读取像素并保存 PNG；不截取桌面、不操纵用户窗口。工具输出 Canvas 指令数、四边形数、draw 次数和最后一个模拟 tick 的 CPU 毫秒数。该时间不是帧率或 GPU 时间。
