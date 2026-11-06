# SESSION-02：S3/S4 实现与验证记录

更新：2026-11-06。工作目录：`E:/source/repos/SLACS2 NG`。本次实现 S3（PPlusAdapter）和 S4（Core / OverlayAdapter / BodyVaginal），接通 Runtime 调用链，并按独立对抗审核（DS V4.1 Flash）修复并发、覆盖清除、读档回收、槽位不足和 3D 重建问题；保存、共享期限和按时间结算留给 S5/S6。

## 1. 本次结论

已完成：

- `PPlusAdapter`：从 `NativeOrgasm` 发起 P+ VM 查询，按 stage 采集 thread、参与者、性别、场景/阶段标签和有向交互，生成 `ResolvedOrgasm` 或带失败码的 `PPlusFailure`。
- `Core`：七区域的统一 `CoreState`，`AddLayers`、`ClearRegions`、`SettingsChanged`、`replaceState`，五层上限、同通知每 actor/区域至多一层、稳定纹理编号和层记录。
- `OverlayAdapter`：RaceMenu `Overlay`/`Override` v2 握手，BodyVaginal profile，7..12 自有槽位，差量更新、清除、失效重放；`AddOverlays(refr, true)` 的显式 immediate 语义已按 0.4.20 实现处理。
- 审核修复：P+ 回调按 `dispatchToken/step` 去重，`waiting` 阻止同一 stage 重复派发，`fail/terminal` 在状态锁内按序写入；RaceMenu 清除改为“默认 diffuse + alpha 1.0 + 清空 normal/specular 材质”，读档后按 `textures\slacs2\Vaginal\` 前缀回收旧槽；槽位不足改为新层优先的部分应用；读档走 `Core::replaceState` 并重放投影；3D 首次就绪自动失效重放。
- Runtime：`NativeOrgasm → begin → poll → PPlusCompleted → 同 tid 序号排序 → AddLayers → ChangeSet → ActorLease → reconcile` 全链路；`ClearActor`、`TestAdd`、`TestClear` 走 Core→ChangeSet→Overlay，`RefreshActor` 只做 Core 当前目标的投影重放。
- BodyVaginal 的 60 个 DDS 已放入 `package/Data/Textures/slacs2/Vaginal`；`package/README.zh-CN.md` 记录了 RaceMenu 槽位和 `bPlayerOnly` 条件。
- S1、S4 自动检查通过；ESP 静态核对通过；完整构建和打包通过。

尚未完成：

- 1170 游戏内 S3/S4 验收**未执行**。本次环境仍是 SESSION-01 记录的物理环境：P+ 为 2.16.0.0、`SexLab.esm` 为 347,818 字节，不满足 2.17NG 依赖门；因此即使启动到 DataLoaded，`Runtime::checkDependencies` 也会拒绝激活。
- RaceMenu 当前 `skee64.ini` 的 Body 槽位数为 6、`bPlayerOnly=1`；BodyVaginal 的 7..12 槽位会返回 `NoOwnedCapacity`，逻辑层保留并每 500 ms 重试。这是用户配置项，本包不覆盖 RaceMenu 的 ini。
- 其余六个区域只有数据模型和 ChangeSet 投影，显示资源/效果按计划留给 S7。
- Clear/Refresh/TestAdd 命令已经有 Runtime 处理，但还没有菜单或调试入口；S8 菜单将复用这些命令。
- 真实 3D 重建、其他模组占用同一节点、P+ 查询乱序在游戏内的表现仍待实测。

## 2. S3 实现状态

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| 查询请求/结果/失败码 | 完成 | `src/integration/PPlusAdapter.h` |
| VM staged 查询 | 完成 | `src/integration/PPlusAdapter.cpp` |
| 场景与来源筛选 | 完成 | `PPlusAdapter::finalize` |
| 有向交互与区域映射 | 完成 | `PPlusAdapter::finalize` |
| 超时/取消/失败终结 | 完成 | `PPlusAdapter::poll`、`handleResult`、`complete` |
| 入口容量预留 | 完成 | `src/runtime/Ingress.h/.cpp` 的 `reserve/release/postReserved` |
| Runtime 发起与排序 | 完成 | `src/runtime/Runtime.cpp` 的 `processNativeOrgasm`、`processPPlusCompleted` |

### 2.1 查询流程

`Runtime::processNativeOrgasm` 只在命令进入 pump、且 tid 属于当前已登记场景时工作：

1. 校验 epoch、FormID、`sessionReady_`、`sceneTids_[tid]`。
2. 通过 `ActorRegistry::bind` 得到来源 ActorId。
3. 构造接口契约中的 `OrgasmQuery{requestId, sourceFormId, source, tid, sequence, epoch}`。
4. 调用 `PPlusAdapter::begin`。`begin` 同步预留一个入口终态容量；epoch 不匹配时返回 `RejectedNotRunning`；终态容量预留失败时返回 `RejectedCapacity`，不会产生异步完成。
5. 被接受的请求按 stage 派发 VM 方法，回调只把 FormID、整数、布尔和数组复制到 adapter 自有的 `shared_ptr<RequestState>`；不保存 Actor/VM 对象指针。
6. `Runtime::pump` 在游戏执行阶段调用 `PPlusAdapter::poll`，adapter 完成最终身份绑定并投递一个 `PPlusCompleted`。

查询 stage：

| 顺序 | 调用 | 复制内容 |
| --- | --- | --- |
| 1 | `SexLabFramework.GetThread(tid)` | thread VM 对象句柄 |
| 2 | `SexLabThread.GetPositions()` | 参与者 FormID 数组 |
| 3 | `SexLabThread.GetActorSex(actor)` | 每个位置的性别码 |
| 4 | `SexLabThread.IsInteractionRegistered()` | 是否有有向交互数据 |
| 5 | `SexLabThread.GetInteractionTypes(target, source)` | 每对 `target→source` 的类型数组 |
| 6 | `SexLabThread.HasStageTag(tag)` | 11 个当前阶段标签 |

查询结果是**本次异步采集的快照**：最终 `ResolvedOrgasm` 不随后续 P+ 场景变化重新查询；同一 actor/tid 的下一次合法通知会重新开始一套查询。

### 2.2 场景、来源和目标规则

- 场景范围：`GetPositions()` 中必须存在玩家或 `IsPlayerTeammate()` 的 actor；否则成功返回空 targets，不进入 Core 加层。
- 来源类别：`GetActorSex` 的 0/1/2/3 分别对应 Male/Female/Futa/CreatureMale；4（CreatureFemale）只有在 `enabledSourceClasses[4]=true` 时才按 female 来源处理。`enabledSourceClasses` 默认允许 0..3、禁止 4/未知。
- Female 来源（类别 1，或已启用的 4）还要求 `allowFemaleFluid=true`；否则空结果。
- 目标类别：1/2/4（Female/Futa/CreatureFemale）在 `femaleTargetsRequirePlayerOrFollower=true` 时只接受玩家或当前 engine teammate；0/3（Male/CreatureMale）不受此限制。
- 自身永远不会加层：`Core::AddLayers` 和 adapter 两处都跳过 source==target。
- 有向交互优先：对每个非来源目标查询 `GetInteractionTypes(target, source)`。Vaginal/Anal 使用“position 被 partner 插入”的方向，Oral 使用“position 对 partner 进行口交”的方向。交互注册且该 pair 的类型数组非空时，1 Vaginal、2 Anal、3/5/6/7/11 Oral/Face、4 Grinding、8 FootJob、9 HandJob 只由有向数据决定，不再用整场景 stage 标签 OR；类型数组为空才回退阶段标签。Breast 没有对应的有向 CTYPE，有向数据存在时不广播 Breast。
- 无有向交互数组时，才回退到当前阶段标签：Vaginal/Pussy、Oral/Blowjob、Anal、Handjob、Footjob、Boobjob/Breast、Tribadism/Rubbing。有向数组存在时，Breast/Hand/Foot 也不从整场景标签广播。
- 区域映射：Vaginal→BodyVaginal；Anal→BodyAnal；Oral→Face；Breast→BodyBreast；Handjob→Hands；Footjob→Feet 和 BodyLegs（同一次通知同一目标各一层）。
- Female 来源：Oral→Face；Tribadism/Rubbing 或 Vaginal→BodyVaginal+BodyBreast；Hand/Foot 按标签；不产生 Anal。
- 一次通知中每个目标/区域最多一条 `InteractionTarget.regions` 项；Core 再用 `(actor, region)` 去重。
- Mega 变体：`settings.megaCumForAll` 或 `SLACS_MegaCumRaces`（本包 ESP local `0x4E66`）包含来源 race。Mega 只影响纹理前缀，不改变区域。
- 任一目标在最终绑定时已失效 → `ParticipantUnavailable`，该请求整体失败，不猜测部分结果。

### 2.3 超时、取消、失败和排序

- 固定查询超时：`5` steady seconds，从 `begin` 的 `started` 起算。`poll`、VM 回调和 `finalize` 都检查；超过后迟到的回调被丢弃，结果为 `TimedOut`。
- `cancel(requestId)`、`cancelTid(tid)`、`cancelEpoch(epoch)` 只设置取消标志，不跨线程销毁状态；`poll` 产生 `Cancelled`。场景结束、读档、新游戏会调用 `cancelTid`/`cancelEpoch`。End 同时丢弃该 tid 的排序状态，已在 End 前处理的完成仍有效，End 后到达的完成按未知请求拒绝，不会把旧场景结果写入新场景；重复的 Scene Start（tid 复用）先取消旧 tid 并清空排序状态。
- 失败码：`Cancelled`、`TimedOut`、`VmDispatchFailed`、`InvalidThread`、`SourceUnavailable`、`ParticipantUnavailable`、`InvalidQueryData`。
- 并发：每个 `RequestState` 有递归互斥锁；VM 回调先比较 `dispatchToken`，过期/重复回调直接丢弃。`poll` 在 `waiting=true` 时不再派发同一 stage，避免异步回调重复写入 `sexes/tags/interactions` 游标。`fail` 在锁内先写失败字段再 release-store `terminal`，`complete` 在锁内读取终态，避免“terminal 已置位但失败码未写”的竞态。
- 回调通过 `shared_ptr<CallbackGate>` 访问 adapter；adapter 析构时关闭 gate，迟到的 VM 回调不会解引用已销毁对象。
- `begin` 被接受后，adapter 保证恰好投递一个终态结果；投递失败时释放预留容量，不把请求留在 map 中。
- 同 tid 的结果由 Runtime 的 `TidOrderState` 按 ingress sequence 排序：后完成的较小 sequence 先缓存，等前面的 sequence 完成后按升序交给 Core。这样 Core 看到的层顺序与通知入口顺序一致。
- Runtime 在每次接受查询后安排 `5200 ms` 后台唤醒，保证 5 秒超时即使没有新 ingress 也会被 `poll` 处理。

## 3. S4 实现状态

### 3.1 Core

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| 七区域数据结构 | 完成 | `src/core/Types.h` 的 `ActorState::regions[7]`；`src/core/Core.h` 暴露 `Core` |
| `AddLayers` | 完成 | `src/core/Core.cpp` |
| `ClearRegions` | 完成 | `src/core/Core.cpp` |
| `SettingsChanged` | 完成 | `src/core/Core.cpp` |
| `replaceState` | 完成 | `src/core/Core.cpp` |
| 五层上限 / 忽略新增 | 完成 | `EffectiveLayerCap` + `AtCapacity` |
| LIFO 截断 | 完成 | `SettingsChanged` 对每区域 `resize(cap)` |
| 稳定纹理 index | 完成 | splitmix64 种子 + 区域内未使用优先 |
| 层 id 与 nextLayerId | 完成 | `CoreState::nextLayerId` |
| `Advance` | 未实现业务 | 当前返回空 `ChangeSet`，S5/S6 接入 |

行为：

- 每次 `AddLayers` 先按 `(actor, region)` 去重，再按区域计数；达到 `maxLayersPerRegion`（固定为 5）时新增被忽略并产生 `AtCapacity` 诊断，已有层不改变。
- `LayerRecord` 保存 `{id, textureIndex, variant}`；纹理池：BodyVaginal/BodyAnal/BodyBreast/Face 为 1..12，BodyLegs/Hands/Feet 为 1..4。种子相同、状态相同时选择稳定；优先选择区域内未使用的编号，池耗尽后从起始位置重用。
- 每次成功加层递增区域和 actor 的 `revision`，并生成完整 `RegionTarget`（不是增量 patch）。
- `ClearRegions` 重复清除幂等；区域原本为空时仍可生成目标，但不递增 revision。
- `SettingsChanged` 对每个 actor 的七个区域截断到当前 cap，丢弃最新层（LIFO），生成全部七个区域的完整目标。
- `replaceState` 用于读档/恢复：清空后按 `ActorState` 重建，过滤 actor id 0/重复，限制 decay 余量，截断超限层，并重算 `nextLayerId = max(layer.id + 1)`。
- 本次只有 BodyVaginal 有 Overlay profile；其余六区域仍会生成 ChangeSet，Runtime 目前只把 BodyVaginal 送入 Overlay，不会伪造其他区域的显示。

### 3.2 OverlayAdapter

| 内容 | 状态 | 位置 |
| --- | --- | --- |
| RaceMenu exchange/v2 握手 | 完成 | `src/integration/RaceMenuApi.cpp` |
| BodyVaginal profile | 完成 | `src/integration/OverlayAdapter.cpp` |
| 自有/空槽分配 | 完成 | `reconcile` 的 `availableSlots` + `slotAppearsFree`；新层优先、容量不足时部分应用 |
| 差量更新/签名/revision | 完成 | `AppliedRegion`、`makeSignature` |
| 清除 | 完成 | 空 target、`overlaysEnabled=false`、`removeSlot` 写默认 diffuse + alpha 1.0 并清空 normal/specular 材质 |
| 3D/安装回调恢复 | 完成 | `OverlayInstalled` + `processDirtyVisuals` 的首次 3D 检测 → `invalidateActor` |
| 读档槽位回收 | 完成 | `slotAppearsFree` 识别本插件 diffuse 前缀，`resetSlotIfOurs` 清理未使用槽 |
| 其他模组共存检查 | 完成（待实测） | diffuse/key 6@0xFF/9-1/9-2/alpha 8@0xFF 检查 |
| 显式 `AddOverlays` bool | 完成 | `AddOverlays(reference, true)` |

关键语义：

- 节点格式不是硬编码旧脚本的 `"Body [ovl"`；每次 reconcile 从运行中的 `GetOverlayFormat(OverlayType::Normal, OverlayLocation::Body)` 读取（当前 RaceMenu 为 `Body [Ovl{}]`），替换 `{}` 为槽位号。
- BodyVaginal 使用 `firstOwnedSlot[Body]` 起的 6 个槽位（默认 7..12）；`GetOverlayCount` 是运行时的权威值。`configured <= firstSlot` 或可用槽位不足时返回 `NoOwnedCapacity`/`NoOwnedSlot`，逻辑层保留，不覆盖其他模组。
- 复用已缓存槽位时，只有当前 diffuse 为空/等于我们上次写入的值，且 normal/specular 也仍是我们的值时才复用；新槽位要求没有 key 6（RaceMenu 归一化索引 `0xFF`）、非空 key 9/1、非空 key 9/2 覆盖，alpha 8/0xFF 缺失或接近 1.0，且 diffuse 为空/默认贴图/本插件自己的贴图。本插件自己的 diffuse 可回收，解决读档后 `applied_` 为空但 RaceMenu 共存档仍保留旧覆盖的问题。
- 槽位分配按**最新层优先**：每层先尝试自己的缓存槽，再占用其他可用槽；容量不足时应用能放下的层，返回 `NoOwnedCapacity` + `slotsApplied`，逻辑层不回滚，下一轮继续重试。旧层不会占住新层需要的位置。
- 签名包含格式、区域、透明度和每个 `slot:layerId:textureIndex:variant`；revision 回退返回 `StaleRevision`；revision 和签名都相同返回 `NoChange`。
- 首次为 actor/epoch 建立 overlay 时调用 `overlay->AddOverlays(reference, true)`。0.4.20 的公开参数名是 `defer`，但实现把 `true` 当作立即执行、`false` 当作排队任务；这里显式传 `true`，并在已核验的 pump 阶段调用。
- `AddNodeOverride` 只写 override map，随后调用 `override->SetNodeProperties(reference, true)` 做同步应用；不使用可能延迟的 `false` 路径。
- diffuse 写 key 9 index 0；`_n` 写 9/1，仅在文件存在时；`_s` 写 9/2，仅在文件存在时。变体缺少可选贴图时写空字符串覆盖，清除材质上残留的 normal/specular；Mega 集没有 `_s`，因此不保留旧 9/2。
- alpha 写 key 8 index `0xFF`（RaceMenu 的非索引键），值为 `settings.overlayOpacity` 的 clamp 结果。
- 清除不能只调 `RemoveNodeOverride`：RaceMenu 0.4.20 只删除 override map，不还原材质。`removeSlot` 在确认槽位仍是本插件的情况下写默认 `textures\actors\character\overlays\default.dds`、alpha 1.0，并用空字符串清空 9/1、9/2；diffuse 已被其他模组改写时不动该槽。
- `invalidateActor` 保留槽位归属，只把 revision/signature 清空；下一次 reconcile 用完整逻辑目标重放。`RaceMenuReady` 保留已初始化的 actor，`Actor3DRebuilt`/`CellChanged` 清除初始化标记以重新 `AddOverlays`。`clearSession` 清空全部缓存。
- 没有 ActorWith3D 时返回 `DeferredNo3D`，Runtime 保存 dirty target，等待独立 pump 重试；不会把逻辑层删掉。`Runtime::processDirtyVisuals` 记录每个 actor 的首次 3D 就绪并触发一次 `Actor3DRebuilt` 重放；读档后的旧 RaceMenu 覆盖也由这次重放/清理回收。

### 3.3 BodyVaginal 资源核验

- 旧包 `E:/source/repos/SLACS2 p+ 0.7` 没有 NIF；RaceMenu 从角色身体 skin 克隆 `body_overlay.nif`，所以本插件不需要提供 BodyVaginal NIF。
- 普通集：`body1..body12`、`_n`、`_s`，共 36 个 DDS；全部 2048×2048 DXT5。diffuse 5,592,560 字节，normal/specular 4,194,432 字节。
- Mega 集：`m_body1..m_body12` + `_n`，共 24 个 DDS；没有 `_s`。其中 `m_body5/6/7/8/9/10` 是 DX10 BC7（dxgi=98），其余为 DXT5；游戏内显示效果待实测。
- 材质键语义按已核验实现：key 9 index 0 diffuse、index 1 normal、index 2 Body skin 的 texture3；旧脚本对 body 克隆节点写 `_s` 到 index 2 是有效的。Face 的 index 2 会命中 FaceGen 特殊槽，因此 S4 不把该规则推广到 Face。
- 60 个文件已复制到 `package/Data/Textures/slacs2/Vaginal`，总量约 265 MB。

### 3.4 Runtime 投影和命令

- `applyChangeSet` 只对 `Region::BodyVaginal` 解析 `ActorWith3D` 并调用 `OverlayAdapter::reconcile`；其余区域保留在 Core 状态中，不产生 Overlay 诊断。
- `Core::apply` 产生的 `CoreDiagnostic` 映射为 Runtime 诊断：`AtCapacity→CapacityUnavailable`、`InvalidTarget→ActorUnavailable`、`InvalidRegion/InvalidSettings→AdapterFailure`。
- `NoOwnedCapacity` 记录 `CapacityUnavailable` 并保留逻辑层；Runtime 保留对应 dirty target，并每 500 ms 唤醒 pump 重试，直到用户抬高 RaceMenu 槽位或目标清除。日志按失败次数收敛，避免每 500 ms 刷屏。
- `SettingsChanged` 走 `Core::apply(SettingsChanged{})` 后立即 `applyChangeSet`；`ClearActor` 清除七个区域；`TestAdd` 生成 1..5 次独立 `AddLayers`，`TestClear` 清除指定/全部区域。这三者复用 Core→ChangeSet→Overlay。`RefreshActor` 是投影级重放：先使 Overlay 缓存失效，再从当前 `CoreState` 重新生成该 actor 的 BodyVaginal `RegionTarget`，不修改业务层；它仍以 Core 为唯一状态源，不另建状态。
- `OverlayInstalled`（RaceMenu 安装回调）只带 FormID/epoch；Runtime 在 pump 中用 ActorRegistry 查 ActorId，再失效并重放当前 BodyVaginal 目标。

## 4. 修改入口

| 文件 | 本次修改 |
| --- | --- |
| `src/integration/PPlusAdapter.h/.cpp` | 新增 S3 adapter、staged VM 查询、筛选/映射、超时/取消/终态投递 |
| `src/core/Core.cpp`、`src/core/Core.h` | 新增 S4 Core 状态变更和 ChangeSet 生成；公开 `Core` 类型移入 `Core.h` |
| `src/integration/OverlayAdapter.h/.cpp` | 新增 BodyVaginal profile、槽位/差量/清除/失效重放 |
| `src/integration/RaceMenuApi.h/.cpp` | 新增 RaceMenu v2 exchange 握手 |
| `src/integration/racemenu/IPluginInterface.h` | 从 RaceMenu 0.4.20 ModderResource 引入的 ABI 头（加 `#pragma once` 和 provenance） |
| `src/runtime/Ingress.h/.cpp` | `reserve/release/postReserved`、`OverlayInstalled` 命令 |
| `src/runtime/ActorRegistry.h/.cpp` | `find(FormId)` 供安装回调回查 ActorId |
| `src/runtime/Runtime.h/.cpp` | PPlus/Overlay 成员、依赖接线、poll/排序/ChangeSet 投影、菜单命令 |
| `src/config/Settings.h/.cpp` | 沿用 S1 字段；未改变 schema 和默认值 |
| `src/core/Types.h` | 沿用 S1 契约类型；`Core` 声明移到 `src/core/Core.h` |
| `tests/s4_checks.cpp`、`xmake.lua` | 新增 Core 自动检查目标 |
| `package/Data/Textures/slacs2/Vaginal/*.dds` | BodyVaginal 交付资源 |
| `package/README.zh-CN.md` | 安装、RaceMenu 槽位、资源说明 |

