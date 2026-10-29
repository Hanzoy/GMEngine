# v0.5 运行时比较与优化结果

测量日期：2026-10-29。结论：当前 UI 原型需要解决重复图片内存、文字纹理切换和编辑器空闲重绘，已经实现；尚无证据需要把整个运行时重写。图片包体与加载峰值、完整字体排版和移动端适配仍需后续工作。

## 实测范围与条件

本机 Windows x64、Intel Core i9-14900K、32 个逻辑处理器；机器装有 RTX 4090 D 与 UHD 770。GM 使用 MSVC Release / bgfx D3D11 自动选择适配器；本次没有记录实际适配器 ID 或测量 GPU 时间，因此不作 GPU 性能结论。Unity 使用本机 **6000.6.0f1**，Windows x64、Mono、非 Development 发行构建、D3D11、UI Toolkit。

两个程序都显示相同的图片 UI：960 × 600 窗口、1120 × 720 逻辑画布、25 个节点、7 个按钮动作、3 个图片节点、2 张唯一图片。Unity 的图片设为不可读、无 mipmap、无纹理压缩、保持原始尺寸，源 JSON 只在构建脚本中解析，运行时读取序列化资源。已目视核对图片、中文、布局和按钮；字体栅格化与 UI 实现不同，并非像素级一致。

## GM 优化前后

`gm_render_bench` 强制绘制 80 帧，前 20 帧预热，后 60 帧取中位数。前端耗时只包围 `RuntimeView::render`，包含可见节点遍历、缓存查询和合批准备，**不包含最终批次 flush、GPU 执行和 present 等待**；不能换算为游戏 FPS。加载耗时为 7 次已预热文件读取、校验和构建实例的中位数，不是进程启动时间。

| 指标 | v0.4 基线 | v0.5 | 判断 |
| --- | ---: | ---: | --- |
| 图片 UI 绘制提交 / 帧 | 30 | 26 | 减少约 13%；保持透明绘制顺序 |
| 图片 UI 前端时间 | 24.8 μs | 12.3 μs | 本机该样本约减半，非完整帧耗时 |
| 300 个不同文本节点提交 / 帧 | 301 | 2 | 背景一批、文字图集一批 |
| 300 文本前端时间 | 97.3 μs | 30.3 μs | 减少约 69% |
| 计数器提交 / 帧 | 10 | 7 | 数字图集继续复用 |
| 上传后 CPU 图片像素容量 | 12,581,520 B | 0 B | 文件资源释放像素 vector 容量 |
| 图片二进制加载 | 23.706 ms | 22.910 ms | 没有实质改变加载路径，不宣称加载加速 |

两种压力场景的预热帧均无新增文字或图片上传。300 文本 CSV 中的 `load_ms` 是同次图片资源加载的记录，不是这个内存生成的文本场景加载时间。原始数据：`build/perf-before/render.csv`、`build/perf-after/render.csv`；测试源码：`tests/perf/render_bench.cpp`。

### 已实施的优化

