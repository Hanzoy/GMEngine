# 0.13.1：材质油光与法线响应修正

2026-11-02。问题来自 TokyoNeon 街区的实际人物和场景材质反馈。修复引擎公共光照和项目转换数据，没有通过降低全局曝光或取消全部反射掩盖问题。

## 已确认的原因

转换后的 glTF 包含 1,074 个材质，其中 1,005 个带 `KHR_materials_specular`：892 个强度约 0.498、65 个为 0、40 个约 0.2、8 个约 0.098。旧转换器没有保存它们，方向光、点光源和 IBL 均按默认强度 1 计算。另有 65 个材质声明 `KHR_materials_ior: {ior: 1}`，旧 Shader 同样忽略，统一采用 F0=0.04。

这些是 **glTF 导出结果的参数**，并不代表虚幻的整个材质图已被完整导出。尤其是皮肤次表面散射仍有转换损失。

抽查的夹克、长裤、衬衫粗糙度中位数约 0.68–0.86，金属度大多为零；直接把整个人物粗糙度强行设为 1 会抹掉衣料差异，不能解决参数丢失。旧法线计算还有独立问题：分别归一化三角形位置导数得到的 T/B，没有针对插值顶点法线正交化，UV 剪切和光滑曲面会扭曲法线贴图的响应。

## 引擎接口

内置 `lit` 增加可选参数：

```json
"u_specular": [0.04, 0.04, 0.04, 0.5]
```

RGB 是非金属在正视角的反射率 F0（线性值，尚未乘强度），W 是非金属高光强度。缺省为 `[0.04,0.04,0.04,1]`。W=0 关闭非金属镜面分量；不会关闭漫反射，也不会削弱金属。粗糙度继续控制高光形状，不能用 W 替代粗糙度贴图。

`gm_brdf.sh` 统一方向光和点光源的 GGX/Fresnel 计算，并提供 `gmMappedNormal`。环境反射同时缩放 split-sum 中的 F0 和 F90 项，避免只压低正视角却遗留掠射角亮边。非金属漫反射按剩余反射能量调整。法线切线先投影到插值法线的切平面，再生成正交副切线，并保留 UV 镜像方向。

自定义 Shader 可以传入同一控制值：

```glsl
vec3 direct = gmDirectBrdf(n, v, l, base, metal, rough, u_specular);
color += direct * lightColor * max(dot(n,l), 0.0);
color += gmEnvironment(n, v, base, metal, rough, ao, u_specular);
color += gmLocalLighting(world, n, v, base, metal, rough, u_specular);
```

旧的 `gmEnvironment`、`gmPoint`、`gmLocalLighting` 调用签名仍可用，使用默认参数。二进制场景和 SDK ABI 10 不变；新 `u_specular` 需要新版渲染器。安装包包含两个公共 Shader 头，编译缓存版本升至 16，确保项目重新烘焙自定义 Shader。

TokyoNeon 的 `tools/gltf_materials.py` 保存标量高光强度、颜色与 IOR→F0；已有材质也已迁移。当前输入没有高光颜色/强度贴图；未来遇到这些贴图会明确报错，避免再次静默丢失。湿地面保持原有单独的反射设置，模型、灯光、曝光、粗糙度贴图均未为了对照而改动。

## 验证

- 新增 GPU 对照：方向光强度 1/0.5/0；点光源高光关闭；IBL 高光关闭及掠射角；金属不受非金属控制影响；缺省参数不会继承上一个材质；常量法线贴图在 UV 剪切后保持一致。
- 原有渲染测试通过，包含 SSAO 横带与阴影回归。DXBC 和 ESSL 均编译；未进行 Android/iOS 真机验证。
- 全套 CTest 首轮 19/20 通过。网络测试第一次遇到临时 TCP 端口对应 UDP 端口被占用，单独复测通过；未停用其他程序或修改网络实现。渲染改动没有涉及网络。
- 原生截图采用同机位、同光照、同曝光；近景关闭雨并启用既有 TAA，性能对照保留标准雨景。截图在私有项目 `docs/screenshots/materials/`，未修图。

RTX 4090 D / i9-14900K，D3D11 隐藏表面、1920×1080、无 VSync，每轮 120 帧预热 + 600 帧采样，交替三轮，表格为各轮 p50 的中位数：

| 指标 | 修改前 | 修改后 | 差值 |
| --- | ---: | ---: | ---: |
| GPU | 6.775 ms | 6.893 ms | +0.118 ms |
| CPU 提交 | 10.356 ms | 10.626 ms | +0.270 ms |
| 整帧 | 11.037 ms | 11.325 ms | +0.288 ms（约 2.6%） |

两组均为 3,515 次绘制，主视角 27,974,229 三角形，纹理和几何显存占用不变。增加一个材质参数和相应计算有小幅成本，不能声称免费或性能提升。不是可见窗口 FPS，也不是与虚幻的性能对照。原始汇总见 `validation/material-response-v0131.json`。

安装验证还发现 0.13 新增组件后旧脚本仍要求固定总数 18。已改为验证所需组件唯一存在、扩展组件和 System 相对增加，并保留真实运行/移动检查，避免新增引擎组件导致无关误报。

## 尚未解决的画质差距

修复减少了皮肤、布料和场景的错误油光，但仍不是商业引擎人物管线：没有皮肤 SSS、布料纤维专用 BRDF、头发各向异性或完整眼球材质。当前仍由 UV 导数重建切线，而非导入完整的 MikkTSpace 顶点切线。源材质烘焙、局部灯光无阴影、有限间接光、透明排序和基础抗锯齿仍影响画面。没有以调色或后处理将这些能力差距表述为已解决。

实现依据：[Khronos 高光扩展](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_specular/README.md)、[Khronos IOR 扩展](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_ior/README.md)。
