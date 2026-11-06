# SESSION-04 — S6B 环境采样与 S7 七区域/效果

## 1. 本次结论

本会话完成 S6B 与 S7 的代码实现、Runtime 接线、资源落盘和自动检查：

- S6B：新增 `Environment` 采样器，Runtime 在已核验的游戏执行阶段按固定节拍解析 actor 并采样；室内使用 parent cell 的 `IsInteriorCell()`，降水使用 `Sky::IsRaining()` / `Sky::IsSnowing()`，游泳使用 `ActorState::IsSwimming()`。`observedRainHours` 只输出连续有效室外观测区间的增量，且每份样本只消费一次。
- S7：RaceMenu 覆盖扩展到 BodyVaginal、BodyAnal、BodyBreast、BodyLegs、Face、Hands、Feet 七个区域；逻辑层数、可见槽位、普通/Mega 纹理、材质通道、清理和失效机制已接入。
- 效果：Oral/Vaginal/Anal 的逻辑层数按阈值 1/3 派生 0–2 级，以 `anal*9 + vaginal*3 + oral` 查 0–26 组合；26 个保留 Spell 已建立稳定编号表，效果关闭或组合为 0 时移除本插件自有效果。
- Runtime：七区域 ChangeSet、效果 ChangeSet、dirty 重试、每秒环境采样、会话切换、读档恢复、3D 重建和删除 actor 均已接线。
- 构建与五组自动检查通过；1170 游戏内验收尚未执行，待执行条件见第 8 节。

状态：**S6B、S7 代码与自动检查完成；1170 实测待执行。**

## 2. S6B Environment 实现状态

### 2.1 采样阶段与节拍

入口：`src/runtime/Runtime.cpp:846` `Runtime::sampleEnvironment`。

- 只在 `sessionReady` 且 Runtime pump 的游戏执行阶段采样；不在 Papyrus VM、RaceMenu 回调或未确认线程中访问世界。
- `pump` 内使用 `std::chrono::steady_clock` 的 `nextEnvironmentSample_` 控制节拍，默认每 1 秒一次；采样节拍属于 Runtime 的 steady 时间，不与 Scheduler 的 Calendar/Active deadline 混用。Scheduler 仍负责 NaturalDecay、TimedClear、OverlayRetry 等游戏期限。
- 只遍历 CoreState 中的 actor，通过 `ActorRegistry::resolve(..., ResolveNeed::ActorOnly)` 重新解析；解析失败立即 `environment_.invalidate(actor)`，不产生样本。
- `RE::BSTimer` 为空或 `pauseCount != 0` 时对整个环境采样器调用 `invalidateAll()` 并直接返回；暂停期间不采样、不累计雨水时间。
- 每个 actor 采样后调用 `Core::Advance`，把 `EnvironmentSample` 同值传入 Core；采样失败不会清空层数，只断开雨水连续性。

### 2.2 室内、降水与游泳判定

入口：`src/runtime/Environment.cpp:27` `Environment::sample`。

- parent cell：`actorRef.GetParentCell()`；为空时视为不可观测，清该 actor 的观测并返回 `nullopt`。
- 室内：`cell->IsInteriorCell()`，即 parent cell 的 interior 标志；不使用 `IsInWater()` 或天气分类代替。
- 降水：`RE::Sky::GetSingleton()` 的 `IsRaining()` / `IsSnowing()`；`Sky` 是全局天气快照，因此本版只对“当前已加载且室外”的 actor 计算淋雨暴露，不给异地/未加载 NPC 断言降雨。
- 游泳：`actorRef.AsActorState()->IsSwimming()`；只有真正的游泳状态触发全清，涉水/浸水不算。
- 暴露口径：`exposedRain = raining && !snowing && !interior`。连续室外观测（同一 cell、相邻样本均为室外雨）定义本版雨水暴露；不还原采样间隔内部的瞬态进出室内。雪不加速自然衰减，也不清层；雪天仍按自然日历衰减补算。

### 2.3 observedRainHours 连续区间

