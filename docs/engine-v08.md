# GM Engine 0.8：应用入口与运行时基础

这版将原先叫作 GMEngineSDK 的完整发行目录改为 GM Engine 应用，并补齐一组运行时基础能力。以下明确区分已经实现的功能和仍需开发的部分，不能把这版视为清单中的所有模块都已完成。

0.8.1 修复了干净电脑中新建项目立即要求 CMake 的发行遗漏：模板 DLL 随安装包提供，复制到新项目的 `bin/windows-x64/Game.dll`，只有重新构建 C++ 时才调用工具链。未修改原生模板代码的旧项目可自动补齐缺失 DLL；自定义代码必须使用自己的编译模块。打开项目失败后回到选择器。构建工具支持 `GM_ENGINE_CMAKE` 显式路径，并能查找 CMake / Visual Studio 常见安装位置。

## 安装、项目与工作区

本地应用在 `D:\Demo\GMEngineApp`。安装程序和便携包在 `D:\Demo\GMEngineBuild\releases`。安装程序按当前用户安装，提供开始菜单、可选桌面快捷方式、`.gmproject` 关联、系统卸载入口；不会移动或删除游戏项目。构建安装包使用固定版本的 [Inno Setup](https://jrsoftware.org/isinfo.php)，下载校验见 `scripts/package-editor.ps1`。

`gm_editor.exe` 无参数时显示项目选择器；`--project` 直接打开项目／资源；`--editor-shell` 打开编辑器自身界面工程。最近项目在 `%LOCALAPPDATA%/GMEngine/projects.json`，最多 30 项，失效路径保留提示，“从列表移除”不会删除文件。

游戏工作区现在分成：

```text
GMEngineGame/
  projects/CrystalMeadow/  独立项目；game.gmproject、CMakeLists.txt、Scripts、assets、build、dist
  samples/UI/             UI 示例和素材
  tools/                  工作区启动、构建、打包、验证脚本
  tests/                  游戏玩法测试
  docs/                   示例说明
  benchmarks/             Unity 对照资料与历史测量
  .cache/                 会话及当前测试输出
  archives/               旧缓存、旧发布文件、迁移记录
```

旧 GMEngineSDK 已归档到 `GMEngineBuild/archive/GMEngineSDK-v07`。源码、安装目录、项目和构建目录相互独立。

## ECS 与运行约定

SDK ABI 升为 8。已有项目需要重新编译 DLL；打开旧 ABI 模块时编辑器会尝试重新构建。组件仍为平坦值数据，项目 System 通过公开 C ABI 注册；状态的 create/destroy 保持在模块内部配对，不跨模块转移 STL 所有权。

- `Context::create(parent)`、`destroy(entity)`、`set<T>`、`remove<T>` 在 System 执行结束后提交结构变化，可以在 `each` 中使用。`create` 返回带代数的实体句柄，新组件要到提交后才能查询。
- `get`、`getMut` 和查询得到的组件指针只在当前 System 的结构提交边界之前有效。生成的实体槽位会复用，旧句柄不会重新指向新实体；有源资源／UI 绑定的槽位不复用。
- `set` 校验字段和资源索引，并添加声明的依赖组件；移除被其他组件依赖的组件会被拒绝。直接在 `each<T>` 中写原生字段仍由游戏 System 负责保证值有效，以避免逐实体、逐字段反射校验进入每帧热路径。
- 结构命令按顺序执行，并非跨整个 System 的事务。无效命令会报告 System 名和 tick；原生游戏代码需要修正错误。
- System 可提供读写 `Term` 清单。声明之后，未声明的查询／读取／写入会报错；没有声明的旧 System 保持串行、宽松访问。`gm.WorldPosition` 始终只读。没有引入多线程 System 调度。
- 每个 World 有独立事件队列，事件 payload 最多 64 字节，下一模拟 tick 可见。事件队列上限 65536。
- 默认实时模拟固定为 60 Hz；渲染按窗口帧循环执行。每次最多补 8 步，超额时间记录在 droppedSeconds 中，避免恢复窗口后无限追赶。当前尚未做渲染插值。

运行时子树实例化以已加载场景中的源实体为模板，复制其初始组件和子节点。它不是完整的外部 Prefab 资源、覆盖值和变体系统。

## 后台任务与多线程边界（0.8.2）

内部任务池 `src/tasks.hpp` 固定复用工作线程：默认 `clamp(逻辑核心数 - 2, 1, 4)`，最多排队 64 个任务，超出容量立即报错，不持续创建线程。任务拥有输入和返回值，通过 `Task<T>` 查询、取结果、请求取消；句柄析构不等待任务，池关闭时取消排队任务、请求运行中任务停止并等待工作线程退出。取消是协作式的，任务必须检查 token；工作线程不能阻塞等待另一个池任务，防止线程耗尽造成死锁。

Player 的场景字节读取从每次 `std::async` 改为共享任务池，每读取 1 MiB 检查取消。取消后旧结果不会被发布，可重新请求场景。仅文件读取在工作线程；二进制校验、World 创建和纹理提交仍在主线程，因此大场景切换仍可能出现提交阶段停顿。没有加入跨场景纹理缓存或异步 GPU 上传。

编辑器的 `project.build`、`project.package`、`project.assets` 在任务线程中启动独立 `gmctl` 子进程，复用离线项目服务。原因是当前组件注册表属于进程全局状态，原生模块的注册回调和编译不能与编辑器在同一进程并发操作。构建子进程中的 CMake / MSBuild 保持原有并行编译参数；这次优化减少编辑器等待，不宣称提高 C++ 编译吞吐。

- 状态栏、项目页显示操作与耗时，提供取消；组件查看、面板导航、视口操作和查询保持可用。
- 开始前须停止运行并保存所有打开资源；执行期间暂时禁止编辑、保存、播放、项目切换和重载，避免源数据与构建结果不一致。新建／打开项目时缺失模块的自动修复仍走原有同步启动流程。
- 成功后在主线程加载新模块并校验所有打开文档，校验失败保留原注册表。构建失败或取消后内存中的旧模块仍可使用。取消不是磁盘事务回滚，可能留下中间构建文件，可再次构建恢复。
- 每个任务使用 Windows Job Object，取消或关闭编辑器终止该任务的构建进程树；不会终止其他编辑器或用户独立启动的编译器。
- 同一项目的导入／构建／打包通过 `.gm/project-operation.lock` 排他访问，跨线程与跨进程的另一个写任务会明确报忙。锁由系统句柄持有，异常退出后释放；磁盘上的锁文件可以保留。它不阻止外部文本编辑器修改源文件。

实时命令示例：

```json
{"op":"project.build","async":true}
{"op":"project.task.status"}
{"op":"project.task.cancel"}
```

`async:true` 立即返回 `{ok:true,pending:true,task:"ID"}`，只表示已接受任务。状态查询返回当前或最近一次任务的 ID、操作、active、state、elapsedMs，完成时附带最终 `response`。取消请求返回后应继续查询到终态。省略 `async` 时，`gmctl --live` 等待最终响应，原有脚本无需修改；任务状态／取消命令仅在实时编辑器会话中提供。`gmctl --project` 和 `--stdio` 保持同步执行。任务请求和子进程响应位于会话目录 `tasks/<ID>/`，原生编译日志位于项目 `build/native-build.log`。

当前不向游戏 SDK 暴露任务池，ABI 仍为 8。ECS System 串行调度，World、组件查询、事件和结构命令接口不能从自建工作线程调用。后续 ECS 并行需要补齐每个 worker 的命令缓冲、读写冲突调度、事件合并与确定性验证；这版不将“后台任务可并发”当作“游戏逻辑已自动并行”。

验证包含线程池并发、队列限额、异常恢复、取消、关闭清理、子进程树终止、项目写入互斥及场景读取取消。`scripts/test-background.ps1` 在 CMake 中插入 30 秒测试等待，验证查询响应、取消后重建、失败保留模块、打包和默认实时 CLI 兼容。首轮本机 5 次查询为 92–117 ms，这是响应测试，不能推导为帧率或编译加速比例。

## 2D、碰撞与动画

新增的组件都是普通可编辑 ECS 组件：

| 组件 | 行为 |
| --- | --- |
| `Pose2D` | 旋转角度、X/Y 缩放，与 Transform2D 组合；父子关系组合完整 2D 仿射变换 |
| `Camera2D` | 开关、优先级、缩放；位置／旋转来自实体变换，无摄像机时使用默认视图 |
| `SpriteRegion` | 图片中的归一化 UV 区域，可以引用同一张图集 |
| `SpriteAnimation` | 行列式图集、帧数、fps、播放／循环开关，通过 gm.SpriteAnimation 更新 UV |
| `Collider2D` | AABB 触发器，尺寸、偏移、层／接收掩码、启用开关 |

旋转四边形使用 UV 插值裁剪，仍按纹理合批；完全在视口外的几何不提交。编辑视图和运行视图都支持旋转／缩放。编辑选择框和碰撞体目前使用旋转后的包围盒，不是精确多边形。

碰撞使用按 X 排序的扫描，产生 CollisionEnter / Stay / Exit 事件；相交对双方互发，source 是对方、target 是接收者。重叠对上限 30000。提供区域查询和最近射线命中。没有刚体、质量、速度响应、连续碰撞或物理材质。

## 资源、场景和运行状态

项目导入生成 `文件名.gmmeta`，其中的 UUID 是稳定资源 ID；它应与资源一起进入游戏项目版本控制。复制资源时应生成新 ID，移动资源时同时移动 sidecar。可以使用 `asset://UUID` 引用场景图片和 UI 图片／UI 资源，解析发生在编辑／编译阶段，Player 中没有这套 JSON 解析器。普通相对路径继续支持，手动移动相对路径资源时仍需更新引用。

`.gm/assets.json` 是可重建索引，`.gm/import-cache.json` 是增量缓存。场景缓存键包含模块 schema、编译器版本、场景、HUD 和图片内容指纹；输出文件也校验内容指纹。改变 HUD 引用的图片会使场景失效。缓存命中不再重复解码图片或构造运行场景。内容指纹用于缓存一致性，不是安全签名。

构建扫描 `assets/` 中的所有 `.scene.json`，生成 `build/scenes/*.gmscene` 与 `scenes.gmcatalog`，并将入口复制为 `build/game.gmscene`。发布包含同样的二进制目录。C++ `Context::switchScene(idOrName)`、CLI `runtime.scene.switch` 使用目录中的 ID 或场景文件名（去掉 `.scene.json`）。场景名称应唯一，稳定 ID 是无歧义入口。

Player 在后台线程读取场景字节；校验与 World 构建仍在主线程，成功后替换旧 World。读取或 World 构建失败会提示错误并保留旧场景；纹理随后在主线程渲染路径上传，尚未提供显存不足时的整场景回滚。这不是完整的资源流式加载／GPU 异步上传系统；多个场景仍各自嵌入图片，跨场景共享纹理和细粒度卸载尚未完成。

`runtime.save/load` 将当前组件与存活实体保存为带校验的二进制快照，位置是 `%LOCALAPPDATA%/GMEngine/saves/<模块名>/<slot>.gmsave`，不覆盖场景源文件。恢复时重新建立 World 和 System 状态；不保存 System 私有状态、事件队列或模拟时钟。只有相同 schema 可加载，游戏存档版本迁移与玩家设置服务仍需继续设计。

## 命令实例

```json
{"op":"project.recent"}
{"op":"project.assets"}
{"op":"runtime.entity.create","parent":"player"}
{"op":"runtime.component.set","id":"runtime-...","type":"Transform2D","fields":{"x":100,"y":200}}
{"op":"runtime.component.remove","id":"runtime-...","type":"Velocity"}
{"op":"runtime.entity.destroy","id":"runtime-..."}
{"op":"runtime.prefab.instantiate","source":"player"}
{"op":"runtime.query.area","x":0,"y":0,"w":300,"h":200,"mask":1}
{"op":"runtime.query.ray","x":0,"y":100,"dx":1,"dy":0,"length":500,"mask":1}
{"op":"runtime.scene.switch","scene":"second"}
{"op":"runtime.save","slot":"slot1"}
{"op":"runtime.load","slot":"slot1"}
{"op":"runtime.stats"}
```

项目选择器命令使用 `gmctl --hub`，项目服务用 `--project game.gmproject`，操作正在打开的编辑器用 `--live 会话路径`。生成实体返回字符串 id；空间查询返回 SDK Entity 数值句柄。`runtime.component.set` 接收该组件完整字段集；源资源编辑仍使用原有的 component.set / set / batch，继续支持撤销。

性能页和 runtime.stats 显示模拟步耗时、各 System 当前／峰值耗时、实体数、查询缓存和组件有效数据字节数。字节数不是整个进程内存，也不包含 Flecs 内部分配及纹理占用。

## 对原清单的状态更新

| 模块 | 0.8 已具备 | 尚未完成 |
| --- | --- | --- |
| 项目与游戏代码 | 独立项目、入口、模块构建、应用安装、项目选择器 | 可视化项目设置、平台构建配置 |
| 组件与逻辑 | 自动注册、生命周期、事件、可选读写契约 | 更完整的模块／插件依赖与热重载迁移 |
| 场景管理 | 运行时增删、动态组件、子树实例化、目录切换与旧 World 卸载 | 外部 Prefab、覆盖／变体、叠加场景 |
| 运行调度 | 固定步长、补帧上限、顺序依赖、耗时统计 | 渲染插值、测量后引入并行 |
| 资源管理 | UUID、稳定引用、依赖、增量编译、后台场景读取 | 跨场景共享、细粒度异步加载／卸载、平台纹理格式 |
| 2D 渲染 | 摄像机、旋转／缩放、图集区域、视口裁剪 | 可编辑材质、自定义 Shader 管线 |
| 输入 | Windows 键鼠和 UI／游戏焦点 | 动作映射、手柄、多点触控、输入上下文 |
| 碰撞与物理 | AABB 触发、碰撞层、事件、射线／区域查询 | 精确多边形；刚体按玩法需求决定 |
| 动画与声音 | 精灵图集动画 | 属性动画、音效／音乐／混音生命周期 |
| 游戏 UI | 独立 UI 树、图片和 ECS 数值绑定 | 锚点、布局容器、安全区、焦点导航、滚动／输入控件、通用绑定 |
| 状态与存档 | 与源资源分离的二进制运行快照 | 玩家设置、正式存档模型和版本迁移 |
| 平台与发布 | Windows 安装包、便携包、独立游戏包 | Android／iOS 平台层、移动端部署及真机验证 |
| 调试与质量 | 性能页、System/tick 错误定位、契约与实时测试、Windows CI 配置 | 完整内存／资源分析、GPU 时间线、目标设备测试 |

下一阶段应先完成动作输入与游戏 UI 布局，再接音频及平台接口。移动端发布需要 Android NDK／SDK 与 macOS/Xcode 构建环境；仅能在 Windows 验证的代码不能标记为已完成移动端支持。

## 验证入口

本机 Windows 验证记录（2026-10-30）：7 组 CTest 通过；项目与增量导入检查 13 项、独立项目检查 14 项、编辑器实时场景检查 41 项、安装目录搬迁检查 5 项、纯二进制部署检查 8 项通过。已实际执行安装程序，确认开始菜单、卸载登记和项目文件打开命令；CrystalMeadow 与两份 UI 发布包均通过 Player 的 `--check`。手动验证了项目选择、F5 运行、生命值按钮和停止后保留编辑数据。GitHub CI 是新增配置，执行结果应以仓库 Actions 为准。

- `ctest --test-dir ../GMEngineBuild/build -C Release --output-on-failure`：场景、生命周期、空间功能、编辑服务、UI、图片与渲染契约。
- `scripts/test-v08.ps1`：项目选择器 CLI、增量编译、多场景包、存档、切换、诊断和 Player。
- `scripts/test-project.ps1`：独立 SDK、Unicode 路径、扩展组件／System、错误重载恢复与发布。
- `scripts/test-live.ps1`、游戏工作区 `tools/test-scene-live.ps1`：编辑器实时命令与手动工作区共享服务。
- `scripts/verify-sdk.ps1`：整体移动安装目录后，创建、构建、发布、打开编辑器。
- `scripts/test-installation.ps1`：移除开发工具 PATH，并把 CMake 指向不存在的路径，验证新建、编辑、运行、发布与旧模板恢复都不调用编译器；自定义代码缺少模块时明确失败。`-Headless` 跳过窗口验证供 CI 使用。
- `.github/workflows/windows.yml`：Windows 核心与项目契约。GPU/窗口渲染测试在本地运行，不把云端无设备环境当作真机测试。
