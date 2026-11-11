# RaceMenu Face 覆盖层的几何与材质生命周期

调研日期：2026-11-11。范围：RaceMenu 的 Face 安装、NIF/材质生命周期、透明与深度状态。证据包括本地源码、上游 1.6.1170 源码、从本机安装版 `RaceMenu.bsa` 解出的实际模板，以及 SLACS 的适配器探针。此前的资源和旧脚本对照见 [Face 叠层调查](face-overlay-stacking.md)。

## 结论

- RaceMenu 没有把 Face 限成单层：更新循环逐槽卸载、重建；每个新槽单独加载 NIF，从该次加载取得 shader/alpha 属性。可见源码没有把五槽合并成一份最终材质的步骤。
- **Face 输入头部是 FaceGen（4），输出覆盖层却由模板决定。实际 Face、Body、Hands、Feet 模板均为 SkinTint（5）。** 将输入 shader 类型当成输出类型，会误判纹理槽和渲染路径。
- 实测四种模板都开启 ZTest/ZWrite，材质 alpha=1，NiAlpha flags=4845、threshold=0。Face 与其他模板的 shader flags 差异是 bit10 `Facegen_Detail_Map`。Face 并没有独有的“关闭混合”或“alpha 为零”配置。
- 五条 `AddNodeOverride` 记录本身不证明五个 live 几何存在。记录存到 actor/性别/节点名映射；应用时只查找已有节点并跳过不存在的节点。应把记录读取与 live 节点/材质读取分开。
- 几何直接沿用头部变换，没有按层增加位置或深度偏移；模板又开启深度写入，因此深度/decal/排序是具体的渲染候选。但 Body 也有这组状态，不能直接认定它是 Face 症状根因。本轮未在游戏里复现或修复渲染异常。

## Face 槽位如何生成

`Face [Ovl{}]` 使用 `face_overlay.nif`，与 Body 的 `Body [Ovl{}]` / `body_overlay.nif` 分开定义。[OverlayInterface.h:23](<E:/source/repos/RaceMenu/skee64/OverlayInterface.h:23>) [OverlayInterface.h:33](<E:/source/repos/RaceMenu/skee64/OverlayInterface.h:33>) Face 更新循环按配置数量逐个卸载并重装 Face 节点，每个槽都传入 `FaceGen` 作为源 shader type。[OverlayInterface.cpp:1094](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1094>) Face 安装入口从 actor 的 head data 取 `headTexture`，在对应 head part 中找到首个 FaceGen shader 几何，再把该几何和纹理集交给通用安装器。[OverlayInterface.cpp:544](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:544>)

这里的 `FaceGen` 是用于选择**输入头部几何**的条件，不意味着输出 overlay 使用 FaceGen 材质。通用安装器实际从 `face_overlay.nif` 加载的几何中取得 shader property 和 alpha property；之后再把源几何的一些状态、变换和纹理路径写到新形状上。[OverlayInterface.cpp:113](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:113>) [OverlayInterface.cpp:154](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:154>)

## 每槽 NIF、几何与属性指针

当目标节点名尚不存在时，`InstallOverlay` 为该次调用创建 `BSResourceNiBinaryStream` 和栈上 `NifStreamWrapper`，加载 NIF 并遍历其 root objects 找到一个 `NiGeometry`。它先从加载出的几何保存 `m_spEffectState`、`m_spPropertyState`，再按输入源几何类型调用 `CreateBSDynamicTriShape()` 或 `CreateBSTriShape()` 创建目标形状，命名为当前槽位名并设 `kAlwaysDraw`。[OverlayInterface.cpp:96](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:96>) [OverlayInterface.cpp:101](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:101>) [OverlayInterface.cpp:132](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:132>)

