# Unity Godot 与虚幻引擎的资源格式和 AI 操作能力调查

本报告服务于 GMEngine 的设计讨论，评估目标是：开发者可以在编辑器中手动完成的操作，AI 也能通过程序入口完成，并且双方可以继续编辑同一份资源。重点比较场景、UI、预制体及关联资源的可读性、可修改性和编辑器接口。

**主要判断：场景格式优先借鉴 Godot 的文本场景与资源组织；UI 格式重点参考 Unity UI Toolkit 的文档和样式分离；程序操作与手动操作的一致性，参考 Unity 的序列化编辑接口和虚幻的属性编辑、事务机制。** 这是基于下述官方机制的设计判断，不是三个引擎整体能力的排名。

调查日期：2026 年 10 月 28 日。Unity 主要参考 Unity 6 文档中的 6000.0 路径；Godot 固定参考 4.6；虚幻参考查询时标注为 5.8 的官方文档与 Python API。网页会继续更新，涉及新增字段和接口时需以项目采用的版本复核。本报告是官方资料调查，没有在三个引擎中执行同一组自动化任务或测量 AI 成功率。

## 比较标准

需要分别回答四个问题：**读得懂吗、定位得准吗、改得可靠吗、改完能让人继续编辑吗。** 文本格式主要改善前两项；正确的保存、引用维护、变更通知与撤销机制主要影响后两项。结构化文件和操作接口应分别评价。

| 维度 | 具体检查内容 |
| --- | --- |
| 直接读取 | 能否从文件识别对象、层级、属性、资源引用和事件关系 |
| 直接修改 | 局部修改是否清晰，是否需要同步更新多处内部标识或覆盖记录 |
| 程序调用 | 是否能查询对象、编辑资源、保存，以及取得错误信息 |
| 人与 AI 接续编辑 | 程序修改是否进入编辑器状态、变更通知及撤销体系 |
| 复杂资源处理 | 图结构、继承关系、导入产物是否有专门接口，而非只暴露原始数据 |

下表中的“较适合”表示存在清楚的文本或 API 路径；“有条件”表示需要了解引用、继承、布局等规则；“不宜直接改”表示应优先经过引擎接口。它们都不等于无需验证。

## 资源格式对照

| 引擎与资源 | 文件或表示 | AI 直接读取与修改 | 更合适的操作路径 |
| --- | --- | --- | --- |
| Unity 场景与预制体 | 文本模式下的 UnityYAML | 可读取；结构修改有较多约束 | 读取文本辅助理解，通过编辑器 API 修改结构 |
| Unity uGUI | 场景或预制体中的 GameObject 与组件 | 有条件；继承普通场景的引用复杂度 | 修改组件、布局和预制体的 API |
| Unity UI Toolkit | UXML 与 USS | 较适合结构、文本和样式修改 | 文本编辑配合 UI Builder 预览 |
| Unity 资源元数据 | 旁置的 .meta | 可读，但标识不能随意重建 | 资源管理接口维护身份和导入设置 |
| Godot 场景与 UI | .tscn | 较适合；节点、属性和连接有文本表示 | 局部文本修改或节点与资源 API |
| Godot 配置与主题等资源 | .tres | 简单属性资源较适合 | 文本编辑或 Resource API |
| Godot 二进制资源与导入缓存 | .scn、.res、.godot/imported | 不宜直接按文本修改 | 修改源资源或导入设置，再保存或重导入 |
| 虚幻关卡与常规资产 | .umap、.uasset | 二进制，不适合直接文本修改 | 编辑器脚本、专用资产 API |
| 虚幻 UMG 与蓝图资产 | Widget Blueprint、Blueprint | 文件层面不适合直接文本修改 | 结构查询与编辑 API，必要时补充编辑器扩展 |
| 虚幻 DataTable 的外部数据 | CSV 等导入源 | 较适合表格数据修改 | 修改源文件后重导入 |
| 虚幻 Slate UI | C++ 声明式代码 | 代码可读可改 | C++ 编辑和构建流程 |