- 每个 actor 保存上一次观测：日历小时、活动秒、steady wall time、parent cell FormID、上次是否为有效室外雨。
- 只有以下条件全部成立才视为连续：
  - 同一 parent cell FormID；
  - 日历间隔 `elapsedHours ∈ [0, 0.5]`；
  - 活动秒间隔 `elapsedActive ∈ [0, 5]`；
  - steady wall time 间隔 `∈ [0, 5s]`；
  - 上一次观测与本次观测都属于有效室外雨。
- 连续时 `observedRainHours = elapsedHours`；否则为 0，并从本次样本重新起算。
- 这样可覆盖：暂停、睡眠/等待、快速旅行、加载、卸载、跨 cell、长帧/卡顿、外部时间跳变。
- Core 只在该样本 `elapsed > 0.0` 时把 `observedRainHours` 计入预算（`src/core/Core.cpp:300`），同一时间戳的样本重复送入不会二次消费。

### 2.4 日历衰减与雨水预算

入口：`src/core/Core.cpp:262` `SettleActor`。

- 每次结算先推进 `lastSettledGameHours`，日历 elapsed 始终按自然衰减规则补算，不受暂停/加载是否断雨影响。
- 雨水预算仅在 `settings.rainAccelerates` 开启、自然衰减开启、样本非室内、`raining == true` 时加入；雪、室内、无有效连续观测均不加入。
- 两种预算共用 `decayIntervalGameHours`：`total = decayRemainder + elapsedCalendar + rainHours`。
- `naturalDecay == false` 时两项预算都停止：清空每区 remainder，不因日历/雨水删层；整体限时清除独立于该开关。
- 逐层公式保持：
  `k = min(层数, floor((remainder + elapsedCalendar + rainHours) / decayInterval))`，
  按 LegacyLifo 从旧层开始删除，余量保留给下一次结算。

### 2.5 游泳全清

`SettleActor` 检测到 `swimming && settings.swimCleaning == LegacyImmediateClear` 时：

- 一次清空七个区域；
- 清空各区的 `decayRemainderGameHours`；
- 清除 `timedClearAt`；
- `actor.revision++`；
- 为七个区域各推一个空 `RegionTarget`，Runtime 通过 ChangeSet 更新 Overlay 和 Effects。

## 3. S7 七区域 / 效果实现状态

### 3.1 七区域 profile

入口：`src/integration/OverlayAdapter.cpp:31` `kRegionProfiles`，`ProfileFor(Region)`；区域槽位入口 `src/config/Settings.cpp:609` `OverlaySlotsForRegion`。

| 区域 | 资源目录 | 文件名前缀 | 纹理池 | normal | specular | RaceMenu 域 | 域内偏移 | 可见槽 |
|---|---|---|---:|---|---|---|---:|---:|
| BodyVaginal | `Textures/slacs2/Vaginal` | `body` | 12 | 是 | 是 | Body | +0 | 6 |
| BodyAnal | `Textures/slacs2/Anal` | `body` | 12 | 是 | 是 | Body | +6 | 6 |
| BodyBreast | `Textures/slacs2/Breast` | `chest` | 12 | 是 | 是 | Body | +12 | 6 |
| BodyLegs | `Textures/slacs2/Footjob` | `legs` | 4 | 否 | 否 | Body | +18 | 2 |
| Face | `Textures/slacs2/Oral` | `head` | 12 | 是 | 否 | Face | +0 | 6 |
| Hands | `Textures/slacs2/Handjob` | `hands` | 4 | 否 | 否 | Hands | +0 | 2 |
| Feet | `Textures/slacs2/Footjob` | `feet` | 4 | 否 | 否 | Feet | +0 | 2 |

