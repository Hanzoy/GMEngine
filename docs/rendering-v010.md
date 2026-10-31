# 0.10.0 渲染：材质、纹理、后处理和 3D

本版提供可运行的基础 3D 管线，与原来的 2D／UI 管线共用 bgfx 帧和有序视图。源资源在编辑期导入、校验、编译；发布后的 Player 读取二进制，不解析 JSON、导入 OBJ、生成 mipmap 或编码纹理。

## 已实现的范围

| 模块 | 实现 |
| --- | --- |
| 材质 | 内置 lit／unlit、自定义 bgfx Shader；命名 vec4 参数、最多 8 个 2D 纹理槽；opaque／alpha／additive；正反面剔除和深度读写 |
| 光照 | 一盏方向光、GGX 金属度／粗糙度直接光照、常量环境光、法线贴图、颜色／发光／透明度／alpha cutoff／UV 变换 |
| 纹理 | 离线完整 mip 链；sRGB 在线性空间过滤；透明颜色预乘后过滤再还原；法线重新归一化；BC3、ASTC 4×4、ETC2 RGBA、RGBA8 |
| 后处理 | RGBA16F HDR 目标，能力不足时退回 RGBA8；曝光、ACES 近似色调映射、Gamma、半分辨率 Bloom 与双向高斯模糊、FXAA；0.25–1 渲染比例 |
| 3D | 透视相机、深度缓冲、父子矩阵、内置 cube／plane／sphere、OBJ 索引网格、视锥裁剪、透明物体排序、内置材质 GPU 实例绘制 |
| 编辑／运行 | 原生 ECS 组件与现有组件 Inspector／CLI 共用；编辑态相机预览不执行游戏 System；Play 使用独立 World；场景恢复刷新 GPU 资源 |
| 发布 | Windows 保留 BC3／RGBA；Android 保留 ASTC／ETC2／RGBA；自定义 Shader 也按后端裁剪；场景和预制体均支持 |

纯 2D 场景没有 Camera3D 和启用的 PostProcess 时，不创建上述 Shader、深度目标和后处理缓冲。游戏 HUD 在后处理完成后绘制，编辑器其他面板继续使用原来的 2D 层。已有 UI／SpriteRenderer 图片仍使用原来的 RGBA 小图集和缓存；本版压缩纹理与材质接口用于新网格管线，不表示所有 UI 图片已改成压缩纹理。

## 独立示例和启动

Render Lab 位于 `D:\Demo\GMEngineGame\projects\RenderLab`，游戏资源和脚本没有放回引擎仓库。

```powershell
# 打开工程；F5 运行，F8 停止。
& D:/Demo/GMEngineApp/bin/gm_editor.exe --project D:/Demo/GMEngineGame/projects/RenderLab/game.gmproject

# 编译游戏模块与资源、发布 Windows 包。
'{"op":"project.build"}' | D:/Demo/GMEngineApp/bin/gmctl.exe --project D:/Demo/GMEngineGame/projects/RenderLab/game.gmproject
'{"op":"project.package"}' | D:/Demo/GMEngineApp/bin/gmctl.exe --project D:/Demo/GMEngineGame/projects/RenderLab/game.gmproject

# 发布包可独立运行，不依赖源码目录和编译工具。
& D:/Demo/GMEngineGame/projects/RenderLab/dist/gm_player.exe
```

示例包含金属球、带法线贴图的球、OBJ 发光晶体、自定义条纹 Shader、透明物体、重复实例与带 mipmap 的地板。WASD 移动相机，HUD 按钮切换 Bloom／FXAA。在层级中选择实体修改组件；`lighting` 实体控制光照与后处理。当前编辑态是游戏相机预览，3D 自由相机、拾取、变换手柄尚未实现。材质与 Shader 当前编辑源文件；修改外部资源后刷新资源列表或重新运行以预览。

## ECS 组成

`sdk/include/gm/render.hpp` 定义：

- `Transform3D`：位置、XYZ 欧拉角（度）、三个正缩放值。左手坐标，+Y 向上、+Z 向前。局部缩放后依次旋转 X/Y/Z，与父级矩阵组合。
- `WorldTransform3D`：引擎计算的只读 4×4 列主序矩阵，可在 System 中声明只读访问。
- `MeshRenderer`：网格／材质资源索引、启用状态；自动依赖 Transform3D。源文件中填路径或 `builtin:cube` 等引用，运行时为整数索引。
- `Camera3D`：视野角、近／远裁面、启用和优先级。采用优先级最高的启用相机，平局按实体顺序选择。
- `DirectionalLight`：方向、颜色、强度和环境项。本版采用第一盏启用的方向光。
- `PostProcess`：启用、曝光、Gamma、色调映射、Bloom 开关／阈值／强度／半径、FXAA、renderScale。本版采用第一份启用的配置。