新形状随后直接接收保存的 shader 和 alpha property `NiPointer`；没有在这里调用属性的 `Clone()`。Dynamic 路径会复制源几何的动态顶点数据，除非命中 `g_adjustedBlocks` 分支而共享那段动态数据并递增引用计数。目标的 `m_localTransform` 和 `m_spSkinInstance` 直接取自输入头部几何。[OverlayInterface.cpp:154](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:154>) [OverlayInterface.cpp:164](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:164>) [OverlayInterface.cpp:187](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:187>) 这一代码段没有 slot-index 相关的平移或深度偏移；静态形状的原生创建细节则封装在 `CreateBSTriShape()` 中。

源码每个新槽位的安装调用会重新走这条加载路径，并在安装完成后把形状附加到 destination node。[OverlayInterface.cpp:258](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:258>) `NifStreamWrapper::LoadStream` 只是转调原生 `NiStream::LoadStream`；该原生方法的实现不在当前 RaceMenu 源码树里。[NifUtils.cpp:1162](<E:/source/repos/RaceMenu/skee64/NifUtils.cpp:1162>) 因此结论限于：本地可见 C++ 没有显式的跨槽 NIF/material 缓存或共享赋值；原生加载器内部是否有共享对象行为仍未由这份代码证明。

## 材质、纹理集、alpha 与覆盖应用

安装时，RaceMenu 从新形状的 shader property 取得 target material，然后复制 Face 头部 texture set 中索引 1 及以上的路径，刷新 shader 纹理。`g_overlayForceDecal` 开启时会在 target shader flags1 上设置 `kSLSF1_Decal`；它不会改写 shader 的完整 flags 集合，也没有显式写 ZTest/ZWrite 或 blend 状态。[OverlayInterface.cpp:193](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:193>) [OverlayInterface.cpp:206](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:206>) [OverlayInterface.cpp:218](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:218>)

若 `g_overlayAlphaOverride` 开启，安装器会把配置中的 `alphaFlags` 与 `alphaThreshold` 写进目标几何的 `NiAlphaProperty`。[OverlayInterface.cpp:242](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:242>) 本地默认值在 `main.cpp` 初始化，随后可以从 `[Overlays/Data]` 配置节读取覆盖值，所以默认不等于用户运行时配置。[main.cpp:130](<E:/source/repos/RaceMenu/skee64/main.cpp:130>) [main.cpp:783](<E:/source/repos/RaceMenu/skee64/main.cpp:783>)

安装器在附加新形状前调用 `Impl_ApplyNodeOverrides`。该函数遍历对象内的几何，按几何名匹配 actor 的覆盖记录，再对匹配几何调用 `SetShaderProperty`。[OverlayInterface.cpp:250](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:250>) [OverrideInterface.cpp:1012](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:1012>) [OverrideInterface.cpp:1118](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:1118>) key 8（`kParam_ShaderAlpha`）写入当前 lighting material 的 `alpha` 字段；它不写 `NiAlphaProperty` 的 flags 或 threshold。[ShaderUtilities.cpp:283](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:283>) key 9 的纹理更新会从当前 material 的纹理路径创建新的 `BSShaderTextureSet`、替换指定路径并赋回该 material，然后加载纹理并重新初始化 shader。[ShaderUtilities.cpp:147](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:147>) 这是对当前 material 的按节点更新；若底层 material 指针别名，代码没有另行复制 material 来隔离这些写入。

## 覆盖记录不等于 live 几何

`Impl_AddNodeOverride` 仅把 variant 写入按 actor formID、性别和节点名索引的数据结构。[OverrideInterface.cpp:56](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:56>) `Impl_SetNodeProperties` 后续才遍历当前根节点、查找记录中的节点名，并且只在 `foundNode` 存在时应用属性；找不到节点就跳过。[OverrideInterface.cpp:777](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:777>) 所以 `GetNodeOverride` 能读到五槽记录，不足以证明 FaceGen 当前有五个 `Face [OvlN]` 形状。应使用 live-node 的 `GetNodeProperty` 或直接遍历 3D 节点来确认实际材质值。

## 能区分材质别名与几何缺失的观察

在同一演员、同一次 3D 构建后，逐槽读取 `Face [Ovl0..4]` 的 live 几何，并比较：

