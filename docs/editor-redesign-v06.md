# v0.6 场景编辑器与操作调整

日期：2026-10-29。本轮围绕完整的“选实体—改组件—运行—查看状态—停止—继续编辑”流程改动，不增加 Agent 面板。

## 参考依据

Unity 把 Hierarchy、Scene、Inspector、Project 分别用于对象组织、场景操作、属性编辑和资源管理；Inspector 根据当前选择显示组件。GM 采用相同的职责划分。[Unity 编辑窗口](https://docs.unity.com/en-us/engine/6000.6/manual/unity-editor/editor-windows-views-reference)、[Inspector](https://docs.unity.com/en-us/engine/6000.7/manual/unity-editor/editor-windows-views-reference/using-the-inspector/inspector-options)

Godot 将资源标签、工作区与运行操作分开，并提供场景树、检查器和底部面板。GM 保留文档标签，同时让场景／游戏切换与运行控制各有明确位置。[Godot 界面](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html)

Unreal 区分主工具栏、视口工具栏、Outliner 和 Details，对象列表与属性检查根据选择协作。GM 参考这种职责划分，并按本项目已确定的操作习惯，通过菜单和快捷键保存／撤销，顶部集中运行控制，视口提供选择／移动工具。[Unreal Editor Interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface)

## 已实现的布局和交互

| 区域 | 当前功能 |
| --- | --- |
| 菜单栏 | 文件、编辑、实体／节点、视图、工具；保存与撤销使用标准菜单／快捷键 |
| 主工具栏 | 运行、暂停、逐帧、停止，以及清楚的编辑／运行／暂停状态 |
| 左侧层级 | 实体树、搜索、折叠、创建和选择；不显示运行时内部存储结构 |
| 中央 | 文档标签、场景／游戏视图；编辑时可移动实体、缩放、平移、显示网格 |
| 右侧检查器 | 实体名称、父级、组件卡片、字段、添加／移除组件；运行时只读观察 |
| 底部 | 资源与控制台页签，目录、搜索、缩略图；支持场景、UI 和图片 |

编辑器外壳继续由 `editor/editor.ui.json` 描述，新增主工具栏也使用同一 UI 资源结构。场景检查器等专业控件由 C++ 实现，通过组件元数据生成字段。

操作细节：

- F5 运行当前内存数据；F7 暂停，Shift+F7 逐帧，F8 停止。
- 点击游戏画面接收键盘输入，Escape 释放；游戏输入期间暂时取消该窗口的输入法关联，回到编辑操作时恢复，避免 WASD 弹出中文候选框。
- 场景运行使用独立 World；检查器只读，停止后恢复源值，运行中数值变化不导致文档变脏。
- 字段 Enter 提交，Escape 取消，Tab 切换；拖动实体一次对应一次撤销。
- 组件依赖由共同校验逻辑处理；添加时自动补齐，移除会破坏依赖时拒绝。
- 面板可调整宽高，保留原有多文档与 UI 编辑功能。当前没有任意浮动／停靠窗口，也没有虚构尚未实现的功能按钮。

## 操作验证

实际窗口操作验证了：选择 Player、把生命值由 100 改为 85、未保存直接运行、HUD 显示 85、点击受伤后 HUD 和运行检查器同时显示 70；停止后作者值仍为 85，撤销恢复 100 且文档干净。还验证了组件菜单添加／撤销、短按 D 移动、输入法隔离，以及 1062 × 660 外窗尺寸下的布局与检查器滚动。自动实时测试补充暂停／逐帧、运行查询、运行隔离和多次运行／停止。

界面截图保存在 `docs/images/editor-v0.6.png` 与 `../GMEngineGame/docs/images/ecs-game-v0.6.png`。具体使用方式见 [完整示例说明](../../GMEngineGame/docs/scene-example.md)，ECS 选型和测量边界见 [对比验证](ecs-validation-v06.md)。

截图来自本轮交互检查，之后补充了检查器滚动条，并修正实体菜单文字和停止状态提示。最终精简 Flecs 构建通过 CTest、16 项场景实时检查和独立发布包 `--check`。补拍独立发布包窗口时，桌面操作工具返回 `GetCursorPos failed: 拒绝访问 (0x80070005)`，因此最终发布包的图形复查未记为通过。