例如编辑 `camera` 的位置，图形 Inspector 和 CLI 操作同一资源值：

```json
{"op":"component.set","id":"camera","type":"Transform3D","field":"x","value":2}
```

运行时动态组件写入会验证网格与材质索引边界。预制体加载将纹理、网格、Shader 和材质引用重映射到当前 World。当前 3D 变换每个模拟步重新组合；尚未实现 3D 渲染插值、LOD 或大世界空间索引。

## 材质源格式

`copper.material.json`：

```json
{
  "format": "gm.material", "version": 1,
  "shader": "lit", "blend": "opaque", "cull": "back",
  "depthTest": true, "depthWrite": true,
  "parameters": {
    "u_baseColor": [0.95, 0.44, 0.18, 1],
    "u_surface": [0.85, 0.22, 0, 0],
    "u_emission": [0, 0, 0, 0],
    "u_uvTransform": [1, 1, 0, 0]
  },
  "textures": {
    "s_baseColor": {"source": "../textures/grid.texture.json", "stage": 0},
    "s_normal": {"source": "../textures/normal.texture.json", "stage": 1}
  }
}
```

颜色参数使用线性 RGB；颜色贴图使用 sRGB 解码。`u_surface` 四项依次为金属度、粗糙度、alpha cutoff、法线强度；第四项为 0 时跳过法线贴图分支。`u_emission` 为 RGB 与强度，`u_uvTransform` 为 UV 平铺与偏移。`unlit` 会设置 `u_unlit`。透明材质通常使用 `blend: alpha, depthWrite: false`；按包围盒中心的相机深度从后向前绘制，交叉透明几何不保证正确排序。

自定义材质的 `shader` 改为相对 `.shader.json` 路径，参数名可自行定义（`u_` 前缀），纹理名使用 `s_` 前缀。运行时反射检查类型和名称，重置未赋值参数，防止材质之间残留状态；声明的采样器必须全部绑定。当前支持单个 vec4 和 sampler2D，不支持矩阵／数组／CubeMap 参数。`u_camera`、`u_lightDirection`、`u_lightColor` 为引擎保留值。

```json
{
  "format": "gm.shader", "version": 1,
  "vertex": "vs_stripe.sc", "fragment": "fs_stripe.sc",
  "varying": "varying.def.sc"
}
```

源 Shader 通过 bgfx shaderc 编译为 D3D11 `s_5_0` 和 GLES `300_es` 两份字节码。可选 `includeDirectories` 为相对目录。编译器优先查找 `GM_SHADERC`，其次安装目录 `tools/shaderc.exe`。也可提供 `variants: [{backend: "d3d11", vertex: "vs.bin", fragment: "fs.bin"}, …]` 使用预编译文件。自定义 Shader 使用相同的 position3／normal3／uv2 顶点布局，输出线性颜色；可以使用 bgfx 预定义模型／视图／投影矩阵。自定义 Shader 本版逐物体提交，只有内置材质启用矩阵实例绘制。

## 纹理导入和二进制运行

```json
{
  "format": "gm.texture", "version": 1, "source": "grid.png",
  "mipmaps": true, "srgb": true, "repeat": true, "linear": true,
  "formats": ["bc3", "astc4x4", "etc2rgba", "rgba8"]
}
```

可直接引用 PNG／JPEG，使用上述默认设置。法线设置 `normalMap: true`，会禁用 sRGB。`linear: false` 使用点采样。支持奇数尺寸的 mip 过滤和压缩块边缘扩展；Windows BC3 顶层宽高需要四像素对齐，非对齐图片选择 RGBA 回退。可以删去不需要的格式来节省源编译产物和 CPU 内存，但目标设备必须支持至少一种保留格式；没有可用格式会明确报错。

流程为：源场景解析与引用验证 → 收集材质／纹理／网格／Shader 依赖 → 离线导入／编码 → `.gmscene` v3 内嵌 GMRA 资源块 → Player 校验并读取 → 按设备原生能力上传选中的纹理与不可变网格 → 排序／裁剪／绘制 → 后处理 → HUD。Windows 优先 BC3；GLES 优先 ASTC，再 ETC2。不会选择仅由 bgfx 软件模拟的压缩格式。

v3 可读取原 v2 场景，ABI 10 模块继续可用；旧 Player 不能读取 v3，发布时必须一起更新 Player。纹理最大 4096×4096，资源块 128 MiB，最多 512 纹理／1024 网格／4096 材质／128 Shader。OBJ 支持位置、法线、UV 和负索引；无材质分组导入、骨骼和 glTF。多边形按扇形三角化，应导出三角形或凸多边形。资源当前按 World 持有，CPU 保留包内变体；跨场景共享、GPU 纹理流式驻留仍需后续设计。