## 5. 公开接口

除新增内部实现文件外，公开契约保持 `docs/INTERFACES.zh-CN.md` 的名称、签名、单位和所有权。本次实际使用的入口：

```cpp
namespace slacs
{
    QueryStart PPlusAdapter::begin(const OrgasmQuery& request);
    bool PPlusAdapter::cancel(RequestId requestId, SessionEpoch epoch) noexcept;
    void PPlusAdapter::cancelTid(Tid tid, SessionEpoch epoch) noexcept;
    void PPlusAdapter::cancelEpoch(SessionEpoch epoch) noexcept;
    std::uint32_t PPlusAdapter::poll(SteadyTime now, std::size_t maxCallbacks);

    ChangeSet Core::apply(CoreState& state, const CoreOp& op, const Settings& settings, const GameTime& now);
    ChangeSet Core::replaceState(CoreState& state, std::vector<ActorState> reboundState,
                                 const Settings& settings, const GameTime& now);

    OverlayHandshake OverlayAdapter::initialize();
    OverlayResult OverlayAdapter::reconcile(const ActorLease& actor, const RegionTarget& target,
                                             const Settings& settings);
    void OverlayAdapter::invalidateActor(ActorId actor, VisualInvalidation reason);
    void OverlayAdapter::clearSession(SessionEpoch epoch);

    IngressResult Runtime::enqueue(RuntimeCommand a_command);
    PumpReport Runtime::pump(const GameTime& a_now, WorkBudget a_budget);
}
```