- `OverlayDomain` 顺序为 Body/Face/Hands/Feet，RaceMenu `OverlayLocation` 顺序为 Body/Hand/Feet/Face；`LocationFor` 显式映射，避免枚举序号误用。
- `BodyLegs` 与 Feet 共用 `Footjob` 目录，但分别使用 `legs` 与 `feet` 前缀、不同 RaceMenu 域。
- UBE 未建立 profile：`textures/slacs2_ube` 在当前 package 中不存在，`OverlayAdapter::initialize` 明确记录 `Overlay texture profile UBE is unavailable (textures/slacs2_ube is missing)`；`TextureVariant` 仍只有 Normal/Mega，不改变公开类型。

### 3.2 槽位、容量与选层

- 默认起始槽：四个域均为 7。
- Body：`+0/+6/+12/+18` 对应 7–12、13–18、19–24、25–26。
- Face：7–12；Hands：7–8；Feet：7–8。
- `Settings::validate`（`src/config/Settings.cpp:636`）检查：
  - Body 四子区范围两两不重叠；
  - Body/Face/Hands/Feet 各自不越过 RaceMenu 槽 250；
  - Body 基准 232 会因 legs 250–251 越界而拒绝，231 为最大合法值；
  - Face 基准 246、Hands/Feet 基准 250 被拒绝。
- 逻辑容量固定为每区 5 层（`Settings::validate` 要求 `maxLayersPerRegion == 5`）。
- 可见容量独立计算：`availableSlots = min(registryCount, configured - firstSlot)`，不会把逻辑层数直接当作可见槽数。
- 选层从最新层向旧层遍历，优先复用同一 `layerId` 以前占用的自有槽（仍属于本插件且未越界），否则只使用本插件自有槽或确认空闲的槽；不随机覆盖其他插件槽位，不删除逻辑层。
- 可见容量不足时返回 `NoOwnedCapacity`，Runtime 保留 dirty 并周期重试；诊断按 actor/region 限频，不刷屏。
- 纹理编号由 Core 按层种子从 1..池大小中选择未用编号；逻辑状态保存编号与变体，读档后保持一致。

### 3.3 材质通道与清理

入口：`src/integration/OverlayAdapter.cpp:724` `applySlot`、`:844` `texturePath`。

- 纹理键统一为 RaceMenu shader key 9；diffuse index 0、normal index 1；Body 三个主区域使用 index 2 作为既有 body/skin specular 通道。
- Face 不使用 index 2（FaceGen 特殊槽），Hands/Feet/BodyLegs 不使用 normal/specular（对应资源没有 `_n`/`_s`）。
- 路径：`Textures/slacs2/<folder>/[m_]<prefix><index><suffix>.dds`；Mega 统一加 `m_` 前缀。
- 可选键按 profile 和文件存在性处理：
  - profile 声明 normal/specular 且文件存在时才写入；
  - 文件不存在或切到 Mega（Mega 无 `_s`）时主动清对应键，避免上一次变体遗留；
  - 清理时只删本插件最后写入且仍匹配的键，外部 diffuse 保留，不误删其他插件/玩家覆盖。
- 首次非空层调用 `AddOverlays(ref, true)`，随后对本插件使用的槽调用 `SetNodeProperties(ref, true)`；新层、换槽和重建都走同一套写入与失效流程。
- `overlaysEnabled == false` 时移除缓存槽并扫描本域自有槽，恢复默认 diffuse、alpha 1，清本插件可选键。
- `clearActor(lease, id)` 按记录的 region 找到相应 RaceMenu 域，删除所有缓存槽；`clearSession` 只清缓存，不访问世界。

### 3.4 效果

入口：`src/integration/Effects.cpp:34` `initialize`、`:57` `reconcile`；Core 派生入口 `src/core/Core.cpp:106` `EffectLevel`、`:117` `PushSpellTarget`。

