# SLACS2 旧版行为、资源与 SexLab P+ 契约研究

研究日期：2026-11-05。用途：为面向新存档的 C++ 重构提供行为证据与算法决策依据。已确认直接使用 RaceMenu C++ API，不要求安装 SlaveTatsNG；旧存档迁移不作为设计约束。本文没有修改旧源码、PEX、ESP 或游戏资源，没有安装依赖或运行游戏。

标记含义：**事实**是本地源码或文件直接可见的内容；**推断**是从调用链得出的后果；**建议**是新实现的候选设计；**待验证**需要运行时、原始工程或额外一手资料确认。源码中的注释不能替代实际实现。下列链接使用完整绝对路径；二进制资源没有源码行号，因此使用记录 ID 和十进制文件偏移定位。

## 1. 应以哪一份源码为准

**事实：**包内存在两套同名源码，`Source/Scripts` 并非 `Scripts/Source` 的简单副本。按字节比较，只有 `Dirty_BathingQuest.psc` 完全相同；其余四个脚本均不同。修改时间和实现内容都表明 `Source/Scripts` 是较新的研究基线，但这不能证明 PEX 与该源码逐字对应。

| 脚本 | `Scripts/Source` 字节数 / 行数 | `Source/Scripts` 字节数 / 行数 | 具体变化与证据 |
|---|---:|---:|---|
| Main | 20,566 / 593 | 39,033 / 1,298 | 旧版从 Add/Clear ModEvent 进入；新版新增 P+ orgasm 管线、衰减、游泳、GC、显式层数与法术同步。[旧文件](<E:/source/repos/SLACS2 p+ 0.7/Scripts/Source/SLACS_Main.psc:26>)；[新事件注册](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:105>) |
| CumEffectHandler | 2,819 / 93 | 4,408 / 145 | 旧版依赖 `sslThreadController` / `thread.Animation`，每轮检查游泳、天气和计时；新版用 P+ `SexLabThread`，环境检查移出，但仍在效果开始时加层、结束时清除。[旧实现](<E:/source/repos/SLACS2 p+ 0.7/Scripts/Source/SLACS_CumEffectHandler.psc:12>)；[新实现](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_CumEffectHandler.psc:19>) |
| SpellHandler | 6,097 / 163 | 20,439 / 594 | 旧版扫描 RaceMenu 实际纹理、将数量截断到 2、拼接法术名称；新版维护 JContainers 状态、27 项缓存和 DbFormTimer 验证队列。[旧算法](<E:/source/repos/SLACS2 p+ 0.7/Scripts/Source/SLACS_SpellHandler.psc:35>)；[新状态入口](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:487>) |
| MCM | 20,807 / 567 | 25,980 / 688 | 新版展示自然衰减、雨水、游泳、时长、阈值和女性体液选项，并继续提供测试层数、快捷键和槽位设置。[新版页面](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:94>) |
| Dirty_BathingQuest | 8,141 / 218 | 8,141 / 218 | 两份字节相同；是完整上游洗澡脚本的覆盖，加入一个清洗通知。[事件发送处](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/Dirty_BathingQuest.psc:140>) |

文件系统记录：新版 Main 修改于 2026-02-23，另外三个新版 SLACS 脚本修改于 2026-02-22；旧版 SLACS 文件修改于 2025-11 至 2025-12。包内 Main/MCM PEX 修改时间也在 2026-02，但未反编译确认。包标记仍为 `d2025.12.8`，安装包名为 `SLACS 2 v0.79.5u.7z`，不能用其推断准确源码版本。[meta.ini](<E:/source/repos/SLACS2 p+ 0.7/meta.ini:4>)

**待验证：**包内还存在 [SLACS_MainAlias.pex](<E:/source/repos/SLACS2 p+ 0.7/Scripts/SLACS_MainAlias.pex>) 和 [SLACS_PlayerAlias.pex](<E:/source/repos/SLACS2 p+ 0.7/Scripts/SLACS_PlayerAlias.pex>)，但两套目录都没有对应 PSC。不能将其内部维护/读档行为当作已知。下文分析的是可读源码与 ESP 结构，不声称重现所有已编译行为。

## 2. 包内资源和 ESP 的实际职责

对 [SLACS_2_Plus_SCO.esp](<E:/source/repos/SLACS2 p+ 0.7/SLACS_2_Plus_SCO.esp>) 的 TES4/GRUP/记录头、子记录、VMAD 做只读结构解析，得到：

