# 0.11 光照：阴影、HDR 环境与一次反弹光

本轮把光照接入引擎、ECS 组件、资源编译、图形编辑器、CLI 和打包 Player。原有 2D 场景不创建 3D 光照目标；未添加新组件的场景保留原有光照。原生模块 ABI 仍为 10，旧组件布局未改变；读取器保留旧渲染 schema 指纹的兼容路径。

## 已实现的能力

| 功能 | 实现 | 当前边界 |
| --- | --- | --- |
| 方向光阴影 | 深度贴图、3×3 PCF、法线偏移、接收面斜率补偿、覆盖边缘淡出、光空间 texel 对齐 | 单张有界贴图；尚无 CSM、点光源/聚光灯阴影、PCSS |
| 遮挡物 | 从完整场景挑选光空间内的物体；支持标准底色贴图 alpha cutoff、UV 变换与双面/正反面剔除 | Alpha Blend 不投影；自定义顶点变形/特殊 alpha 逻辑需要专用 shadow pass，尚未提供 |
| HDR 环境 | Radiance `.hdr` 导入；SH9 漫反射卷积；GGX 预滤波粗糙度 mip；环境反射、Y 轴旋转和天空背景 | 全局远场环境；没有局部探针、视差校正、屏幕空间反射或室内遮挡探针 |
| 间接光 | Reflective Shadow Map（RSM）记录位置、法线、反射通量；在接收面采样一次漫反射反弹 | 只覆盖方向光看到的第一层表面；未计算反弹路径的二次遮挡，可能漏光；没有多次反弹或实时光追 |
| 缓存 | 精确比较光空间投影、物体变换、投影材质/贴图引用、光源颜色与强度；未变化时复用 shadow/RSM | 相机跨越阴影 texel、物体/光源/材质改变后重绘；动态场景仍需承担额外几何 pass |
| 诊断 | `shadowDraws`、`rsmDraws`、`lightingTriangles`、`lightingBytes`、`lightingCacheHits`、`environmentDraws` | Draw 计数表示本帧实际提交；命中缓存时为零不代表关闭光照 |

环境照明与 RSM 是两个来源：环境负责远处天空的入射光和反射，RSM 负责本场景物体反射的方向光。当前环境光不会自动被墙壁遮挡，不能将它当成已经实现完整室内 GI。材质已有 AO 时可在 Shader 中用于环境和反弹光；本轮未实现 SSAO。

## 编辑与运行

在场景中新增或选择 Lighting 实体，通过 Inspector 添加以下组件；CLI 的 `component.add` / `component.set` / `save` 调用相同编辑服务。运行时可通过 `runtime.component.set` 或 C++ `Context::getMut<T>` 调整数值，组件结构定义在 `sdk/include/gm/render.hpp`。

| 组件 | 字段 | 含义 |
| --- | --- | --- |
| `ShadowSettings` | `enabled`, `resolution`, `distance`, `depthBias`, `normalBias`, `softness` | 分辨率为 256–4096 的 2 次幂；distance 是光空间横纵覆盖宽度（米），不是世界无限远阴影；softness 为 PCF 像素半径 |
| `EnvironmentLight` | `enabled`, `irradiance`, `radiance`, `intensity`, `rotation`, `background` | 两张环境资源引用、照明倍率、Y 轴角度、是否绘制天空 |
| `IndirectLight` | `enabled`, `resolution`, `samples`, `radius`, `intensity` | RSM 分辨率 64–1024 的 2 次幂；8–64 次采样；以米计的反弹采样半径和强度 |

每类组件使用场景顺序中第一个启用实例；方向光沿用同样规则。三类设置是场景级设置，不是任意数量的独立光源。关闭 ShadowSettings 不会强制关闭 RSM。未提供 ShadowSettings 时，RSM 使用默认 60 米覆盖。

`DirectionalLight.ambient` 仍作为兼容的常量填充光保留。使用 HDR 环境时建议将它设为 0，避免重复填充；曝光通过 PostProcess 调整。扩大阴影覆盖范围会降低每米分辨率，应按场景尺度选择。

```json
{"op":"component.add","id":"lighting","type":"ShadowSettings"}
{"op":"component.set","id":"lighting","type":"ShadowSettings","field":"resolution","value":2048}
{"op":"component.add","id":"lighting","type":"EnvironmentLight"}
{"op":"component.set","id":"lighting","type":"EnvironmentLight","field":"irradiance","value":"environment/irradiance.texture.json"}
{"op":"component.set","id":"lighting","type":"EnvironmentLight","field":"radiance","value":"environment/radiance.texture.json"}
{"op":"component.add","id":"lighting","type":"IndirectLight"}
{"op":"save"}
```

这些命令以已打开对应场景为前提。源资源字段是路径或稳定资源 URI；编译后 `Kind::Texture` 存储资源索引。运行时设置资源字段应使用已经加载的索引，不能向 Player 传入 HDR/JSON 路径。场景编译、二进制校验、动态组件设置和 prefab remap 都识别新资源类型。

## HDR 资源格式

`assets/environment/irradiance.texture.json`：

```json
{
  "format": "gm.texture",
  "version": 1,
  "source": "sky.hdr",
  "environment": "irradiance",
  "width": 64,
  "exposure": -4
}
```

反射资源使用相同 source 和 exposure，将 `environment` 改为 `radiance`、`width` 改为 512。输入是 2:1 经纬度 Radiance HDR，最大 4096×2048、文件最大 64 MiB；EXR 暂不支持。输出宽度必须为 4–1024 的 2 次幂，高度为宽度一半。