`OrgasmQuery` 没有增加契约外字段；`AddLayers` 的 `selectionSeed` 由 Runtime 从 `sequence/tid/source` 计算，Core 不访问系统随机源。

## 6. 构建

环境：Windows x64、VS2022、XMake 3.0.9、CommonLibSSE-NG 6.0.0（commit `15ad90b31b2ca09cf477c7e1a43ff5ff05af120f`），生成运行时元数据 `1,6,1170,0`。

```powershell
xmake f -p windows -a x64 --toolchain=msvc -y
xmake build -y -r
```

结果（完整日志：`docs/implementation/build-s2.log`）：

- C++ 编译、链接成功；控制台无 error/warning（完整构建 9.891s）。
- `PapyrusCompiler.exe`：`0 error(s), 0 warning(s)`，`Assembly succeeded`。
- `package ready: E:\source\repos\SLACS2 NG\package\Data`
- `build ok`。

产物：

| 产物 | 大小 | 位置 | SHA-256 |
| --- | --- | --- | --- |
| DLL（构建目录） | 1,892,864 字节 | `build/windows/x64/release/SLACS2 NG.dll` | `f39f9bcfe8b498b3f8c88254475fa442e77372a63ada07948c3a41790b56116d` |
| DLL（包内） | 1,892,864 字节 | `package/Data/SKSE/Plugins/SLACS2 NG.dll` | 同上 |
| PEX | 1,198 字节 | `package/Data/Scripts/SLACS_Main.pex` | `8e5c9af617b194eb4beebbc68d582e2388600684d8cbf1a57264f83e553193d4` |
| ESP | 25,850 字节 | `package/Data/SLACS_2_Plus_SCO.esp` | `77477cfe385a67e3c7e8c487b60450cb94f549913aeca1523063f97d6669976e` |
| BodyVaginal DDS | 60 个，约 265 MB | `package/Data/Textures/slacs2/Vaginal/` | — |