1. **图片上传后释放 CPU 像素。** 尺寸和索引仍保留，GPU 纹理继续共享。新的视图确实需要重建纹理时，可从原二进制恢复像素，保留当前动作状态；源文件已改变则要求显式重新加载。没有文件来源的内存实例保留像素。加载和上传期间的临时缓冲仍存在，不能说加载峰值为零。Unity 的 `TextureImporter.isReadable=false` 同样避免保留额外可读像素副本。[Unity 官方说明](https://docs.unity.com/en-us/engine/6000.6/script-reference/unityeditor/textureimporter/isreadable)

2. **通用文字共享图集，矩形和 UV 同时裁剪。** 轴对齐 UI 无需为每个不同裁剪框切换 GPU scissor；相邻且纹理一致的绘制可以合批，保留原有顺序。文字图集最多 4 页，每页 1024² RGBA8，共 16 MiB；按页淘汰缓存。超大文字纹理及数字图集另计，并非所有文字内存固定为 16 MiB。这是整段文字缓存图集，还不是 Unicode 字形布局引擎。Unity UI Toolkit 也通过图集减少纹理导致的批次中断。[Unity 官方说明](https://docs.unity.com/en-us/engine/6000.0/manual/uitoolkits/uielements/uie-support-for-runtime-ui/uie-performance-consideration-runtime)

3. **编辑器按事件重绘。** 输入、文档变化、窗口变化和提示到期触发绘制；空闲时等待消息，约每 100 ms 处理一次 CLI 邮箱。Player 原本已经采用静态事件驱动，不能把它记作本版新增收益。UE 的失效缓存与事件驱动建议也强调避免无变化时重复做布局和绘制；GM 暂时采用更简单的整个窗口失效机制。[Unreal 官方说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine)

## 与其他引擎的占用对照

进程采样每次 2 秒、连续 3 次；私有内存表示 Private Bytes，工作集表示物理驻留，不含独立显存统计。CPU 是进程消耗换算为**一个逻辑核**的百分比，换算整机百分比需除以 32。短采样中 0 表示未观察到计时增量，不代表没有任何工作。驱动、分配器、后台活动与窗口状态会影响结果。

| 相同图片 UI | 进程私有内存 | 工作集 | 静态 CPU 样本 | 发布目录 |
| --- | ---: | ---: | ---: | --- |
| GM v0.4 | 137.09 MiB | 79.71 MiB | 0% | 保留基线 exe 与资源 |
| GM v0.5 | 约 134 MiB | 约 78 MiB | 0–1.6% | 13.51 MiB，含资源与许可 |
| Unity 6000.6.0f1 | 580.95 MiB | 约 260.76 MiB | 7.6–10.1% | 102.38 MiB，完整构建输出 |

**这不是引擎速度排名。** Unity 以 60 FPS 持续运行；GM 没有动画或游戏循环工作时等待事件。只可比较此用例的静态占用，不能据此宣称 GM 比 Unity 快多少倍。Unity 的 Mono、字体、资源和通用子系统也提供了 GM 尚未具备的能力。释放约 12 MiB 像素后，进程私有内存没有等量下降：新增图集、驱动缓冲与分配器保留内存都会改变快照。

编辑器基线在未操作时仍持续渲染，3 次样本单核等效 CPU 为约 5.5–14.8%，私有内存 166.87 MiB；新版对应样本约 0.8–1.6%，私有内存 154.74 MiB，已不再持续重绘。新版打开了两个文档标签并显示图片缩略图，编辑器快照不是严格相同 UI 状态的分配对照。编辑器测量与 Player 分开记录，不能用编辑器占用作为游戏运行时开销。原始记录在 `build/perf-before/*-idle.json`、`build/perf-after/*-idle.json`、`../GMEngineGame/comparison/build/unity-idle.json`。

Godot 本机有 4.7 Mono 编辑器，但没有匹配的 Windows release 导出模板；本次没有拿包含编辑功能的可执行文件冒充发行 Player。Unreal 没有可用于相同场景的发行构建，本次不提供推测的 CPU、内存、包体或 FPS 数字。[Godot Windows 导出机制](https://docs.godotengine.org/en/stable/tutorials/export/exporting_for_windows.html)

| 引擎机制 | 对 GM 的启发与本版取舍 |
| --- | --- |
| Unity UI Toolkit 图集、使用提示，纹理默认不可读 | 已实施文字图集与上传后像素释放；避免逐帧重新解释 JSON |
| Godot 离线导入、按用途设置压缩和 mipmap | 已离线解码；暂不对 UI 图片一律有损压缩。2D 图片、像素画和 3D 纹理需求不同。[官方图片导入指南](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_images.html) |
| Unreal UMG 缓存布局/绘制，事件驱动更新 | 已缓存 Player 布局并使编辑器按事件绘制；尚无必要增加消耗额外渲染目标的离屏层缓存 |

## 仍需优化的部分与时机

- **图片包体、加载峰值：下一优先级。** 两张 PNG 原始合计约 3.98 MiB，当前 RGBA 二进制约 12 MiB，GPU 像素载荷也约 12 MiB。离线按目标分辨率缩小、纹理压缩或无损包压缩应在真实移动设备上比较画质、上传速度和峰值。当前没有 Android/iOS 后端与同设备基准，不能直接宣称使用某种压缩一定更快。
- **跨界面资源共享：多界面加载时再做。** 当前只在一个 UI 内按路径去重；真实游戏多个界面共用素材时，独立纹理包、稳定资源 ID 和引用计数会更有价值。
- **复杂文字、长列表与动画：有这些功能时做对应基准。** 完整字形图集、文本排版、列表虚拟化、局部重绘仍未实现。当前 300 文本前端约 0.03 ms，尚不支持以此为理由大规模重写。文本变化频繁时整段栅格化仍可能产生尖峰。
- **图形后端预算：保持可测。** bgfx、驱动、交换链和临时顶点缓冲有固定占用；不能从图片尺寸直接推算整个进程内存。将来应增加 GPU 时间、上传峰值和目标设备电耗测量。

## 复现与验证

```powershell
./scripts/build.ps1
./build/Release/gm_render_bench.exe ../GMEngineGame/UIExamples/build/images.gmui
./scripts/test-cli.ps1
./scripts/test-live.ps1
./scripts/verify-runtime.ps1

# 需要本机已安装并已许可的 Unity 6000.6.0f1 与 Windows Build Support
./../GMEngineGame/comparison/tools/build-unity.ps1
# 启动 ../GMEngineGame/comparison/build/Unity/Build/UnityUI.exe，等待界面稳定后：
./scripts/measure-process.ps1 -ProcessId <进程ID> -Output ../GMEngineGame/comparison/build/unity-idle.json
```

自动验证覆盖合批、300 文本图集、20 次计数零新增文字上传、图片复用、动作不重新上传、三次重载不增加图片纹理数量、像素容量释放与恢复后运行状态保留，以及多文档撤销隔离、复制子树的内部引用、二进制发布不依赖 JSON 或图片解码器。图集可更新属性、中文显示、图片添加/撤销和编辑器 F5 启动 Player 另经实际窗口检查。