| 项目 | 直接观察 |
|---|---|
| 大小、masters | 39,917 字节；`Skyrim.esm`、`SexLab.esm`、`SkyUI_SE.esp`、`RaceMenu.esp` |
| 记录数 | TES4×1、MGEF×59、SPEL×26、QUST×3、EFSH×26、FLST×2 |
| SexLab 覆盖 | 33 个 MGEF 是 SexLab 原记录覆盖，全部挂 `SLACS_CumEffectHandler`；另外 26 个 MGEF 为本插件自有 |
| Quest | `SLACS_Main_Q`（原始文件 FormID `04000D62`，偏移 23,423）、`SLACS_MCM_Q`（`04000D63`，23,034）、`SLACS_SpellHandler_Quest`（`04002E19`，23,739） |
| FormList | `SLACS_AffectedActorsList`（`04000D64`，39,603）、`SLACS_MegaCumRaces`（`04004E66`，39,658） |
| VMAD | Main 绑定 SexLab、SpellHandler、MCM、Player、两张 FormList；MCM 绑定 Main/SpellHandler/SexLab；SpellHandler 绑定 26 个 Spell 和 26 个 MGEF，并含 `SLACS_PlayerAlias` 绑定 |
| 可疑旧绑定 | 被覆盖的 `SexLabCumOralAnalEffect`（`010434D2`，2,296）与 `SexLabVaginal2Oral2Effect`（`0108D646`，6,399）虽挂脚本，却没有 Main/SexLab 属性值；一个旧 MGEF 还保留新脚本已不存在的 `PlayerRef` 属性。不要原样复制 VMAD |

以上 FormID 的高字节是该文件的 master 索引，不是运行时固定 load order。

**事实：**第二条效果入口有实际 ESP 连接链。P+ [SexLab.esm](<E:/source/repos/sexlab P+/SexLab.esm>) 中 `SexLabQuestFramework`（原始 `05000D62`，偏移 131,012）的 `sslActorLibrary` 属性将 `CumVaginalSpell`、`CumOralSpell`、`CumAnalSpell` 分别绑定至 `05041478`、`050434D5`、`050434D7`；其 EFID 分别引用 `05041477`、`050434D0`、`050434D1`，正是 SLACS ESP 覆盖并挂脚本的 MGEF。源码也直接给目标添加这些法术。[sslActorLibrary.psc](<E:/source/repos/sexlab P+/Source/Scripts/sslActorLibrary.psc:26>)

**事实：**包内共有 291 个 DDS：Anal/Breast/Oral/Vaginal 各 60，Handjob 8，Footjob 16，另有 clothes 27。主四组普通纹理各有 12 份 diffuse/normal/specular；`m_` 各有 12 份 diffuse/normal、没有 specular。手、脚、腿的普通/`m_` 各有 4 份 diffuse，没有 normal/specular。包内未发现 `Textures/slacs2_ube` 目录。这里指该目录的资源盘点，不能排除用户其他模组另行提供 UBE 纹理。路径选择逻辑见 [SLACS_Main.psc](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:1194>)；可选 normal/specular 逻辑见 [SLACS_Main.psc](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:783>)。

**新版边界：**保留原 Main QUST EDID `SLACS_Main_Q`（原文件 FormID `04000D62`，local ID 为 `0x000D62`）及其 VMAD 挂载脚本名 `SLACS_Main`，把 `SLACS_Main extends Quest` 改成薄 P+ Event→native 桥。查找使用 local ID 与原 ESP 文件名，加载序号由游戏解析。清除 Main VMAD 中旧 MCM、SpellHandler、SexLab、Player 和 FormList 属性，移除废弃 Quest、别名及其脚本绑定。只重新编译并交付 `SLACS_Main.pex`，三个 `Global Native` 声明放在同一脚本类中。S7 接入 26 种组合效果及相应 Spell-MGEF-EFSH 资源，移除 33 个 SexLab MGEF 覆盖。目前可读业务入口没有使用 `clothes` 的证据。

## 3. 旧版核心业务规则

### 3.1 场景和受影响者范围

**事实：**新版主入口不是处理所有场景。`HookAnimationStart` 取全部参与者，只有包含玩家或当前随从时才给所有参与者注册 `SLACS` actor tracking，并把 tid 存入全局 `SLACS_ActiveThreads`。重要角色的定义是玩家，或 PO3 返回的当前随从。[场景筛选](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:240>)；[重要角色定义](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:947>)