说明：DLL 的哈希以本次最终构建为准；源码哈希会随注释/文档之外的代码改动变化，不作为二进制身份。

## 7. 自动检查

### 7.1 S1 检查

```powershell
xmake build -y slacs_s1_checks
./build/windows/x64/release/slacs_s1_checks.exe
```

结果：`SLACS2 NG S1 checks passed`（`docs/implementation/s1-checks-s2.log`）。覆盖 Settings 默认/往返/非法值，以及 Ingress 关闭、序号、容量、连续同 actor/tid 通知、终态容量预留与释放。

### 7.2 S4 Core 检查

```powershell
xmake build -y slacs_s4_checks
./build/windows/x64/release/slacs_s4_checks.exe
```

结果：`SLACS2 NG S4 checks passed`（`docs/implementation/s4-checks.log`）。覆盖：

- 五层上限、`AtCapacity`、层 id 连续。
- 同一次通知内重复区域去重、self 不加层。
- 种子稳定、纹理池范围。
- 清除幂等和 revision。
- `SettingsChanged` 的 LIFO 截断和七区域完整目标。
- `replaceState` 的 clamp、`nextLayerId`、formId。

### 7.3 ESP/PEX/资源检查

```powershell
py tools/clean_esp.py --verify package/Data/SLACS_2_Plus_SCO.esp
```