1. 节点是否存在及其 geometry 指针；
2. `m_spEffectState`、其中的 material、`material->textureSet` 指针是否跨槽相同；
3. 每个 textureSet 的纹理路径、NiAlphaProperty 指针及其 flags/threshold、material alpha；
4. shader 的 ZTest/ZWrite 状态及 flags。

若仅部分 live 节点存在，问题在节点生成/容量这一层；若五个 geometry 存在但 shader/material/textureSet 指针相同，材质别名分支得到直接证据；若指针各异且路径、alpha 均符合各槽设置，则“共享一份最终材质”不再是该次状态的解释。以上均需对用户实际 DLL/游戏状态读取；本轮没有 GPU 或实机验证。

## 安装版 BSA 的实际模板测量

读取的是本机 `D:/tuanzimouse/mods/RaceMenu Anniversary Edition/RaceMenu.bsa`，不是测试者机器的覆盖结果。归档 SHA256 为 `2e4a47aefa8a12dad1b3d3dfa58f3b39e9672b24e66e4d0a3c2b0b7ddcc6c6ab`。直接解压其中四个 normal overlay NIF 并按 Skyrim NIF 字段布局解析；没有改动已安装文件。完整原始字段、块引用和哈希保存在 [测量 JSON](data/racemenu-normal-templates-2026-11-11.json)。

| 字段 | Face | Body / Hands / Feet |
| --- | --- | --- |
| NIF shader type | 5（SkinTint） | 5（SkinTint） |
| Shader flags1 | `0x82601703` | `0x82601303` |
| Shader flags2 | `0x02008081` | `0x02008081` |
| Facegen Detail Map，flags1 bit10 | 开启 | 关闭 |
| ZTest，flags1 bit31 | 开启 | 开启 |
| ZWrite，flags2 bit0 | 开启 | 开启 |
| Material alpha | 1.0 | 1.0 |
| NiAlpha flags / threshold | 4845 / 0 | 4845 / 0 |
| Blend | SrcAlpha / InvSrcAlpha | SrcAlpha / InvSrcAlpha |
| Alpha test | Greater，阈值 0 | Greater，阈值 0 |
| 模板 Decal 位 | 关闭 | 关闭 |

文件自带的 diffuse 均为 `Textures\\Actors\\Character\\Overlays\\default.dds`；其余八个纹理路径为空。四个 NIF 的形状都引用本文件内的 shader 块 2 和 alpha 块 4。安装器随后会复制角色纹理的 index 1..8，再应用 SLACS 覆盖；这些是模板初始值，不是最终 live 材质值。