- 参与区域：Face=Oral，BodyVaginal=Vaginal，BodyAnal=Anal；Breast/Legs/Hands/Feet 不参与组合。
- 每区等级：层数 `>= spellLevel2Threshold` → 2；`>= spellLevel1Threshold` → 1；否则 0；默认阈值 1/3。
- 组合索引：`combinationIndex = analLevel * 9 + vaginalLevel * 3 + oralLevel`，范围 0–26。
- 26 个保留 Spell 的 local FormID 表位于 `Effects.cpp`，按组合索引 1..26 排列；`initialize` 用 `TESDataHandler::LookupForm<SpellItem>(local, "SLACS_2_Plus_SCO.esp")` 解析。
- 每个 Spell 校验存在效果、baseEffect 和 `data.effectShader`；失败仅记录错误并让 `effectsReady_ = false`，不关闭整个会话。
- `reconcile`：
  - 无 actor 租约 → `DeferredActorUnavailable`，保留 dirty；
  - 未初始化 → `ApiUnavailable`；
  - 索引越界 → `Failed/MissingForm`；
  - revision 回退 → `StaleRevision`；
  - 先移除 1..26 中除目标外的本插件 Spell，再对目标 `AddSpell`；
  - 组合为 0 或 `effectsEnabled=false` 时目标为空，移除全部本插件 Spell；
  - 缓存 actor → 组合索引/revision。
- Runtime 的 `invalidateActorVisuals` 会按当前层数重算组合，并清 Effects 缓存，确保重建后重新应用。

### 3.5 Runtime 接线

| 行为 | 入口 | 状态 |
|---|---|---|
| 每秒环境采样 | `src/runtime/Runtime.cpp:846` | 完成 |
| ChangeSet 七区域投影 | `src/runtime/Runtime.cpp:1274` | 完成，已删除 BodyVaginal 特判 |
| ChangeSet 效果投影 | `src/runtime/Runtime.cpp:1274` | 完成，`dirtyEffects_` 重试 |
| 七区域 dirty 重试 | `src/runtime/Runtime.cpp:1401` | 完成 |
| 效果 dirty 重试 | `src/runtime/Runtime.cpp:1453` | 完成 |
| 3D 重建失效 | `src/runtime/Runtime.cpp:1509` | 遍历七区域并清效果缓存 |
| 会话开始/重置 | `src/runtime/Runtime.cpp:285` / `:338` | 清效果、清 Overlay、重置环境 |
| 删除 actor | `Runtime::removeActorForm` | 清 Core、deadline、Overlay、Effects、Environment |
| 读档恢复 | `Runtime::commitLoadStaging` + `Core::replaceState` | 恢复层/编号/变体/余量/限时剩余，重推七区与效果目标 |
| 菜单加层/清除/刷新 | `Runtime::processMenuAction` | 已支持七区域、TestAdd/TestClear/RefreshActor |

## 4. 修改入口

| 文件 | 关键行 | 修改内容 |
|---|---:|---|
| `src/config/Settings.h` | — | 新增 `OverlaySlotRange` 与 `OverlaySlotsForRegion` 声明 |
| `src/config/Settings.cpp` | 609 | 七区域槽位映射、Body 子区偏移与数量 |
| `src/config/Settings.cpp` | 636 | 四域范围、250 上限与 Body 子区互斥校验；补 BodyLegs 末端检查 |
| `src/core/Core.cpp` | 106 | 效果等级派生 |
| `src/core/Core.cpp` | 117 | 26 组合 SpellTarget 推送 |
| `src/core/Core.cpp` | 262 | 自然衰减、雨水预算、游泳全清、限时清除 |
| `src/core/Core.cpp` | 380 | 加层后重算效果与视觉目标 |
| `src/core/Core.cpp` | 465 | 清除后重算效果与视觉目标 |
| `src/core/Core.cpp` | 577 | 读档替换状态后重推七区与效果 |
| `src/runtime/Environment.h/.cpp` | 27 | 新环境采样器 |
| `src/integration/Effects.h/.cpp` | 34/57 | 效果表、解析、应用与移除 |
| `src/integration/OverlayAdapter.h/.cpp` | 31/240/724/844 | 七区域 profile、槽位、通道、路径、清理 |
| `src/runtime/Runtime.h/.cpp` | 285/338/846/1274/1401/1453/1509 | 采样、七区/效果 ChangeSet、dirty、会话切换 |
| `tests/s7_checks.cpp` | — | 七区/雨水/游泳/26 组合/槽位自动检查 |
| `xmake.lua` | 65 | 新增 `slacs_s7_checks` 目标 |
| `tools/verify_assets.py` | — | 七 profile 必需/可选纹理检查 |
| `tools/package.py` | — | 打包前调用纹理检查 |