**事实：**`SLACS_Orgasm(Form actor, int tid)` 首先验证 tid 在列表中，然后按线程内性别分类处理来源：Male=0、Female=1、Futa=2、CrtMale=3、CrtFemale=4。0/2/3 是通常来源；1 仅在 `AllowFemaleFluid=true` 时处理；4 没有来源分支。默认还限制女性类受方 1/2/4 必须是玩家或随从；男性类受方 0/3 不受这一额外限制。[来源筛选](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:276>)；[受方筛选](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:656>)

| 来源 | 受方 | 主入口的部位规则 |
|---|---|---|
| 0 / 2 / 3 | 1 / 2 / 4 | 按交互/标签加 Vaginal、Oral、Anal、Breast、Handjob、Footjob |
| 0 / 2 / 3 | 0 / 3 | 同上，但禁止 Vaginal 和 Breast |
| 1 且允许女性体液 | 1 / 2 / 4 | Oral→Face；Tribadism/Rubbing 或 Vaginal→Vaginal+Breast；Handjob/Footjob 各自处理；没有 Anal |
| 1 | 0 / 3 | 不处理 |
| 4 | 任意 | 主来源分支不处理 |

证据：[ProcessComplexOrgasm](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:686>)。自己的 orgasm 不给自己加层（`target != sourceActor`）。

**事实：**场景标签用于初始推断；若 interaction 已注册，Vaginal/Anal/Oral 改用 `HasInteractionType(type, target, none)`；Oral 含 Oral/Deepthroat/Skullfuck。Breast/Hand/Foot/Trib 仍用场景标签。`HasSceneTag` 可描述场景的任意阶段，`HasStageTag` 才是当前阶段。[旧调用](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:638>)；[P+ 标签范围](<E:/source/repos/sexlab P+/Source/Scripts/SexLabThread.psc:68>)

**推断：**`partner=none` 会把“目标与任何人有某交互”归因到这一次 orgasm 的来源，在多人场景可加错层。旧注释“Interaction 暂不覆盖 Hand/Foot”也不符合当前提供的 P+，其 enum 已有 FootJob=8、HandJob=9、Facial=11、LickingShaft=7。[方向/None 契约与枚举](<E:/source/repos/sexlab P+/Source/Scripts/SexLabThread.psc:129>)

**建议：**新规则以明确的 `(source,target)` 有向交互为优先；每种 CTYPE 先按 API 的 position/partner 方向转换，再映射区域。当前阶段标签仅用于缺交互数据的回退。保留 Breast/Trib 之类没有同义精确交互的规则为显式回退策略，不把整场景所有标签自动广播给所有目标。`Futa`/TreatAs 与 overlay 使用的基础性别是不同数据，不能互相替代。

### 3.2 区域、纹理、槽位与叠层

**事实：**一次成功的 AddCum 对每个勾选区域至多新增一条记录；Footjob 同时尝试 Feet 和 Body/legs，任一成功即返回成功。记录是 `StorageUtil` 中的纹理编号列表，不保存槽号、来源、创建时间或 texture variant。[AddCum](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:559>)；[记录键和值](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:722>)

| 逻辑区域 / RaceMenu 区域 | 纹理前缀、编号池 | 默认槽位（起始配置均为 7） | 槽数 | 逻辑层上限 |
|---|---|---:|---:|---:|
| Vaginal / Body | `body1..12` | 7–12 | 6 | 5 |
| Anal / Body | `body1..12` | 13–18 | 6 | 5 |
| Breast / Body | `chest1..12` | 19–24 | 6 | 5 |
| Footjob / Body | `legs1..4` | 25–26 | 2 | 5 |
| Oral / Face | `head1..12` | 7–12 | 6 | 5 |
| Handjob / Hands | `hands1..4` | 7–8 | 2 | 5 |
| Footjob / Feet | `feet1..4` | 7–8 | 2 | 5 |

槽位事实：[GetMinSlot/GetMaxSlot](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:1070>)；纹理编号数量：[AddCum](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:570>)；固定逻辑上限：[MaxLayersPerPart](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:45>)。

**事实：**纹理编号优先从未用编号中随机选择，耗尽时复用已用编号；达到 5 层直接拒绝新增。槽位先找没有 override/default.dds 的位置，其次随机替换本插件已占槽位，最后在整个区域随机选择一个槽位，即使它属于其他模组。它先把编号加入逻辑列表，再写 diffuse/alpha 和存在的 normal/specular。[编号与写入](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:725>)；[分配算法](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:1031>)

