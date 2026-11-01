# 0.13：局部灯光、雾、平面反射与大型街区验证

本轮以用户提供的 Tokyo Back Alleys 虚幻资产为测试负载。引擎代码与资产保持分离；资产和转换脚本在独立项目 `D:/Demo/GMEngineGame/projects/TokyoNeon`，专有模型、贴图和商店参考图不进入源码仓库。

## 新增接口

| ECS 组件 | 参数 | 行为与范围 |
| --- | --- | --- |
| `PointLight` | `color/intensity/range/enabled`，依赖 `Transform3D` | 位置来自世界变换；平方距离衰减和范围淡出；每个网格选择最多 8 盏有效灯。当前没有点光源阴影。 |
| `HeightFog` | `color/density/heightFalloff/baseHeight/startDistance/maxOpacity/enabled` | 沿视线积分指数高度密度；使用场景单位（米）和世界 Y 高度。不是带遮挡的体积散射。 |
| `PlanarReflection` | `height/resolutionScale/strength/enabled` | 单个水平面，反射分辨率为主画面比例 0.25–1；镜像相机、反向背面剔除、平面裁切，独立 HDR 颜色与深度目标。 |

组件通过共同的注册表进入编辑器、命令行、场景编译、ECS 和二进制 Player。既有组件布局与 SDK ABI 10 不变；兼容此前的场景注册表指纹。多份雾或平面设置按既有场景级组件规则选择首个启用实体。

内置 `lit` 材质通过 `parameters.u_planar: [1, 0, 0, 0]` 接收反射，默认关闭。反射接收材质不进入反射画面，防止递归反馈。强度与 Fresnel、粗糙度共同影响混合；5 次纹理采样的粗糙模糊是近似，不能替代完整的粗糙反射预滤波。

自定义 Shader 在 `bgfx_shader.sh` 后包含 `gm_lighting.sh`，按需调用：

```glsl
gmClipReflection(world);
color += gmLocalLighting(world, normal, viewDirection, baseColor, metallic, roughness);
color = gmPlanarReflection(color, world, normal, viewDirection, roughness, wetMask);
color = gmHeightFog(color, world, cameraPosition);
```

引擎占用纹理槽 8–15；材质仍使用 0–7。新增灯光与雾 uniform 由渲染器提供，材质不需要逐一配置。自定义材质必须调用对应函数才能参与这些效果。

## 资源容量

场景资源总字节上限仍由 `GM_RENDER_RESOURCE_BUDGET_MIB` 约束，源码默认 128 MiB。本机压力测试构建配置 3840 MiB；这不是预分配、缓存池或流式加载预算。场景二进制采用 32 位段长度，因此上限保留编码与非渲染数据余量，不允许任意增加。

纹理与网格计数上限分别扩展至 4096，编译器与二进制解码器共用常量；总字节检查仍然生效。增加了到达上限的成功案例及超过上限的拒绝案例。场景文件格式保持 v3，大于旧容量的文件需要新版本 Player。

源文件依赖条目上限相应提高至 32768，因为一张纹理通常同时引用描述文件和像素文件；依赖计数到达上限时仍允许重复查找，并准确拒绝下一条新依赖。原来的 4096 条依赖限制会先于大型场景的资源容量触顶。

## 性能行为

- 没有启用平面反射时，不分配反射目标，不提交反射绘制。
- 启用后增加一次镜像视角的场景绘制。计数 `reflectionDraws/reflectionTriangles/reflectionBytes` 独立列出；总 drawCalls 和 targetBytes 包含其代价。
- 点光源选择发生在 CPU，每个网格至多评估场景灯光列表并绑定 8 盏。当前包含点光源的场景关闭内置批实例合并，避免同一批次错误共用灯光列表；后续应根据相同灯光集合分组或实现分块光照。
- Fog 和灯光变化会使 TAA 历史失效。平面设置变化同样使历史失效。
- 阴影深度范围和缓存键只考虑实际投影的不透明物体；透明雨滴移动不会重建方向光阴影或 RSM。通过移动透明物体到极端高度的 GPU 回归验证。
- Tokyo 雨滴是项目自定义 Shader 和透明几何，具有深度测试；当前没有透明运动矢量，开启雨时使用 FXAA，关闭雨后高画质启用 TAA。

## 已执行验证

原有 20 项 CTest 合约通过，包含新增的测试：