## 5. 公开接口

### 5.1 Environment

```cpp
class Environment
{
public:
    [[nodiscard]] std::optional<EnvironmentSample> sample(
        ActorId a_actor, RE::Actor& a_actorRef, const GameTime& a_now);

    void reset(SessionEpoch a_epoch);
    void invalidate(ActorId a_actor);
    void invalidateAll();

private:
    struct Observation
    {
        double calendarHours{};
        double activeSeconds{};
        SteadyTime wall{};
        FormId cellFormId{};
        bool exposedRain{};
    };

    SessionEpoch epoch_{};
    std::unordered_map<ActorId, Observation> observations_;
};
```

### 5.2 Effects

```cpp
enum class EffectStatus : std::uint8_t
{
    Applied,
    NoChange,
    DeferredActorUnavailable,
    ApiUnavailable,
    StaleRevision,
    Failed,
};

enum class EffectErrorCode : std::uint8_t
{
    None,
    MissingForm,
    StaleRevision,
    ApiFailure,
};

struct EffectResult
{
    EffectStatus status{ EffectStatus::Failed };
    ActorId actor{};
    DesiredSpell desired{};
    Revision revision{};
    EffectErrorCode error{ EffectErrorCode::None };
};

class Effects
{
public:
    bool initialize();
    EffectResult reconcile(const ActorLease& a_actor, const SpellTarget& a_target);
    void invalidate(ActorId a_actor);
    void clearSession(SessionEpoch a_epoch);
};
```

### 5.3 槽位辅助

```cpp
struct OverlaySlotRange
{
    std::uint16_t firstSlot{};
    std::uint16_t slotCount{};
};

[[nodiscard]] OverlaySlotRange OverlaySlotsForRegion(
    const Settings& a_settings, Region a_region);
```

### 5.4 Core 既有公开契约

本次没有改变 `Core::apply` / `Core::replaceState` / `ChangeSet` / `DesiredSpell` / `SpellTarget` 的公开签名。`ChangeSet.spells` 从本次起由 Core 产生，Runtime 负责投影。

## 6. 构建

### 6.1 环境

| 项 | 值 |
|---|---|
| 平台 | Windows x64 |
| 配置 | Release |
| 构建系统 | XMake 3.0.9 |
| 编译器 | Visual Studio 2022 |
| CommonLibSSE-NG | 6.0.0 |
| 运行时目标 | Skyrim SE 1.6.1170（编译期元数据） |
| Papyrus | PapyrusCompiler，`tools/package.py` 调用 |

### 6.2 命令与结果

```powershell
xmake clean
xmake build "SLACS2 NG"
xmake build slacs_s1_checks slacs_s4_checks slacs_s5s6_checks slacs_s5_persistence_checks slacs_s7_checks
```

结果：

- 主 DLL 编译、链接成功；
- Papyrus `SLACS_Main`：`0 error(s), 0 warning(s)`，assembly succeeded；
- 纹理检查：`UBE profile: missing resources (expected)`；`overlay textures: 120 diffuse, 96 normal, 36 specular`；`asset check ok`；
- 打包阶段：`package ready: E:\source\repos\SLACS2 NG\package\Data`；
- 构建收尾：`build ok`。

日志：

- `docs/implementation/build-s4.log`（clean + 主目标完整构建）
- `docs/implementation/checks-build-s4.log`（五组检查目标构建）

## 7. 自动检查

### 7.1 命令与结果

```powershell
./build/windows/x64/release/slacs_s1_checks.exe
./build/windows/x64/release/slacs_s4_checks.exe
./build/windows/x64/release/slacs_s5s6_checks.exe
./build/windows/x64/release/slacs_s5_persistence_checks.exe
./build/windows/x64/release/slacs_s7_checks.exe
```