依据分别见下文各引擎的官方来源。这里的文件类型只描述对应资源，不表示引擎中的所有资产都采用同一种格式，也不表示可读的源文件等同于运行时最终状态。

## Unity

### 场景文本有用但不宜作为唯一编辑入口

Unity 的场景和预制体可以采用文本序列化；Unity 6 的 Editor Settings 将 Force Text 列为默认选项。文本场景有利于搜索与版本比较，但 Force Text 并不意味着 PNG、音频等原始媒体会变成可编辑的场景文本。[Editor Settings](https://docs.unity3d.com/6000.0/Documentation/Manual/class-EditorManager.html)、[文本场景格式](https://docs.unity3d.com/6000.0/Documentation/Manual/TextSceneFormat.html)

UnityYAML 是特定的 YAML 子集。一个场景对象与它的组件分别序列化，通过 fileID 等引用关联；跨文件引用还涉及资源身份。AI 能读取这些关系，但理解一个对象往往需要追踪多个记录，普通 YAML 工具也不能默认兼容全部细节。[序列化格式说明](https://docs.unity.com/en-us/engine/6000.6/manual/working-with-scenes/text-scene-format/format-description)、[UnityYAML](https://docs.unity3d.com/6000.0/Documentation/Manual/UnityYAML.html)

预制体实例、嵌套预制体和变体会引入源资源、属性覆盖及占位对象。修改树结构或替换基础预制体，可能需要同时维护多处引用。因此，**查看位置、名称、引用比较容易；批量重构层级和继承关系的难度明显更高。** [Unity 官方 YAML 说明](https://unity.com/blog/engine-platform/understanding-unitys-serialization-language-yaml)

这里有一个官方资料口径差异：UnityYAML 手册写明不能在外部生成或编辑该格式，而 Unity 官方博客展示了直接修改 YAML 的用途，并建议尽量使用 Asset Database API。合理的工程判断是：直接修改在技术上存在可行案例，但不应据此把原始 YAML 当作有完整兼容保证的公共编辑协议。[UnityYAML 手册](https://docs.unity3d.com/6000.0/Documentation/Manual/UnityYAML.html)、[官方博客](https://unity.com/blog/engine-platform/understanding-unitys-serialization-language-yaml)

### 两套 UI 的资源体验不同

**UI Toolkit 是值得重点参考的设计。** UXML 表达 UI 结构，USS 表达样式；UI Builder 能可视化编辑这些资源，也提供用 IDE 打开 UXML 的入口。AI 可以修改文档和样式，人再通过同一套 UI 资源继续调整。USS 借鉴 CSS，但具有 Unity 自己的扩展和限制。[UXML](https://docs.unity.com/en-us/engine/6000.0/manual/uitoolkits/uielements/uie-structure-ui/uie-uxml/uie-writing-uxmltemplate)、[USS](https://docs.unity3d.com/6000.0/Documentation/Manual/UIE-USS.html)、[UI Builder](https://docs.unity3d.com/6000.0/Documentation/Manual/UIB-interface-overview.html)

需要区分文档结构与实际展开后的 UI：模板实例和动态生成的元素不一定完整写在当前 UXML 中；最终效果还取决于样式和逻辑。UI Builder 的活动文档也有自己的保存状态，官方明确指出项目级保存不负责保存其活动 UXML。对 AI 而言，“修改了文件”“当前预览已更新”“编辑器已保存”应当分别确认。[UI Builder 文档](https://docs.unity3d.com/6000.0/Documentation/Manual/UIB-interface-overview.html)

**uGUI 使用 GameObject 和组件工作流。** 它能复用场景与预制体系统，适合通过组件 API 操作；从直接文本编辑角度看，UI 会继承 Unity 场景序列化的复杂度。不能将 UI Toolkit 的文档式编辑优势泛化到所有 Unity UI。[Unity UI 系统比较](https://docs.unity3d.com/6000.0/Documentation/Manual/UI-system-compare.html)

### 接口设计最值得借鉴的部分

SerializedObject 与 SerializedProperty 允许程序通用地编辑序列化字段，并处理变更标记和 Undo 相关工作。直接给对象属性赋值，并不自动具备相同的编辑器行为。这与 GMEngine 的需求非常接近：**程序操作需要具备手动操作的编辑语义，而不只是改掉一个内存值。** [SerializedObject API](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/SerializedObject.html)

对预制体，LoadPrefabContents 提供加载到隔离场景、修改、保存并卸载的工作方式。这比让 AI 自行重建复杂序列化记录更清楚，也适用于批量处理。[PrefabUtility.LoadPrefabContents](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/PrefabUtility.LoadPrefabContents.html)

资源身份与导入设置通过 .meta 保存。稳定身份有利于移动和重命名；代价是资源文件与 .meta 必须配套维护，丢失 .meta 会破坏引用。AI 操作不能只看见主文件就忽略它的元数据。[Asset metadata](https://docs.unity3d.com/6000.0/Documentation/Manual/AssetMetadata.html)

**对 GMEngine 的判断：** 借鉴 UI Toolkit 的资源表达和序列化编辑 API；普通场景文件应尽量减少需要手工同步的内部记录。资源身份值得保留，但需要统一接口负责维护。

## Godot

### 场景和资源的文本表示比较直接

Godot 的 .tscn 用节点、属性、外部与内部资源、信号连接描述场景树；.tres 用于文本资源，.scn 和 .res 对应二进制形式。对于小型场景、UI 层级、主题或参数资源，文本与编辑器对象之间的对应关系比较直接。[TSCN 格式](https://docs.godotengine.org/en/4.6/engine_details/file_formats/tscn.html)、[Resources](https://docs.godotengine.org/en/4.6/tutorials/scripting/resources.html)

Resource 的一个优点是：自定义属性既能序列化，也能由 Inspector 展示和编辑。AI、脚本与人工属性面板可以围绕同一种资源类型工作，不必各自定义一套独立数据结构。[Resources](https://docs.godotengine.org/en/4.6/tutorials/scripting/resources.html)

文本易读仍有边界：默认值通常被省略，保存时注释和额外空白会被丢弃；带大量网格或动画数组的资源也可能很长。因此，AI 不能只凭字段缺失判断没有该属性，也不能把编辑器可能丢弃的注释当作持久说明。[TSCN 格式](https://docs.godotengine.org/en/4.6/engine_details/file_formats/tscn.html)

### UI 复用场景体系但必须理解布局规则

Godot 的 Control 和 Container 可以组成 UI 节点树；Container 会接管子控件的位置和尺寸分配。AI 若直接修改被容器管理的坐标，修改可能被忽略或在下一次布局时覆盖。更合适的对象是容器类型、尺寸标志、边距等布局规则。[Using Containers](https://docs.godotengine.org/en/4.6/tutorials/ui/gui_containers.html)

这是一个值得借鉴的统一性：场景和 UI 可以共用节点、属性、资源引用与保存机制。但格式必须让 AI 看出某个值是用户设定、继承结果，还是由布局系统计算出来的。后一句是对 GMEngine 的设计推论。

### 标识和保存语义需要特别注意

Godot 4.6 的格式文档已经描述节点 unique_id，用于追踪移动或重命名后的节点；旧场景不保证带有该字段。资源也有 UID，但 NodePath 仍用于一些节点和动画属性引用。因此不能把现代 Godot 简化为“只有名字和路径”，也不能假定新增 ID 会自动解决所有文本重命名问题。[TSCN 格式](https://docs.godotengine.org/en/4.6/engine_details/file_formats/tscn.html)

程序生成场景时还有一个容易遗漏的规则：把节点加入父子层级，并不等于它会被保存。PackedScene 的打包会考虑 Node.owner；owner 没设对，就可能出现编辑时看见了节点，保存后却缺失的情况。对于 AI，这类规则应由较高层的创建操作处理。[PackedScene](https://docs.godotengine.org/en/4.6/classes/class_packedscene.html)

### 编辑器接口适合扩展但仍需封装操作

EditorInterface 提供当前场景、选择、资源检查、保存、运行场景等入口；EditorUndoRedoManager 让插件操作接入对应场景的撤销历史。它们足以作为 AI 操作层的重要基础，但仍需要把节点操作、owner、保存与撤销组织成完整流程，不能由“有脚本 API”直接推出“所有 GUI 操作已一一开放”。[EditorInterface](https://docs.godotengine.org/en/4.6/classes/class_editorinterface.html)、[EditorUndoRedoManager](https://docs.godotengine.org/en/4.6/classes/class_editorundoredomanager.html)

导入系统也区分源资源、旁置的 .import 配置和 .godot/imported 中的生成结果。AI 应优先修改源资源或导入设置；直接改缓存容易在重导入时被覆盖。[Import process](https://docs.godotengine.org/en/4.6/tutorials/assets_pipeline/import_process.html)

**对 GMEngine 的判断：** Godot 是场景格式和统一资源体系最直接的参考对象；需要进一步改善的是有效属性查询、引用重构，以及让节点创建和保存规则对调用者更简单。

## 虚幻引擎

### 主要资源不适合直接文本编辑

虚幻的常规资产 .uasset 和关卡 .umap 是二进制文件，不能像文本一样直接阅读或使用普通文本合并工具。UMG 的常见可视化工作流以 Widget Blueprint 为单位，包含 Designer 与 Graph 视图；它也不是 UXML 那样的独立文本 UI 文档。[Perforce 工作流中的文件格式说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-perforce-as-source-control-for-unreal-engine)、[UMG 工作流](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engines-systems-and-workflows-overview-for-unity-developers)

因此，AI 通过通用文件读取工具难以直接理解这些资源；需要引擎把对象、属性和图结构转换成可查询的信息。二进制格式限制的是直接文件路径，不代表资源不能被自动化修改。这是对上述格式与下述 API 的综合判断。

One File Per Actor 将关卡中的 Actor 数据拆到外部文件，减少多人编辑同一文件的冲突；但外部文件名是编码后的名称，需要编辑器辅助识别。它值得借鉴的是修改粒度，不能把“拆成多个文件”直接视为“变成易读文本”。[One File Per Actor](https://dev.epicgames.com/documentation/en-us/unreal-engine/one-file-per-actor-in-unreal-engine)

### 属性编辑与事务机制很有参考价值

虚幻提供 Python 编辑器脚本。get_editor_property 与 set_editor_property 可访问所公开的编辑器属性；其中 set_editor_property 会触发与 Details 面板编辑对应的前后处理，比直接写 Python 对象字段更接近人工编辑行为。脚本还可通过 ScopedEditorTransaction 将支持撤销的操作组成一个历史项；导入模型等本身不可撤销的操作不会因此自动获得撤销能力。这里的 Python 是编辑器自动化能力，不是游戏运行时脚本语言。[Python 编辑器脚本](https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python)

Remote Control API 提供 HTTP 与 JSON 入口，能够调用已加载 UObject 上符合条件的函数、读取或修改属性。事务写入模式会处理编辑通知和撤销。这非常接近“人用面板、AI 用命令”的目标。但它受对象是否加载、函数暴露方式及属性权限约束，不能把它理解为编辑器任意操作的万能远程接口。[Remote Control HTTP API](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-control-api-http-reference-for-unreal-engine)

### 蓝图可以程序化操作但要核验具体覆盖范围

不能简单地说“蓝图只能用鼠标编辑”。查询时的 Unreal Python 5.8 文档中，BlueprintEditorLibrary 已提供创建蓝图资产、添加函数图和成员变量、查询图与引脚、编译蓝图等能力。这说明二进制资产也能具有结构化的读写入口。[BlueprintEditorLibrary](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/BlueprintEditorLibrary)

不过，某个库提供图查询或变量编辑，并不能证明任意节点创建、连线、UMG 层级和动画编辑都具有完整的同等入口。WidgetBlueprint 本身也只公开特定的编辑器属性。实际接入时应逐项验证目标操作，并在不足处使用相应的 C++ 编辑器扩展；本报告不对全部界面操作作覆盖承诺。[WidgetBlueprint API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/WidgetBlueprint)

### 适合文本工作的例外

DataTable 可以从 CSV 等外部数据导入，字段需要符合指定行结构。它适合 AI 修改数值和配置后重新导入，但应区分外部源文件和导入后的资产，明确哪一份是数据的维护入口。[Data Driven Gameplay Elements](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine)

Slate 使用 C++ 声明式语法构建 UI，适合代码生成和修改。但是，写 Slate C++ 与修改现有 Widget Blueprint 是不同的资源工作流；Slate 可编程，并不意味着现有 UMG 文档能直接转换成同等的可视化与文本双向编辑体验。[Slate Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/slate-overview-for-unreal-engine)

**对 GMEngine 的判断：** 虚幻最值得借鉴的是属性反射、编辑通知、事务和远程调用。若我们的目标还包括 AI 不启动编辑器就能读懂场景，则不宜只提供二进制场景和 UI 资产。

## 用同一个操作比较三种路径

以“修改主菜单按钮文字，调整它在布局中的位置，保存后让人继续编辑”为例，下面是依据资源机制推导的工作路径，不是已执行的基准测试。

| 工作流 | AI 可以采取的路径 | 必须检查的地方 |
| --- | --- | --- |
| Unity UI Toolkit | 修改 UXML 中的按钮属性与 USS 布局规则 | 模板、动态元素、样式覆盖以及 UI Builder 保存状态 |
| Unity uGUI | 通过编辑器 API 定位对象并修改组件和布局 | 预制体实例覆盖、布局组件、撤销和保存 |
| Godot UI | 修改 .tscn 或通过节点 API 修改 | 父容器是否接管布局、主题或继承覆盖、节点保存归属 |
| 虚幻 UMG | 经编辑器接口读取并修改 Widget Blueprint 的相应结构 | 所需 API 是否开放，修改的是持久资产还是运行实例，以及编译、保存结果 |

这说明 UI 是否适合 AI，不只取决于“文件是不是文本”，还取决于是否能准确说明某个属性由谁决定、修改应该写回哪里。

## GMEngine 当前可以借鉴的设计

以下是调查后的设计建议，不是三个引擎的既有能力保证，也不预先决定 GMEngine 的具体文件语法。

| 建议 | 对应参考 | 对当前需求的意义 |
| --- | --- | --- |
| 场景及配置采用清楚的文本结构 | Godot TSCN 与 TRES | AI 可以离线读取，开发者可以审查差异 |
| UI 结构和可复用样式有明确表达 | Unity UXML 与 USS | 人工可视化编辑和 AI 文本修改有共同资源基础 |
| 属性类型同时服务面板与程序入口 | Godot Resource、Unity SerializedProperty、虚幻编辑器属性 | 减少人工与 AI 两套规则发生分歧 |
| 修改入口处理编辑通知、保存和撤销 | Unity 序列化编辑、Godot 撤销管理、虚幻事务 | AI 修改后，人能够可靠接手 |
| 对象身份稳定且能按语义查询 | Unity 资源 ID、Godot UID 与节点标识 | 重命名和移动时减少误定位与引用损坏 |
| 源资源、导入配置与生成产物分清楚 | Godot 导入流程、Unity 元数据 | AI 知道该改哪一份内容 |
| 图形资源公开节点、端口和连接 | 虚幻图查询机制及其覆盖边界 | 图形编辑器中的关系能被程序理解和修改 |

相对不适合本项目目标的设计包括：场景和 UI 只保存为无法独立查询的二进制；文本中混入大量不透明的数据块；一次简单修改要求调用者同步维护多份内部关系；程序改属性绕过编辑器通知和撤销；文件文本、当前编辑状态与实际运行状态之间缺少清楚的对应关系。这里评价的是这些做法对本项目目标的影响，并非它们在性能或大型生产管线中没有价值。

下一步最值得讨论的是一个具体的往返流程：**同一个 UI，开发者拖拽修改后，AI 能读取其有效结构；AI 修改后，开发者重新打开能继续拖拽编辑，且内容、引用和修改记录保持一致。** 先用这个要求检验候选资源设计，比立刻选择 JSON、YAML 或 XML 更能发现问题。