结果：`verified package\Data\SLACS_2_Plus_SCO.esp`（`docs/implementation/esp-verify-s2.log`）。

构建内 `tools/package.py` 还会检查 PEX magic、必需/禁止字符串和生成的 1170 元数据；本次构建通过。

DDS/资源核对（`docs/implementation/s4-assets.log`）：

- normal 12 组完整 `d/n/s`；mega 12 组完整 `d/n`，无 `_s`。
- 所有文件 2048×2048；绝大多数 DXT5，mega 的 5/6/7/8/9/10 为 DX10 BC7。
- 当前 RaceMenu ini：Body/Face/Hands/Feet = 6/3/3/3，`bPlayerOnly=1`。

### 7.4 未覆盖的自动检查

- 没有在真实 Papyrus VM 中执行的 P+ adapter 集成测试；staged 查询、回调线程和 5 秒超时只有静态代码路径。
- 没有 RaceMenu 运行环境测试；槽位/覆盖/3D 重建只能由 1170 实测确认。
- 没有其他模组覆盖同一节点的自动化测试；共存检查目前是代码级检查和资源/节点命名核对。

### 7.5 独立审核与修复

2026-11-06 使用 DS V4.1 Flash 做了只读对抗审核（代码为准，不以报告为准）。已修复并重新构建的确定性问题：