输出（`docs/implementation/checks-s4.log`）：

```text
SLACS2 NG S1 checks passed
SLACS2 NG S4 checks passed
SLACS2 NG S5/S6 checks passed
SLACS2 NG S5 persistence checks passed
SLACS2 NG S7 checks passed
```

### 7.2 S7 覆盖项

`tests/s7_checks.cpp`：

| 检查 | 内容 |
|---|---|
| `CheckSevenRegionPools` | 七区纹理池 12/12/12/4/12/4/4；每区 5 层；Mega 变体存储 |
| `CheckSwimmingClearsAll` | 游泳清七区、清 timedClear、推 7 个空 RegionTarget；涉水不清 |
| `CheckRainBudget` | 雨 0.5h + 日历 0.5h 消耗一层；同一时间戳重复样本不二次消费；干 0.5h 保留余量；室内样本忽略 observed；雪不加速；`rainAccelerates=false`；`naturalDecay=false` 清余量 |
| `CheckSpellCombinations` | 1→1、3→2、oral2+vaginal1→5、anal1+vaginal1+oral2→14、Breast/Legs/Hands/Feet 不偏移、最大 26、27 组合穷举、`effectsEnabled=false`、阈值 3/5、清除后回 0 |
| `CheckOverlaySlots` | Vaginal 7–12、Anal 13–18、Breast 19–24、Legs 25–26、Face 7–12、Hands/Feet 7–8；移位后 +0/+6/+12/+18；Body 232/Face 246/Hands/Feet 250 拒绝 |

### 7.3 未覆盖

自动检查不访问游戏世界和 RaceMenu 渲染，不覆盖：

- 真实 Sky 天气切换、室内外 cell、暂停/等待/旅行的时间行为；
- RaceMenu 实际槽位/renderer、第一/第三人称、NPC 3D 重建；
- 真实存档/读档与 FormID remap；
- SlaveTatsNG 或其它 overlay 插件共存；
- 26 个 Spell 在 1170 游戏内的实际 MGEF/EFSH 表现。

## 8. 1170 游戏实测

### 8.1 环境与执行状态

| 项 | 值 |
|---|---|
| 目标环境 | Skyrim SE 1.6.1170 + SKSE 2.2.6 |
| P+ | 参考基线 2.18.1；当前可测环境为 P+ 2.16.0.0 + `SexLab.esm` 347,818 字节，不能作为 2.18.1 验收 |
| RaceMenu | 0.4.20 |
| SKSE Menu Framework | 3.10.0.0 |
| 当前执行状态 | **未执行**：本会话没有 1170 游戏进程、可交互新档和依赖替换条件 |
| 前置配置 | RaceMenu Body/Face/Hands/Feet overlay 槽位至少 26/12/8/8；NPC 重建测试需 `bPlayerOnly=0`。当前记录的 ini 为 6/3/3/3、`bPlayerOnly=1`，会触发容量不足，需在实测环境中调整 |

### 8.2 逐项验收

