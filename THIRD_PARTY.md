# 第三方依赖

下载入口与版本固定在 `scripts/setup-deps.ps1`。本项目没有修改第三方源码。

| 依赖 | 固定版本 | 许可 |
| --- | --- | --- |
| bgfx.cmake | `5ea74ee66662daef4089695846d6322da0dde94e` | CC0；见其 LICENSE |
| bgfx | `fe9bbdec66110f8626b9251a4947fc336ac3f62c` | BSD-2-Clause；见 `third_party/bgfx/LICENSE` |
| bx | `0e5bbca6878000f6371c65c53fe06f6f0ae87ca9` | BSD-2-Clause；见 `third_party/bx/LICENSE` |
| bimg | `9bfbd6493b48a15238f2d1374a718ec026524515` | BSD-2-Clause；包含另附许可的依赖，见目录内 LICENSE 文件 |
| nlohmann/json | `v3.11.3` | MIT；见 `third_party/nlohmann/LICENSE.MIT` |
| stb_image | 随上述 bimg 固定提交提供 | MIT / Public Domain 双许可；见 `third_party/bimg/3rdparty/stb/stb_image.h` 末尾 |
| Flecs | `v4.1.6` | MIT；见 `third_party/flecs/LICENSE`；场景运行时使用 |
| EnTT | `v3.16.0` | MIT；见 `third_party/entt-3.16/LICENSE`；仅用于对照测试 |
| EnTT（兼容性调查） | `v4.0.0` | MIT；见 `third_party/entt/LICENSE`；当前 MSVC 19.39 未编译通过，不链接产品 |

ECS 下载入口为 `scripts/setup-ecs.ps1`。本次获取的 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| Flecs 4.1.6 flecs.c | `6005392EB13C0F3C7ABDECB2F85271E50934F63919AFEBF0BBE85F6DFC7320D6` |
| Flecs 4.1.6 flecs.h | `526036A5A41678E2A43A3CB835E9EAA70FD1993868B1978950C0D275752F69B1` |
| EnTT 4.0.0 entt.hpp | `EEDCB83FEDFFE640334EF69A0647625A186CF1DAEB333B21B0A1445F01E89B6C` |
| EnTT 3.16.0 entt.hpp | `3272191CF3D46CEC92368F918695502DC8D174C92E0B8B4A5D53FED28E240C9F` |

游戏工作区的 `scripts/package-scene.ps1` 为场景发布包附带 Flecs、bgfx、bx、bimg 的许可。EnTT 仅参与测试，不进入 Player 发布包。

字体使用当前 Windows 安装的 Microsoft YaHei UI，不在项目中重新分发。bgfx / bimg 的第三方组件保留其原始版权和许可文件；分发二进制时应一并保留所用组件的许可声明。

`gm_player` 不链接 nlohmann/json；JSON 仅用于编辑器、CLI、资源编译器及测试。游戏工作区的 `scripts/package-example.ps1` 将 bgfx、bx、bimg 的许可文件复制到发布目录的 `licenses` 中。

PNG/JPEG 解码使用独立的 `gm_image_import` 模块，仅链接到编辑器、资源编译器和测试；Player 不链接 stb_image。游戏工作区的 `UIExamples/assets/images` 中两张示例图使用内置 imagegen 生成，生成提示词与原始尺寸记录在该目录的 README 中。

引擎安装通过 CMake install 将第三方许可复制到 SDK 的 licenses 目录；stb_image 的 MIT 许可副本位于 resources/stb-license.txt。