**事实：**目标的 UBE 种族决定根目录 `slacs2_ube`/`slacs2`；源种族在 Mega FormList 中或全局 Mega 开关打开，则选择 `m_` 文件。UBE 通过 `UBE_AllRace.esp` 的 18 个硬编码 FormID 缓存判断。[UBE 缓存](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:1144>)；[路径](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:1194>)；[Mega 路径选择](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:767>)

**推断与实施处理：**两槽区域最多显示两张图，但可能记录五层；旧代码复用 slot 时可能残留上一个 variant 的可选贴图。新实现将业务层数与可见槽分开，表现缓存记录精确区域、slot、路径/variant 与归属。槽位有限时优先投影较新的逻辑层，只使用本插件自有槽或实际空槽；逻辑层保留，资源不再提供的自有材质键主动清除。

### 3.3 法术状态

**事实：**新版以 Vaginal/Anal/Oral 三个逻辑列表长度计算 0/1/2 级；先检查 `LayersForLevel2`，再检查 Level1，默认阈值 3 和 1。Breast/Hands/Feet/legs 不直接参与组合。组合索引为 `(anal * 9) + (vaginal * 3) + oral`，0 为空，其余 1..26 查表。然后维护 JFormMap(actor→[oral,vaginal,anal])、当前法术和 dirty mask。[层数映射](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:425>)；[SpellHandler 状态入口](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:487>)

**事实：**每次 AddCum 已立即同步法术，AnimationEnd 还会再同步；不能按旧注释声称“只在动画结束时才应用法术”。SpellHandler 队列逐一验证 StorageUtil 当前法术、HasSpell、HasMagicEffect，必要时移除/重加；未加载目标本轮跳过，下一次全量轮询重入队。[Add 后同步](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:625>)；[AnimationEnd](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:305>)；[修复逻辑](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:383>)；[未加载分支](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:319>)

**建议：**保留“三部位逻辑层数导出 26 种效果”的业务意图时，使用唯一的 actor 逻辑状态推导期望效果；即使没有足够可见槽，法术等级也不反向读取渲染结果。视觉与效果同步都是幂等 reconcile。关闭效果应立即撤销自有 Spell 并禁止重新添加，而不是仅跳过状态更新。阈值需满足 `1 <= level1 <= level2 <= 有效层上限`；旧 MCM 允许 Level2 超过 5，因此旧配置并不总能达到二级。[旧阈值 UI](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:266>)；[关闭后直接返回](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:487>)

## 4. 时间、衰减与清洗的真实语义

| 量 / 触发 | 默认值和单位 | 实际行为与证据 |
|---|---|---|
| `DecayInterval` | 1.0 游戏小时 | `RegisterForSingleUpdateGameTime`；每次回调每个非空区域删除一条，再注册下一次。[配置](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:37>)；[回调](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:175>) |
| 雨水 | 布尔开关，默认 true | 回调当下全局天气 classification 恰好等于 2，且 actor 非室内且仍有层，则同一次再删一轮。没有累计淋雨时长，没有局部遮蔽判定。[ProcessDecay](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:339>) |
| 游泳轮询 | 5.0 秒 | 只扫描 affected actors，且只检查 3D 已加载者；IsSwimming 即全清。仍有 affected actors 才继续轮询。[OnUpdate](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:151>) |
| `CumTimer` | 120.0 秒 | 旧 AME 首次 5 秒后检查，此后 10 秒一轮；`GetTimeElapsed() > Main.CumTimer` 时全清/Dispel。它不是每层独立 TTL，且在 DisableDecay 时仍可生效。[AME](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_CumEffectHandler.psc:97>)；[MCM 单位](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:113>) |
| 快速旅行 | 参数单位游戏小时；Wait 1.5 秒 | 注销全部事件，等待后重注册/GC；只有 travel>1.0h 才额外运行一次 ProcessDecay，不按经过小时数或配置间隔补算。[快速旅行](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:220>) |
| GC | 冷却 300 现实秒 | cell fully loaded 且 attached 时可能触发；保留玩家、processing level 0 actors 与当前随从，其余清数据。[触发](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:210>)；[GC](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:868>) |
| Spell 验证 | 30 秒全量、0.5 秒消费一人、cell 后 2 秒 | DbFormTimer 三种 ID；消除同一 actor 重复入队使用线性查找，队头移除再处理。[参数](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:70>)；[回调](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:227>) |

**事实：**衰减选择列表最后一项，即后加的先删（LIFO），不是最旧层先消失；没有记录层的年龄。即使 RaceMenu 实际移除失败也照样删除列表项。[DecayBodyPart](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:407>)