| 验收项 | 环境/前置 | 操作 | 期望 | 当前结果 | 待执行条件 |
|---|---|---|---|---|---|
| 雨转晴 | 1170、室外、天气可控 | 室外雨中停留 ≥1 游戏小时，转晴后再观察 | 雨中按“日历+等量雨水”加速衰减；转晴后只按日历衰减；`observedRainHours` 不跨天气继续累计 | 未执行（代码路径已具备） | 有可控制天气的 1170 新档 |
| 室内/室外 | 1170、同一天气 | 室外雨中采样后进入室内，再回室外 | 室内不增加雨水预算，日历照常补算；回室外重新起算连续观测 | 未执行（自动检查覆盖 Core 口径） | 可切换 cell 的实测环境 |
| 观测中断 | 1170 | 暂停、等待/睡眠、快速旅行、加载、卸载 NPC | 中断区间贡献 0；不出现跨暂停/加载的雨水补偿 | 未执行 | 可执行暂停/等待/旅行/存档的实测环境 |
| 雪 | 1170、雪天 | 室外雪中停留 | 雪不清层、不加速；日历衰减照常 | 未执行（自动检查覆盖 Core 口径） | 可控雪天天气 |
| 游泳全清 | 1170、有水的 cell | 进入游泳状态 | 七区域一次全清，timedClear 清除，Overlay/Effects 同步清空；涉水不清 | 未执行（自动检查覆盖七区与目标） | 可游泳的实测 cell |
| 七区域与四域槽位 | 1170、RaceMenu ini 足够 | 逐区加层并检查 Body/Face/Hands/Feet | Vaginal 7–12、Anal 13–18、Breast 19–24、Legs 25–26、Face 7–12、Hands/Feet 7–8 | 未执行；槽位映射已由自动检查验证 | 槽位配置达标后逐区测试 |
| 普通/Mega | 1170、Mega 种族或全局开关 | 普通与 Mega 变体分别加层 | 普通用 `<prefix>N`，Mega 用 `m_<prefix>N`；Mega 无 `_s` 时不残留 specular | 未执行；路径/profile 已静态核对 | 可切换 Mega 条件的实测角色 |
| 缺可选贴图 | 1170、临时移除某 `_n`/`_s` | 加该层并切换变体 | 缺 normal/specular 时跳过写入并清遗留键，diffuse 正常显示 | 未执行；`applySlot` 已按文件存在处理 | 可替换 Data 纹理的实测环境 |
| 容量不足 | 1170、RaceMenu 槽位不足 | 使可见层数超过自有/空槽 | 保留逻辑 5 层；仅显示可用槽；`NoOwnedCapacity` 重试且不覆盖他人槽 | 未执行；静态路径已具备 | 使用 6/3/3/3 ini 或人为减少槽位 |
| 26 组合与关闭效果 | 1170、P+ 事件或测试加层 | 构造 oral/vaginal/anal 0/1/2 级，开关 `effectsEnabled` | 索引 `anal*9+vaginal*3+oral`；阈值 1/3；组合 0 或关闭时移除本插件 Spell | 未执行；27 组合已自动穷举 | 能在游戏内触发加层/改配置 |
| 保存恢复一致 | 1170 | 加层、计时、变色后保存，退出重进读取 | 层数/编号/变体/余量/限时剩余一致；Overlay/Effects 重建；雨水观测从加载起重新起算 | 未执行；持久化与 replaceState 路径已静态核对 | 可存读档的实测环境 |
| 第一/第三人称 | 1170 | 切换视角并检查七个区域 | 两个视角都使用同一套自有槽/键，切换后不丢层、不残留 | 未执行；`SetNodeProperties(ref,true)` 覆盖双视角为静态事实 | 可切换视角的实测角色 |
| NPC 卸载/重建 | 1170 | 离开 cell 使 NPC 卸载，返回重建 | 卸载清理表现缓存；重建后按 Core 层数重新投影；不丢逻辑层 | 未执行；`removeActorForm`/`processDirtyVisuals` 已接线 | 可控制 cell 卸载的实测场景 |
| RaceMenu/SlaveTatsNG 共存 | 1170、两插件同时启用 | 交替添加/移除 overlay，检查槽位与纹理 | 只使用自有/空槽，不覆盖外部槽；清理只清本插件匹配键 | 未执行；仅静态实现，未做第三方实测 | 安装 SlaveTatsNG 的实测环境 |

## 9. 验收项对照