- 灯光颜色、距离范围、禁用状态与 TAA 失效。
- 雾颜色、禁用状态与二进制组件往返。
- 红色物体在地面的实际 GPU 反射；接收面不递归；平面以下物体裁切；反射目标缩放和关闭释放。
- 材质源格式接受 `u_planar`；非法光照范围被拒绝。
- 保留此前 SSAO 无纹理斜平面 6 种组合回归，横向暗带修复未回退。
- 自定义街道和雨 Shader 在 D3D11 实际渲染，内置材质同时编译 DXBC / ESSL。尚无本轮 Android/iOS 真机验证。

完整街区的画面、资源统计和性能测量在独立项目评估报告中记录；测试图和单个网格的结果不代表完整街区性能。

## 与虚幻的差别

| 项目 | 本轮 GM 能力 | 仍缺少 |
| --- | --- | --- |
| 间接光 | HDR IBL、方向光 RSM 一次漫反射、SSAO | 场景遮挡的多次反弹、发光招牌自动照明、室内外天空遮挡 |
| 反射 | 天空环境反射、单水平面反射 | 任意表面的场景反射、透明多层反射、光追或完善 SSR |
| 局部光 | 最多 8 盏/网格的点光源 | 局部阴影、聚光/面光源、分块光源列表 |
| 雾 | 解析高度雾 | 体积灯光、光束和雾内遮挡 |
| 大模型 | 普通静态网格、视锥剔除 | Nanite 类细粒度 LOD/流送、遮挡剔除、贴图流送 |
| 人物 | 资产包提供的静态姿态 | 蒙皮动画、皮肤次表面散射、眼睛/头发专用材质 |
| 时域 | 基础 TAA、FXAA | 每物体/透明运动矢量、复杂遮挡历史管理、TSR 类重建 |

Epic 文档说明 Lumen 包含多次漫反射、天空遮挡及自发光参与间接照明；这些功能会直接影响此类窄巷夜景，不能仅靠 Bloom 或曝光补齐：[Lumen 5.5](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine?application_version=5.5)。

glTF 转换只能表达受限的 PBR 数据；烘焙会固定时间/视角相关材质表达式，不能原样迁移虚幻材质图：[Epic glTF 内容转换说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-the-gltf-exporter-handles-unreal-engine-content?application_version=5.5)。本次日志还记录了 UV 混用、重叠 UV、次表面材质降级。因此对比时必须区分素材转换损失、场景摆放和渲染算法本身。

商店截图的机位、光照、后处理与渲染设置未知，不能从截图推断双方 FPS，或给出可信的“达到虚幻百分之多少”。

## TokyoNeon 原生测量

设备 i9-14900K / RTX 4090 D，隐藏 D3D11 表面、无 VSync；120 帧预热 + 600 帧采样，不代表可见 Player 帧率，也不是同场景 UE 对照。

| 配置 | 整帧 p50 / p95 ms | CPU 提交 p50 ms | GPU p50 ms |
| --- | ---: | ---: | ---: |
| 1080p 标准，静止 | 10.91 / 11.63 | 10.22 | 7.27 |
| 1080p 关闭反射，静止 | 6.35 / 6.96 | 5.64 | 4.87 |
| 1080p 无雨 + TAA，静止 | 11.30 / 12.02 | 10.57 | 7.23 |
| 4K 标准，静止 | 10.91 / 11.50 | 10.20 | 9.06 |
| 1080p 性能预设，静止 | 10.45 / 11.24 | 9.76 | 5.19 |
| 1080p 前进路径 | 8.47 / 10.20 | 7.76 | 5.83 |
| 4K 前进路径 | 8.47 / 10.46 | 7.75 | 7.68 |

完整数据见 [rendering-v013.json](validation/rendering-v013.json)。静止标准机位总计 3515 次 draw；反射增加 1111 次 draw。1080p 关闭反射将整帧中位数从 10.91 降至 6.35 ms。4K 与 1080p 整帧接近，结合 CPU / GPU 时间表明当前受 CPU 提交限制；优先优化材质绑定、重复提交、LOD 与遮挡剔除。

移动路径会改变可见物体集合；无雨预设同时改用 TAA；两者不能被解释为单一功能的隔离开销。所有人物是静态姿态，未覆盖骨骼动画和大量动态投影物体。此高端 PC 场景尚未做移动设备验证。