**事实：**`RemoveOverlayByTextureID` 从 `int j = maxSlot` 开始，循环条件 `while j <= maxSlot`，最后 `j += 1`，因此只检查最大槽，不遍历整个范围。匹配仅按纹理文件名子串，不包含精确 slot/路径/variant。通常会出现逻辑层数减少而图仍在的情况。[确切代码](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:796>)

**事实：**名为 `FadeAndRemoveOverlay` 的函数直接移除 override、改回 default.dds/alpha=1，没有随时间修改 alpha，也没有读取 `FadeOutEnabled`。[清除实现](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:980>)。UI 的 Fade-Out 选项不能作为已实现动画淡出的证据。

| 清洗入口 | 旧行为 |
|---|---|
| 全清 `FX_ALL=-1` | 清七个逻辑列表、已加载时清七组自有路径图层、清 dirty 状态并移出 affected actors。[ClearCumAll](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:832>) |
| 部分清 `0/1/2` | Vaginal=0 同时清 Breast；Anal=1 同时清 Breast；Oral=2 只清 Face 并刷新脸部。并重新导出法术。[ClearCum](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:497>) |
| unloaded 且非重要角色 | 忽略 requested type，直接 ClearCumDataOnly；该函数漏清 Footjob/Body，并不调用 SpellHandler 清除状态。[特殊分支](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:506>)；[漏项](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:856>) |
| BiS / DnB / 游泳 | 一律 FX_ALL；BiS 的 timeToClean、interval、usedSoap 参数在此不参与算法。[事件处理](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:189>) |
| AME Finish / 到期 | AME Active 标志防止同一实例重复 Cleanup；调用 SexLab.RemoveCumFx(actor,-1)，再自发 `Sexlab_ClearCum(Form,int=-1)`。不同实例仍各有自己的生命周期。[Cleanup](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_CumEffectHandler.psc:122>) |

**已确认的时间规则：**自然衰减按经过的游戏时间补算，包含睡眠、等待和快速旅行；雨水加速只计算已观察到暴露的时段，按这些区间经过的游戏时间累计。不能把落地时天气当作整个缺失时段的天气。

**实施衰减算法：**每 actor/region 保存时间锚点和小数衰减预算。普通衰减按 `budget += elapsedGameHours / DecayInterval` 累计；一次删除对应整数层数，沿用旧 LIFO 顺序。雨水额外预算只累计已观测为室外雨中区间的游戏时间；读档和快速旅行只补普通衰减。新增加层前先结算旧状态；区域从空变非空时建立新的时间基点，清空区域同时清预算。这样 6 小时旅行与六次 1 小时结算得到相同普通衰减结果。具体 DTO 和单位见 [接口契约](../INTERFACES.zh-CN.md)。

**实施时钟：**自然衰减和雨水预算使用游戏小时；整体清除默认 120 活动游戏秒，成功加层重置 actor 的期限，可关闭，暂停/菜单时停止累计。单调时钟负责唤醒、查询超时和执行预算。游泳沿用采样确认后全清；效果结束不决定业务层寿命。三类时钟在暂停、等待、睡觉和读档中的表现由 S6 验收。

## 5. SexLab P+ 事件真实契约

此处名称按发送代码原样记录；下划线差异是实质差异。提供的 P+ 目录包含 Papyrus、DLL、ESP/ESM 和资源，没有发现可用的 C++ API 头文件/源码。不能把 `SexLabUtil.dll` 的存在当作稳定 native C++ API 的证明。