- P+ 回调重复派发/终态字段竞态：`waiting` 阻止同一 stage 重复 dispatch，`dispatchToken` 丢弃过期回调，失败字段在锁内先写、再 release `terminal`。
- P+ 回调持有 adapter 的 UAF：回调改为持有 `shared_ptr<CallbackGate>`，adapter 析构时关闭 gate。
- RaceMenu `RemoveNodeOverride` 不还原材质：清除改为写默认 diffuse、alpha 1.0 和空 normal/specular 覆盖。
- 读档后 `applied_` 为空导致旧槽被当成外部占用：通过本插件 diffuse 前缀回收 7..12 槽，并在读档时走 `Core::replaceState` + ChangeSet 重放。
- texture-set key 6 的 RaceMenu 索引归一化为 `0xFF`；`slotAppearsFree` 已按 `6/0xFF`、`8/0xFF`、`9/1`、`9/2` 检查。
- 槽位不足时旧层占满：改为最新层优先（每层先试缓存槽，再试其他空槽），允许部分应用并返回 `NoOwnedCapacity`，每 500 ms 重试（日志 5 秒收敛一次）。
- 3D 重建缺少事件源：`processDirtyVisuals` 检测 actor 首次 `ActorWith3D`，触发 `Actor3DRebuilt` 重放。
- 有向交互只识别 CTYPE 1..4：扩展到 3/5/6/7/11（Oral/Face）、8（Foot）、9（Hand），有向数组存在时不再用 scene tag 广播 Breast/Hand/Foot。
- 旧场景结果可能落入复用 tid：End 丢弃该 tid 的排序状态，重复 Start 先取消旧 tid；End 后到达的完成按未知请求拒绝。
- 读档失败分支只回滚装载：无效 FormID/层 id 时同时清空 Core、ActorRegistry、dirty/排序/3D/重试缓存和 Overlay 会话缓存。

