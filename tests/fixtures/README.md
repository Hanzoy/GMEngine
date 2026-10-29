# 引擎测试夹具

此目录只保留引擎契约测试所需的输入数据，不是可开发或发布的游戏项目。

- `ui/empty.ui.json`：命令行文档创建、保存测试。
- `ui/counter.ui.json`：固定节点索引、动作引用、布局缓存和旧格式兼容测试；只保留测试布局和数值动作。
- `ui/images.ui.json`：图片复用、透明度、适配与着色动作测试；使用下面两张 2×2 图片，不引用游戏素材。
- `images/rgba.png`、`images/rgb.jpg`：最小 PNG/JPEG 解码夹具。
- `counter-v1.gmui`、`editor-v0.1.ui.json`：旧资源格式与文档回归夹具。

SDK Basic 模板用于原生模块/ECS 契约测试。CrystalMeadow 的玩法与完整 UI 示例位于独立的 `GMEngineGame` 工作区。