| 事件 | 实际推送参数（顺序） | 条件 / 语义 / 一手依据 |
|---|---|---|
| `HookAnimationStart` / `HookAnimationEnd`，及其他 `Hook<Event>` | `(int tid, bool hasPlayer)` | SetupThreadEvent 创建 `Hook`+事件名，PushInt 后 PushBool；另有旧 `SendModEvent(HookEvent,tid)`，不是同一 payload。[发送实现](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:2265>)；[公开契约](<E:/source/repos/sexlab P+/Source/Scripts/SexLabFramework.psc:149>) |
| `<callback>_Orgasm`，如 `SLACS_Orgasm` | `(Form actor, int tid)` | 需要 TrackActor/TrackFaction；Added/Start/End 也采用这一形式。[tracking 契约](<E:/source/repos/sexlab P+/Source/Scripts/SexLabFramework.psc:593>)；[实际 Push](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadLibrary.psc:168>) |
| `SexLabOrgasm` | `(Form sourceActor, int fullEnjoyment, int orgasmCount)` | `_OrgasmCount += 1` 后发出；第三参不是 tid。[计数与发送](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:899>) |
| `SexlabOrgasmSeparate` | `(Form sourceActor, int tid)` | 同一次 orgasm 的全局通知，不要求 SLACS actor tracking；紧接 SexLabOrgasm、先于 tracked orgasm。[发送点](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:930>) |
| `SexLabApplyCum` | `(Form targetActor, int type)` | ActorLib 添加本身的 overlay/法术后发送；无 source/tid。type 为 -1/0/1/2。[实现](<E:/source/repos/sexlab P+/Source/Scripts/sslActorLibrary.psc:26>) |
| `SexLabApplyCumFX` | `(Form targetActor, Form sourceActor, int type)` | Config.UseCum 为真，target 已加载且 cell attached，确定一个优先部位后，先调用 ActorLib.AddCumFx，再发送；优先 Vaginal/Grinding>Anal>Oral。[实现](<E:/source/repos/sexlab P+/Source/Scripts/sslThreadModel.psc:2192>) |
| `SexLabClearCum` | `(Form targetActor, int type)` | 移除 ActorLib 自己的 overlay 后发送；清 -1 会递归清 0/1/2，有该类已应用索引才发送，不保证一次 -1 通知。[实现](<E:/source/repos/sexlab P+/Source/Scripts/sslActorLibrary.psc:53>) |

**事实：**新版旧 Main 仍注册 `Sexlab_AddCum` 与 `Sexlab_ClearCum`；给出的 P+ 可读源码不发送这两个带下划线的名称。`Sexlab_ClearCum` 的发送者可在 SLACS 自己的 AME 中找到；`Sexlab_AddCum` 在这些源码中没有发送者。旧 `AddCum(Form, bool,bool,bool,bool,bool,bool,Actor=None)` 也不能直接作为 P+ `(Form,int)` Add 事件的参数契约。[旧注册与签名](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:105>)；[AddCum 签名](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:559>)；[自发清除](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_CumEffectHandler.psc:135>)

**事实：**同一次 P+ orgasm 的源码顺序为：男性类来源先 `ApplyCumFX(source)` → `SexLabOrgasm` → `SexlabOrgasmSeparate` → tracked orgasm。女性类 1/4 跳过默认 ApplyCumFX，但仍发 orgasm 事件。[顺序](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:921>)。ModEvent 接收是异步的，这不是接收端全部回调完成顺序的保证。[异步 hooks 说明](<E:/source/repos/sexlab P+/Source/Scripts/SexLabFramework.psc:149>)

### 5.1 双路径与去重结论

**事实 + 推断：**不能声称当前包的 `Sexlab_AddCum` ModEvent 与 orgasm 必然双触发，因为 P+ 根本没有该名称的可读发送者。已经证实连接的潜在双加层链是：

1. P+ orgasm→ApplyCumFX→AddCumFx→SexLab 原法术→SLACS ESP 覆盖 MGEF→SLACS_CumEffectHandler.OnEffectStart→Main.AddCum。
2. 同一次 orgasm→tracked `SLACS_Orgasm`→ProcessComplexOrgasm→Main.AddCum。

第一条还使用另一套规则：场景标签；Oral 强制加 Breast；source 选择参与者中最后一个 SexLab.GetSex()==0 的 actor，不处理 futa/crtmale 为来源。因而它不仅可能重复，还可能覆盖新主入口的来源、范围、交互规则。[AME 全部规则](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_CumEffectHandler.psc:19>)。实际一次效果是否因引擎已有法术而再次启动、事件落地先后及重复层数仍需运行时确认。

**单一业务入口：**对本地这版 P+，选 `SexlabOrgasmSeparate(Form,int tid)` 为 canonical orgasm 源。采用 [Devious Curses 的接收结构](devious-curses-events.md)：复用 `SLACS_Main_Q` 和挂载的 `SLACS_Main` 薄脚本，以 `RegisterForModEvent → Papyrus Event → Global Native → C++` 转发参数；C++ 入队后再通过 P+ 既有接口取得交互语义。该脚本同时接收 typed HookAnimationStart/End，只管理过程生命周期；不再逐 actor tracking，也不同时消费 SexLabOrgasm、ApplyCum/ApplyCumFX 加层。桥不处理玩家/随从筛选和交互计算。接收契约见 [事件研究](pplus-native-events.md)，实际执行阶段在 1.6.1170 验证。