审核仍未消除的限制：没有 1170 实际 VM/RaceMenu 运行记录；`RefreshActor` 是 Core 目标投影重放而非 `Core::apply`；`ActorRegistry` resolve 未校验 registry epoch（会话切换时 Runtime 会 reset，当前路径不跨 epoch 复用）；外部模组在本插件无失效通知时改写同一槽，需等下一次 ChangeSet/刷新才重新主张。

## 8. 1170 游戏实测

### 环境

- 游戏：Skyrim SE 1.6.1170.0，SKSE 2.2.6（`skse64_1_6_1170.dll`）。
- 目标依赖：P+ 2.17NG（`SexLabUtil.dll` 2.17.0.0，`SexLab.esm` 380,652 字节）、RaceMenu 0.4.20、Menu Framework 3.10、`SLACS_2_Plus_SCO.esp`。
- 当前物理环境（SESSION-01 记录）：P+ 2.16.0.0，`SexLab.esm` 347,818 字节。该环境不满足依赖门，不能用于 S3/S4 验收。

### 执行状态

本次**没有启动 1170 做 S3/S4 行为测试**，因为依赖版本不符；不把旧的 Load/Install 记录当作本轮行为通过。以下为待执行项和条件：

| 项目 | 命令/操作 | 期望 | 当前结果 | 待执行条件 |
| --- | --- | --- | --- | --- |
| 依赖门 | 启动到 DataLoaded，查看 `SLACS2 NG.log` | 出现 initialized/ready，P+ 版本匹配 | 未执行 | 替换为 P+ 2.17NG 和 380,652 字节 `SexLab.esm` |
| 握手 | 查看日志 | `RaceMenu overlay/override v2 ready`（消息收件人 `SKEE`） | 未执行 | RaceMenu 0.4.20 + skee64.ini 抬高槽位 |
| 单来源多目标 | 触发 P+ orgasm | 每个目标按有向区域加层，无 self | 未执行 | 干净 1170 + 2.17NG |
| 仅随从/多人 | 非玩家场景 | 玩家/teammate 过滤符合规则 | 未执行 | 同上 |
| 查询乱序 | 同 tid 连续通知 | 完成结果按 ingress sequence 进 Core | 未执行 | 需日志注入或连续事件 |
| 失败/超时/取消 | 场景结束/禁用来源/强制延迟 | 对应失败码；不产生层 | 未执行 | 同上 |
| 同 tid 多次合法通知 | 连续 orgasm | 每次独立处理，不按 tid 去重 | 未执行 | 同上 |
| 五层上限 | 连续 6 次加层 | 第 6 次忽略，层数 5 | 自动检查通过；游戏未执行 | 同上 |
| 清除/重建 | Clear 后 Refresh/3D 重建 | 视觉移除，重建后恢复 | Core 清除通过；游戏未执行 | 需要菜单入口或临时调用 |
| 无 3D | 远处 actor 加层 | `DeferredNo3D`，之后重试 | 未执行 | 同上 |
| 槽位不足 | 保持 Body=6 | `NoOwnedCapacity`，逻辑层保留 | 未执行 | 当前 ini 即为不足配置 |
| 其他模组共存 | 占用 7..12 的节点/覆盖 | 不覆盖、不误删 | 未执行 | 需要共存模组 |

### 已确认的静态事实