## 构建与验证

```powershell
./scripts/setup-deps.ps1
cmake -S . -B ../GMEngineBuild/build -G "Visual Studio 17 2022" -A x64
cmake --build ../GMEngineBuild/build --config Release --parallel 4
ctest --test-dir ../GMEngineBuild/build -C Release --output-on-failure
cmake --install ../GMEngineBuild/build --config Release
```

内置 Shader 的两个后端头文件已入库，正常构建无需 shaderc。修改 Shader 后运行 `scripts/build-shaders.ps1`；发布需要支持自定义 Shader 源编译的编辑器时，配置 `-DGM_SHADER_COMPILER=/absolute/shaderc.exe`，安装会复制工具与标准 Shader include。压缩编码器只链接工具侧。

本次本地验证：17 项 CTest（含 3 项 GPU 合约）、安装与无编译器环境 9 项、项目扩展 15 项、场景／资源／存档 13 项、Render Lab 集成 8 项；另验证编辑器相机预览、组件编辑撤销和隔离运行。GPU 合约覆盖实际截图中的深度遮挡、单面剔除、透明排序、自定义参数绑定／清零／错误、Bloom 外扩、FXAA、sRGB 压缩采样、奇数尺寸回退、普通与实例绘制逐像素比较、法线基、视锥裁剪、缩放／资源释放和同地址场景恢复。无 GPU 的 CI 运行无窗口合约，GPU 项在本机 D3D11 执行。

Android ARM64 native Player 和 Render Lab 调试 APK 已构建，签名与 16 KB 对齐已验证。没有连接 Android 真机，未验证实际设备 GPU 格式选择、画面与帧率；iOS／macOS／Web 平台层仍未实现，不能仅凭 bgfx 支持便宣称已交付这些平台。

## 性能实测与解释

原始数据见 [validation/rendering-v010.json](validation/rendering-v010.json)。环境：i9-14900K、RTX 4090 D、D3D11、MSVC Release、关闭垂直同步；30 帧预热后取 120 帧实例测试／80 帧后处理测试的中位数。CPU 指渲染提交，未包含模拟、首次资源导入和启动；GPU 数据由 bgfx 延迟返回。GUI／Player 默认仍启用垂直同步。

| 640×480，1500 个网格／500 可见 | 普通绘制 | 实例绘制 |
| --- | ---: | ---: |
| 网格 draw | 500 | 1 |
| CPU 提交中位数 | 0.2822 ms | 0.0646 ms |
| CPU 提交 P95 | 0.4160 ms | 0.1170 ms |
| 截图 | 与实例版本逐像素一致 | 与普通版本逐像素一致 |

| 1920×1080，500 个可见受光物体 | CPU 中位数 | GPU 帧中位数 | 渲染目标字节 |
| --- | ---: | ---: | ---: |
| 效果关闭，保留线性颜色输出转换 | 0.0603 ms | 0.0686 ms | 24,883,200 |
| 色调映射＋FXAA | 0.0649 ms | 0.0932 ms | 24,883,200 |
| 再加 Bloom | 0.0694 ms | 0.1116 ms | 33,177,600 |
| 完整效果，renderScale=0.5 | 0.0662 ms | 0.1075 ms | 8,294,400 |

半分辨率将目标内存降至四分之一，但本机小场景 GPU 耗时只略降：最终合成仍是全分辨率，且这张显卡上的固定开销不可忽略。不能据此推导手机性能或宣传固定 FPS 提升。

32×32 渐变贴图的完整 mip 链：RGBA 5460 B，三种块压缩均 1392 B，约减少 74.5%。离线逐格式解码，RGBA 全通道 PSNR：BC3 33.76 dB、ASTC 41.22 dB、ETC2 45.57 dB；这是该测试图和当前编码质量的结果，不代表所有美术素材的质量排序。Render Lab 两张实际材质纹理共上传 109,280 B，稳定帧不重复上传纹理。

## 后续边界

尚无实时阴影、多灯／聚簇光照、IBL／反射、骨骼／蒙皮、glTF、LOD、遮挡剔除、透明顺序无关算法、TAA、SSAO、3D 物理、材质节点编辑器。当前是经过验证的基础网格渲染管线；应根据具体游戏和目标设备的采样决定下一批优化，而不是直接与完整 Unity／Unreal 渲染器的 FPS 做无控制比较。

底层接口和编译约定参考 [bgfx API](https://bkaradzic.github.io/bgfx/bgfx.html) 与 [bgfx 工具说明](https://bkaradzic.github.io/bgfx/tools.html)，实际实现以仓库固定版本及上述测试为准。