**过程与事务：**P+ 的 tid 标识一次独立动画过程，按其原有过程模型处理；不再另外分配一套场景 generation 作为上游身份。每次 canonical 原生入口分配递增 sequence，下游排队/重试保留 `(runtimeEpoch, tid, ingressSequence)`，同一内部事务重放才能幂等忽略。runtimeEpoch 只负责读档/new game 作废已采集任务；成员与有向交互快照服务于业务判定，不替代 P+ 的 tid。

**同一过程可多次触发：**tid 是动画过程 ID，不是一次 orgasm 的 ID；同一 `(actor,tid)` 的两个合法通知应产生两个事务。内部 sequence 只处理重试幂等。禁止用“同 Actor 100ms/500ms 内忽略”吞合法多次事件；不混读 `SexLabOrgasm` 与 Separate 两条异步流拼接业务身份。[count / tid 分离的原始发送点](<E:/source/repos/sexlab P+/Source/Scripts/sslActorAlias.psc:925>)

## 6. 原生替代与资源边界

| 保留或替换项 | 本地证据与建议边界 |
|---|---|
| SkyUI MCM PSC + Quest/alias | 移除 MCM Quest、脚本和相关 VMAD/PEX 绑定；以 Menu Framework 3 的中文原生控件替代配置与测试操作，C++ 管理配置文件。[旧页面/初始化](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:66>)；[新菜单研究](menu-framework.md) |
| P+ 事件接收 | 复用 `SLACS_Main_Q` 与 VMAD 脚本名 `SLACS_Main`，将 `SLACS_Main extends Quest` 改成薄 Event 桥；同一脚本类声明三个 Global Native，由 DLL 在 `SLACS_Main` 类注册。订阅 Separate 及 typed HookAnimationStart/End，原样转发参数到 C++ 队列。仅交付一份重编译的 `SLACS_Main.pex`，清除旧 Main 属性绑定，不建独立 Quest 或第二份声明 PEX。P+ 查询继续使用既有 VM API；跨等待点保存值与受管理身份。[GetThread](<E:/source/repos/sexlab P+/Source/Scripts/SexLabFramework.psc:120>)；[事件接入研究](pplus-native-events.md) |
| RaceMenu C++ 适配器 | 已确认直接使用 RaceMenu C++ API，替换旧 NiOverride Papyrus 写入；SlaveTatsNG 仅可作为接口用法/共存行为的参考，不是运行时依赖。仍需验证与 SlaveTatsNG、其他 RaceMenu overlay 消费者并存时的槽位归属和重建行为 |
| 清洗集成 | DnB 本包是覆盖整个 Dirty_BathingQuest 后注入 `DnB_CleanActorDirt(Form)`；不能称为上游原生稳定事件。新版不复制该旧脚本覆盖；可选原生适配需先找到可用上游信号。BiS 目前只能证明旧接收签名 `(Form,float,float,bool)` / `(Form,Form,bool)`，未提供实际发送方，标为待核查。[DnB 注入](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/Dirty_BathingQuest.psc:140>)；[BiS 接收](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:196>) |
| 生命周期通知 | SKSE 序列化/消息管理 epoch、清任务和恢复；新游戏/成功读档后，C++ 通过插件文件名和 local ID `0x000D62` 查找原 Main Quest `SLACS_Main_Q`，再调用 `SLACS_Main.RegisterEvents`。该 local ID 是原 ESP 记录 FormID `04000D62` 的低 24 位，未实机验证；不使用旧 load order 字节 `04`。不保留旧 alias/维护 PEX |
| Spell/MGEF/EFSH | 保留所需视觉资产或重建较小集合，由 native 状态驱动期望效果；去掉“效果开始再加业务层 / 结束全清业务状态”的回授。[旧 AME 回授](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_CumEffectHandler.psc:74>) |
| StorageUtil / JContainers / DbFormTimer / PO3 | 全部移除；C++ 容器、权威状态、SKSE 序列化、共享期限队列与引擎 actor 查询直接承担职责。不导出旧 dirty keys，不维持旧消费者或旧数据兼容。[旧 dirty 导出](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:453>)；[旧队列](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_SpellHandler.psc:227>) |

**事实：**MCM opacity 会遍历配置范围内全部 Body/Face/Hands/Feet 槽位并写 alpha，不检查是否属于 SLACS；它可改变其他模组的图层。StorageUtil 显式保存的是测试层数、快捷键、不透明度；其他自动属性依赖脚本保存状态，不能说整个 MCM 都已保存为外部配置。[opacity](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:558>)；[显式保存](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:623>)

## 7. 不应复制的缺陷与安全边界