- `AddOverlays` 的 bool 语义、`SetNodeProperties` 的 immediate 路径、节点格式和槽位计数来自已安装 RaceMenu 0.4.20 的头文件、DLL 字符串和源码核对；运行时行为仍未在本轮日志中出现。
- BodyVaginal 的 DDS 是真实 DXT5/BC7 资源；没有 NIF 依赖，显示节点由 RaceMenu 克隆。
- `_s` 只对 body 克隆节点的 key 9 index 2 使用；Face 的 index 2 是特殊槽，S4 未使用。

## 9. 验收项对照

| 验收项 | 实现状态 | 验证状态 |
| --- | --- | --- |
| 有向单来源/多目标 | 完成 | 有向 1/2/3/5/6/7/8/9/11/4 映射完成；VM/游戏待测 |
| 多人/仅随从场景 | 完成 | 代码路径完成；游戏待测 |
| 查询乱序 | 完成 | Runtime 排序代码完成；未做 VM 注入 |
| 查询失败 | 完成 | 失败码和诊断完成；未实测 |
| 超时 | 完成 | 5 秒 + 5.2 秒唤醒完成；未实测 |
| 取消 | 完成 | 场景结束/读档/新游戏路径完成；未实测 |
| 同 tid 多次合法通知 | 完成 | Ingress 层已测；Core/Overlay 游戏待测 |
| 五层上限 | 完成 | S4 自动检查通过 |
| 覆盖清除与重建 | 完成 | Core 清除通过；Overlay 清除/读档回收/3D 重放代码完成，游戏待测 |
| 无 3D | 完成 | `DeferredNo3D` + dirty 重试 + 首次 3D 检测；游戏待测 |
| 槽位不足 | 完成 | 最新优先部分应用 + 500 ms 重试；当前 ini 可复现，未跑游戏 |
| 重复清除 | 完成 | S4 清除幂等检查通过 |
| 其他模组覆盖共存 | 完成代码级 | 槽位/key 6@0xFF/9/8 所有权检查完成；未做共存实测 |
| 读档恢复 | 完成 | `Core::replaceState` + ChangeSet 重放 + 旧槽回收；游戏待测 |
| BodyVaginal 实际显示 | 完成代码级 | 需抬高 RaceMenu 槽位后实测 |
| 构建/打包 | 完成 | 构建、PEX、ESP、S1/S4 检查通过 |

## 10. 剩余事项

1. 在干净 1170 + P+ 2.17NG 环境中执行第 8 节全部待执行项，尤其是：
   - `RaceMenu overlay/override v2 ready` 是否出现；
   - `GetOverlayFormat` 返回的节点格式是否与 `Body [Ovl{}]` 一致；
   - `SetNodeProperties` 后视觉是否出现，3D 重建是否恢复；
   - 查询回调线程和 `poll` 的实际执行阶段。
2. 把 RaceMenu 的 `skee64.ini` 要求写进最终安装说明（已写 `package/README.zh-CN.md`）；不要随包覆盖用户的 RaceMenu 配置。
3. S5：在当前 `CoreState` 上接正式保存、共享期限和 load staging；Serialization 已实现 schema 1 的具体读写，但保存/读档策略、版本迁移和共享期限语义仍需在 S5 定稿。
4. S6：把自然衰减、雨/游泳采样、TimedClear 和 Quest 重试并入共享 Scheduler；`Core::Advance` 目前是空实现。
5. S7：为其余六区域补 profile/资源/跨区域槽位上限；当前只有 BodyVaginal 有 Overlay。
6. S8：把 Clear/Refresh/TestAdd/TestClear 接到中文菜单；当前命令处理已存在但没有外部入口。
7. 游戏内验证 `NoOwnedCapacity` 的 500 ms 重试：确认槽位抬高后无需重进场景即可显示，并确认日志不会刷屏。
8. Mega 的 BC7 DDS 需要实际显示验证；若引擎不支持，应转为 DXT5 或从 mega 变体排除。

## 11. 下一次入口

从 S5 保存与时间结算继续：

- 状态源：`CoreState`（`src/core/Types.h`）和 `Core::replaceState`。
- 快照：`Runtime::publishSaveSnapshot`、`src/persistence/Serialization.cpp` 的 schema 1。
- 读档：`Runtime::onLoadStaging`、`Runtime::commitLoadStaging`。
- 时间与共享期限：`Core::Advance`（当前空）、`DeadlineIntent`、`DeadlineKind::{NaturalDecay,TimedClear}`；S5/S6 共用同一期限模型。
- 调度：`Runtime::pump` 的 deadline 分支和 S6 共享 Scheduler；本次没有把 S5/S6 逻辑提前写入。

S3/S4 的稳定入口保持不变：`Runtime::processNativeOrgasm` → `PPlusAdapter` → `Runtime::processPPlusCompleted` → `Core::apply` → `Runtime::applyChangeSet` → `OverlayAdapter::reconcile`。