| 验收项 | 实现状态 | 自动检查 | 1170 实测 |
|---|---|---|---|
| 雨转晴 | 完成 | Core 雨预算覆盖 | 待执行 |
| 室内/室外 | 完成 | Core 口径覆盖 | 待执行 |
| 观测中断和雪 | 完成 | 中断/雪口径覆盖 | 待执行 |
| 游泳全清 | 完成 | 七区/限时清除覆盖 | 待执行 |
| 七区域与四域槽位 | 完成 | 槽位/互斥/容量覆盖 | 待执行 |
| 普通/Mega、缺贴图、容量不足 | 完成 | 资源 profile/路径/容量静态覆盖 | 待执行 |
| 26 组合与关闭效果 | 完成 | 27 组合穷举覆盖 | 待执行 |
| 保存恢复一致 | 完成 | 持久化既有检查 + replaceState 路径 | 待执行 |
| 第一/第三人称 | 完成 | 不适用（渲染） | 待执行 |
| NPC 卸载/重建 | 完成 | 不适用（生命周期） | 待执行 |
| RaceMenu/SlaveTatsNG 共存 | 完成 | 不适用（第三方） | 待执行 |

## 10. 产物位置

| 产物 | 路径 | 大小 | SHA-256 |
|---|---|---:|---|
| DLL | `package/Data/SKSE/Plugins/SLACS2 NG.dll` | 2,011,136 | `6aa80979800772e76105e8e9dee7564f721b56816c0f1aac120dabd66583e188` |
| PEX | `package/Data/Scripts/SLACS_Main.pex` | 1,198 | `42e327d2d7253ac47322554379565f3d69ea9f0539ffea19c52a716132d50e16` |
| ESP | `package/Data/SLACS_2_Plus_SCO.esp` | 25,850 | `77477cfe385a67e3c7e8c487b60450cb94f549913aeca1523063f97d6669976e` |
| 纹理 | `package/Data/Textures/slacs2/{Anal,Breast,Footjob,Handjob,Oral,Vaginal}` | 264 DDS，约 1.2 GB | — |
| 主构建 DLL | `build/windows/x64/release/SLACS2 NG.dll` | 2,011,136 | 同 package DLL |

纹理分布：

- Vaginal/Anal/Breast/Oral：各 60 个（12 主贴图 + 12 `m_` + 24 `_n` + 12 `_s`；Mega 无 `_s`）；
- Handjob：8 个（4 主 + 4 `m_`，无 `_n`/`_s`）；
- Footjob：16 个（4 feet + 4 legs + 4 `m_feet` + 4 `m_legs`，无 `_n`/`_s`）；
- UBE：`textures/slacs2_ube` 不存在，标记“缺少资源”。

## 11. 剩余事项

1. **1170 游戏内实测**：按第 8 节逐项执行并填写结果；当前文档只能声明构建/自动检查完成，不能声明游戏验收通过。
2. **RaceMenu ini 调整**：Body/Face/Hands/Feet 至少 26/12/8/8；NPC 重建需关闭 `bPlayerOnly`。当前 6/3/3/3 会触发容量不足验证项。
3. **UBE 资源**：当前明确标记缺少资源；若后续提供 `textures/slacs2_ube`，需新增 UBE profile 和资源检查，不改公开 `TextureVariant`。
4. **第三方共存**：与 SlaveTatsNG 的槽位/键清理只做了静态设计，需实机验证。
5. **可选键残留**：`applySlot` 已按 profile/文件存在清理 normal/specular；若实测发现 RaceMenu 重装或跨存档残留，再补一次全槽扫描。

## 12. 下一次入口

下一次实现：

1. 使用 SKSE Menu Framework 完成全中文菜单（替换 MCM 入口）；
2. 配置持久化（`Data/SKSE/Plugins/SLACS2NG.json` 的完整读写/校验/保存反馈）；
3. 总体验收（含 1170 实测记录）；
4. 安装包整理（package/Data 目录、依赖说明、安装顺序）。

当前入口文件：

- Runtime：`src/runtime/Runtime.cpp`
- 环境：`src/runtime/Environment.cpp`
- 效果：`src/integration/Effects.cpp`
- Overlay：`src/integration/OverlayAdapter.cpp`
- 槽位/配置：`src/config/Settings.cpp`
- 自动检查：`tests/s7_checks.cpp`, `xmake.lua`
- 资源检查：`tools/verify_assets.py`, `tools/package.py`