`exposure` 是导入时的 EV 缩放，范围 -24…24，输出前乘 `2^exposure`。极亮 HDR 可通过降低导入曝光进入半浮点范围，运行时再调整强度；不会默默把 HDR 当作普通 PNG 截成 8 位。源像素必须有限且非负，缩放后须在半浮点范围内。

构建期执行：

1. 读取线性 HDR，按立体角积分得到 SH9，并生成漫反射环境贴图（卷积结果除以 π）。
2. 建立按立体角加权的源 mip 链；GGX Hammersley 重要性采样依据 PDF 选择源 LOD，降低太阳等小型高亮源的采样亮点。
3. 写入线性 RGBA16F 二进制资源，反射图包含完整粗糙度 mip 链。原 PNG/JPEG 压缩纹理格式不变。
4. Player 直接上传 GPU 数据，不解码 HDR、不运行卷积、不解析 JSON。Shader 使用粗糙度选择反射 mip，并使用解析 DFG 近似。

64×32 漫反射图与 512×256 完整反射 mip 共增加 **1,414,488 字节（约 1.35 MiB）**。输入 HDR 留在游戏项目源码中，发布包不包含它。环境资源会参与依赖追踪；当前仍缺少每资源派生数据缓存，修改场景可能触发整场景重编译。

## 自定义材质接入

内置 `lit` 已自动接入；自定义材质在 `bgfx_shader.sh` 后包含安装目录 `sdk/shaders/gm_lighting.sh`。标准调用：

```c
direct *= gmShadow(worldPosition, geometricNormal);
color += gmEnvironment(normal, viewDirection, baseColor, metallic, roughness, ao);
color += gmIndirect(worldPosition, normal, baseColor, metallic, ao);
```

材质使用采样槽 0–7，引擎保留 8–13，所有 `u_gm*` / `s_gm*` 名称保留给引擎。引擎绑定自己的资源，无需把它们写入 `.material.json`；未知引擎采样器会报错。投影 pass 识别 `u_baseColor`、`u_surface.z`（alpha cutoff）、`u_uvTransform`、`s_baseColor`；RSM 还识别金属度、`s_orm` 和 `u_unlit`。任意自定义 Shader 不会自动获得对应的顶点变形或光照公式。

## Sponza 示例与验证

游戏位于 `D:/Demo/GMEngineGame/projects/SponzaGallery`。双击 `Run.cmd` 运行已打包版本，`Edit.cmd` 打开编辑器。`Build.ps1 -Package` 构建并打包；该完整 4K 贴图场景仍需要本机 2048 MiB 评估构建。

环境素材使用 [Poly Haven 的 Kloofendal 48d Partly Cloudy (Pure Sky)](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky)，作者 Greg Zaal / Jarod Guest，按其 [CC0 许可](https://polyhaven.com/license) 使用。游戏项目保留来源、文件校验值和 `tools/fetch_environment.py`。Intel Sponza 自身的许可说明保持独立。

可重复运行：

```powershell
py -3 D:/Demo/GMEngineGame/projects/SponzaGallery/tools/verify_lighting.py D:/Demo/GMEngineBuild/build/Release/gm_scene_capture.exe
```

原图、逐项开关的输入命令、`.benchmark.json`、图像差异检查保存在游戏项目 `docs/lighting/`。源码仓库的 [测量记录](validation/lighting-v011.json) 留存汇总，完整统计与说明见游戏项目 `docs/lighting/evaluation.md`。

测试覆盖 HDR 动态范围、常量/方向性能量、半浮点校验、二进制往返、资源索引、prefab、离屏遮挡物投影范围、阴影接收面暗化、alpha cutoff、颜色反弹、环境反射与旋转、天空背景、缓存命中/失效以及关闭后的目标释放。所有像素检查来自引擎实际 GPU 输出。D3D11 实际运行；GLES3 Shader 编译成功，不代表 Android/iOS 真机光照验证完成。

## 实景开发发现与后续

- 修正了只增加固定环境亮度造成的平坦画面。阴影与有方向的天空照明已接入；室内接触关系仍需 AO 和可见性探针补足。
- 首轮 PCF 在斜面出现条纹，增加接收平面深度梯度与 texel 量化偏移补偿；过大 bias 仍会造成阴影脱离，参数应按场景尺度设置。
- bgfx 的 palette clear 重载会在提供 palette 索引时启用颜色清除，曾覆盖已绘制天空；现改用明确的仅深度清除，并加入 GPU 背景测试。
- 直接从原始 HDR 为所有粗糙度采样会产生高亮噪点，改用源 LOD 与 SH 卷积。
- Sponza 每次更新光照贴图需额外提交约 748 万三角形；静止时复用贴图，动态情况下仍有成本。下一步可按测量增加独立 shadow/RSM 脏标记、投影专用 LOD 和遮挡剔除。
- RSM 采样数较低时有空间噪点，且缺少反弹路径遮挡。这是近似算法的可见限制；后续优先级为遮挡/探针方案与降噪，再评估多次反弹，而不是单纯提高强度。
- 大型世界需要 CSM/分区光照、局部反射探针与资源流式；本轮没有把单个 Sponza 的结果扩展成大型世界或移动平台性能承诺。

实现参考：[bgfx RSM 示例](https://github.com/bkaradzic/bgfx/blob/master/examples/31-rsm/reflectiveshadowmap.cpp)、[Epic 的 IBL / GGX 说明](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)、[Brian Karis 的解析环境 BRDF](https://www.unrealengine.com/blog/physically-based-shading-on-mobile?lang=en-US)、[NVIDIA 的低频环境照明积分说明](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-10-real-time-computation-dynamic)。