`g_overlayForceDecal` 开启时，源码会给所有这些模板设置 flags1 bit26；按测量值推算，Face 变为 `0x86601703`、Body 变为 `0x86601303`，bit10 差异仍保留。`bAlphaOverride` 只重写 NiAlphaProperty，不关闭 ZWrite。字段解码依据为 [NifTools 的 NIF schema](https://github.com/niftools/nifxml/blob/develop/nif.xml)；材质 alpha 在本次 112-byte shader 块内的偏移是 64。

复跑工具依赖 Python 和 `lz4`，只支持本次涉及的 SSE BSA v105 / NIF 20.2.0.7、user 12、BS 83 布局：

```powershell
py -B tools/research/inspect_racemenu_templates.py "D:/tuanzimouse/mods/RaceMenu Anniversary Edition/RaceMenu.bsa" --output artifacts/face-research/template-properties.json
```

## 源码版本对照

本地源码对应较新的运行时目标。为核对 1.6.1170，另外读取上游 commit [`348607e9ae5f360ccab0d623e8b0d8f42e586fa6`](https://github.com/expired6978/SKSE64Plugins/tree/348607e9ae5f360ccab0d623e8b0d8f42e586fa6)：其 [main.cpp](https://github.com/expired6978/SKSE64Plugins/blob/348607e9ae5f360ccab0d623e8b0d8f42e586fa6/skee64/main.cpp#L728-L732) 明确声明 Steam 1.6.1170；[OverlayInterface.cpp](https://github.com/expired6978/SKSE64Plugins/blob/348607e9ae5f360ccab0d623e8b0d8f42e586fa6/skee64/OverlayInterface.cpp) 与本地文件忽略行尾后，只有新增 `<format>` 和 `kAlwaysDraw` 两处差异。逐槽 NIF 加载、Face 头部选择、属性赋值、变换、纹理复制及 alpha 配置流程相同。

`kAlwaysDraw` 是 2026-08-23 的 [1.7.99 更新](https://github.com/expired6978/SKSE64Plugins/commit/9ebcb733e17be695f994cd2e9cc383043446bc02) 新增的，不能将它当成已安装旧 DLL 的事实。该版本对照支持本文对建模/材质流程的结论，但没有建立测试者 DLL 与某次源码构建的一一对应。

## 对 Face 一层与五层现象的判断

| 解释 | 本轮证据 | 判断 |
| --- | --- | --- |
| SLACS 只提交一层或五次提交同一张图 | 五槽适配器探针写入五个节点名和不同 diffuse；旧新 DDS 相同但各索引不同 | 在探针条件下排除 |
| RaceMenu 源码把 Face 槽合并成同一材质 | 每槽新 NIF 加载，按节点名应用；没有跨槽合并步骤 | 源码不支持；原生加载器的实际指针身份仍需 live 数据 |
| Face 模板不启用透明混合，或 alpha=0 | 实际 NIF 的混合开启、alpha=1；与 Body 相同 | 本机归档中排除 |
| Face 输出仍是 FaceGen 4，导致 SLACS 误写 FaceGen 通道 | 输出模板是 SkinTint 5；SLACS Face 仅写 diffuse/normal | 此前混淆输入和输出的解释不成立 |
| 多个重合几何受到深度/decal/绘制顺序影响 | 无层间位置偏移，ZWrite 开启；所有区域共有 | 有机制依据，尚未验证为症状原因 |
| Face 专属 detail-map 标志或头部继承纹理参与渲染差异 | bit10 与头部纹理复制已确认 | 有明确 Face 差异，尚未证明会丢层 |

下一步首先读取真实节点、材质和纹理，分清“没创建五层”与“五层都创建但显示相互遮挡”。只有后者成立，才对 Face 模板的 ZWrite、detail-map 标志逐项做隔离 A/B；不同时更改透明阈值、纹理索引和排序。这能避免把修改后的偶然观感当作已定位根因。上游 [issue #58](https://github.com/expired6978/SKSE64Plugins/issues/58) 仅提供覆盖层顺序反转的报告，涉及全部区域且没有根因说明，不能用来确定本次 Face 症状。

## 调研源码身份

本地 `E:/source/repos/RaceMenu` 目录没有 Git 元数据，无法给它标记 commit；该 `main.cpp` 声明的目标运行时是 Steam 1.7.99 / GOG 1.6.1179，并非用户报告的 1.6.1170 DLL。[main.cpp:719](<E:/source/repos/RaceMenu/skee64/main.cpp:719>) 本次检查的关键文件 SHA256：

| 本地源码 | SHA256 |
| --- | --- |
| `skee64/OverlayInterface.cpp` | `68E6A9B34B539C7ADF6D9A1A0F23427C03FEE6AB9E35BFACD4F947C9164DDE46` |
| `skee64/OverrideInterface.cpp` | `BF1C215FB1B93E085BB63A0928A641C7C30470D1DDF5E1F38815D703E289847F` |
| `skee64/ShaderUtilities.cpp` | `09F8B60BE9BA84E9024C91C4432B971C1A4E41E29BC0F6CE79F39B5D3FDCF863` |
| `skee64/NifUtils.cpp` | `F0269797C383B59985B839CF8C9721B7CF67B19A504EE11AB6B5DCC2B843DCA7` |
| `skee64/main.cpp` | `3BBF32F3490EADE987E663BFC6D6742A45BF70E6A67CB90D3EB19529F457FA40` |