| 确定问题 / 风险 | 新实现必须体现的约束 |
|---|---|
| 纹理 ID 代替图层身份、只扫描末槽、无论移除成败都减计数 | 精确拥有的 Layer/slot/path；逻辑状态唯一权威；渲染结果作为可修复投影；测试部分移除与 slot 复用 |
| 随机覆盖他人槽位；MCM 全范围 alpha | 所有写、清、淡出仅针对明确自有槽位；分配失败可见且可诊断 |
| 三套状态（纹理编号、dirty keys、Spell JMap）可分叉 | 单一 actor 业务模型；派生状态幂等应用；卸载暂不可应用时记录 dirty，不擦掉业务事实 |
| `ThreadLock` 是 bool；加图和列表写入发生在其之前；等待最多 50×0.1s 后仍强占 | 单线程状态归约；异步回调仅投递值消息；不把旧 bool 翻译为 C++ 的同步方案。[旧锁位置](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:603>) |
| fast travel 注销全部事件并 Wait 1.5s | 生命周期内维持入口；用 epoch/generation 取消旧异步任务，避免时间窗口漏事件 |
| end 时重新读 thread 的当前 positions，忽略成员变化 | 以 P+ tid 关联过程，核对每次请求的成员/交互快照；收尾消费该过程已采集状态，避免重读可能复用的 slot。[旧收尾](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:305>) |
| 未加载非重要者直接丢状态；ClearCumDataOnly 漏一组/未清法术 registry | 将“actor 暂未加载”与“actor 已删除”分开；所有七组和派生效果共享一个清空路径 |
| EffectFinish 决定全清，可能与换法术/引擎终止效果相互影响 | 业务层寿命由自身状态控制；渲染/Spell 适配器结束不得自行清除业务状态 |

**调度选择：**首版使用共享期限队列和显式请求状态。任务保存 actor 身份、epoch 和修订号，执行时在选定的游戏阶段重新解析 handle/3D。协程作为以后组织多阶段流程的工具，沿用同一套所有权与取消规则。实现接口见 [接口契约](../INTERFACES.zh-CN.md)。

## 8. 新档验收建议与待验证项

建议把以下可观察规则写入后续规格，区分保留业务意图与修复旧缺陷：

1. 一个 canonical orgasm 回调得到一个事务；同一来源连续两个合法回调得到两个事务；同一内部事务重试不重复加层。男性默认 FX 开/关都不改变 SLACS 自身事务数量。
2. 三人场景只根据本次 source 与 target 的有向交互加层；标签回退只在明确缺少交互数据时执行；女性来源、futas、creature 的分类独立验证。
3. 七组逻辑层、可见槽映射与法术等级都可核对；法术等级从逻辑层派生，可见槽是可修复投影。在两槽区域堆第三层时验证选定的容量策略，缺槽不能误改逻辑层。其他模组占满槽位时不受影响。分别验证没有安装 SlaveTatsNG，以及与 SlaveTatsNG/其他 overlay 模组共存的场景。普通与 Mega 互换不残留缺失 map 的旧 override。
4. 一次 6h catch-up 与六次 1h 普通衰减等价；新加层不背负空闲时段衰减；不根据一次当前雨天追溯未观测的整个旅行。
5. Swim/BiS/DnB/显式全清覆盖全部七组；部分清规则明确；关闭 spells 立即撤销自有效果；读档/切 cell 不能重启已失效的旧任务。
6. 新版面向新档，不实现旧 StorageUtil/JContainers/PEX state 的导入器，不维持旧 dirty keys 或原 MGEF 覆盖。保留 `SLACS_Main_Q` 和 `SLACS_Main` 名称，但把 Main 改为薄桥并清除原属性绑定；移除不再使用的 MCM、SpellHandler Quest/alias、旧 VMAD 属性及其脚本绑定，不留下空壳。只重新编译并交付一份 `SLACS_Main.pex`，在同一脚本类中声明三个 Global Native；验证重订阅和参数交付。插件不调用或依赖旧脚本工具，P+ 等上游自身运行要求另计。菜单、提示和操作结果全部中文。

尚需验证：PEX 与 PSC 的精确对应；提供的 P+ 版本与用户最终安装版本是否一致；P+ 异步回调到达时当前线程对象/交互数据是否仍有效；3D/Face overlay 重建的最佳触发；实际 BiS 发送方与 DnB 上游兼容方式；UBE 纹理来源；26 个 EFSH 的预期视觉及其在目标游戏版本的表现；计时在暂停/菜单/等待/睡觉/读档中的推进规则。本文没有把任何一项标记为游戏测试通过。
