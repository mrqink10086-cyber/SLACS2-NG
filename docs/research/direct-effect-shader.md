# 直接播放 Effect Shader 的原生路径

研究基线为 1.0.6（`e8da8d7`）。文中的“当前实现”和 Effects 行号描述该基线；1.0.7 按此研究实现直接 EFSH、旧法术迁移与实例核验，实际实现契约见 [INTERFACES](../INTERFACES.zh-CN.md#8-racemenu-overlayadapter-与-effects-s4s7)。游戏验收项继续保留。

## 结论

可以由 C++ 直接把 **EFSH (`TESEffectShader`)** 播放到 `TESObjectREFR`，不必为了显示它而给 Actor 添加 Spell，也不必手工构造 `ActiveEffect`。CommonLibSSE-NG 声明的入口是 `TESObjectREFR::ApplyEffectShader(TESEffectShader*, ...)`，返回 `ShaderReferenceEffect*`；实现转发到游戏引擎函数。它是可用的原生 API 路径。

这不是“只靠 MGEF”：MGEF 是魔法效果定义，EFSH 才是此入口接收的可视效果资源。直接播放会跳过 SPEL/MGEF 到 ActiveEffect 的施加链，只保留 EFSH 的 shader 视觉。是否能在 SLACS 的所有装备模型上得到与当前 Spell 相同的结果，尤其装备更换、Actor 3D 重建后的结果，仍须游戏内验证。

## 四种记录和实例不要混为一谈

| 层 | 作用 | 本项目中的关系 |
| --- | --- | --- |
| SPEL / `SpellItem` | 魔法容器，持有若干 `Effect` 项 | 当前 `Effects.cpp` 加到 Actor 上的 Spell |
| MGEF / `EffectSetting` | 定义一个魔法效果；包括 `effectShader`、`enchantShader` 等字段 | Spell 的 `Effect::baseEffect` 指向 MGEF；本项目解析出的 MGEF 指向 EFSH |
| `ActiveEffect` | Spell/MGEF 施加到目标后的运行时魔法效果实例 | `MagicTarget::AddTarget` 接受包含 `MagicItem*`、`Effect*` 等数据的 `AddTargetData`；这是魔法施加路径的一部分 |
| EFSH / `TESEffectShader` | shader 效果记录及纹理/参数 | `ApplyEffectShader` 的直接输入；调用会返回运行时 `ShaderReferenceEffect` |

本地定义位置：`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/M/MagicItem.h:128-135`（Spell 的 effects 列表）、`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/E/Effect.h:40-46`（`Effect::baseEffect`）、`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/E/EffectSetting.h:80-81`（两个 shader 字段）、`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/M/MagicTarget.h:90-115`（魔法目标施加参数和 `AddTarget`）、`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/A/ActiveEffect.h:63-100`（ActiveEffect 生命周期与 controller）。[CommonLibSSE-NG EffectSetting](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/E/EffectSetting.h#L80-L81) · [MagicTarget](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/M/MagicTarget.h#L90-L123) · [ActiveEffect](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/A/ActiveEffect.h#L63-L100)

## 直接应用和停止

`TESObjectREFR.h` 的签名为：

```cpp
ShaderReferenceEffect* ApplyEffectShader(
    TESEffectShader* a_effectShader,
    float a_duration = -1.0f,
    TESObjectREFR* a_facingRef = nullptr,
    bool a_faceTarget = false,
    bool a_attachToCamera = false,
    NiAVObject* a_attachNode = nullptr,
    bool a_interfaceEffect = false);
```

本地依据：`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/T/TESObjectREFR.h:366-367`、`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/T/TESObjectREFR.cpp:90-95`。cpp 用 `RELOCATION_ID(19446, 19872)` 调游戏实现。`Actor` 是 `TESObjectREFR` 派生类型，可在 Actor reference 上调用；本地签名没有 Spell、MGEF 或 ActiveEffect 参数。[CommonLibSSE-NG declaration](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/T/TESObjectREFR.h#L366-L367) · [implementation](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/src/RE/T/TESObjectREFR.cpp#L90-L95)

返回值是实际的 shader reference effect 实例，而不是 `ActiveEffect`。`ShaderReferenceEffect` 继承 `ReferenceEffect`；后者公开 `controller`、目标 handle 和 `finished` 字段。没有在本地 CommonLib API 中找到 `TESObjectREFR::StopEffectShader` 这类配对方法。可用的低层控制证据有两种：

- `ReferenceEffectController::Stop()` 扫描 `ProcessLists::magicEffects`，并移除 `referenceEffect->controller == this` 的临时效果（`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/R/ReferenceEffectController.cpp:88-103`）。它按 controller 清理；在确认 controller 不会共用前，不能断言它只移除一个 shader 实例。
- `ProcessLists::ForEachShaderEffect()` 可枚举 shader 临时效果（`E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/P/ProcessLists.cpp:78-87`）。本地 `StopAllMagicEffects(ref)` 的做法是找到目标的 `ReferenceEffect` 并设 `finished = true`（`:103-112`）；这会匹配该目标的全部 reference effects，粒度太宽，不能直接拿来替换某个组合的 shader。

当前证据足以说明“可以播放”，但还不足以定下 SLACS 实例的安全停止/识别实现。`ApplyEffectShader` 返回值是临时效果实例的指针，实例失效时机和可安全持有范围需实测；不应把它当作可存档身份。`StopAllMagicEffects` 粒度过宽；controller 的 `Stop()` 也不是 `TESObjectREFR` 上的 shader 专用停止 API。避免在 `ForEachShaderEffect` 回调里调用 `controller->Stop()`：枚举经由 `ForEachMagicTempEffect` 持有 `magicEffectsLock`（`ProcessLists.cpp:55-65`），而 `Stop()` 会再次取得同一锁（`ReferenceEffectController.cpp:88-96`）。在验证精确识别和停止路径前，不应把现有关闭/切换行为视为已等价迁移。

## 装备模型范围和生命周期证据

- **装备/几何范围尚未由源码证明。** `ApplyEffectShader` 是对 reference 调用，签名提供可选的 `a_attachNode`；默认 controller 的 `GetAttachRoot()` 返回目标的 `Get3D()`（`ReferenceEffectController.cpp:33-37`）。这说明默认 attach root 是目标 3D 根节点，不等于已证明只影响装备网格或所有装备网格都会着色。`TESEffectShader` 有 `kSkinOnly` flag（`TESEffectShader.h:28-48`），`ShaderReferenceEffect` 有 `wornObject`、`addonObjects`、`targetRoot` 等成员（`ShaderReferenceEffect.h:76-97`），但这些布局名称没有说明 ApplyEffectShader 对换装几何的实际筛选/重绑规则。须在游戏内用不同 Armor/ArmorAddon、`kSkinOnly` 设置、第一/第三人称和 `a_attachNode` 组合验证。
- **换装与 3D 卸载：** Actor 声明有 `WornArmorChanged()`（`Actor.h:504`），但已检查的 shader/reference-effect 接口没有证明直接 EFSH 会在换装后重新绑定，或在卸载/重建 3D 后自动重放。基类 controller 默认 `GetClearWhenCellIsUnloaded() == false`、`GetAllowNo3D() == false`（`ReferenceEffectController.cpp:55-75`）；这是默认实现，不应当作所有引擎调用路径的已验证行为。
- **保存与读档：** `ReferenceEffect` 声明 `SaveGame`、`LoadGame`、`FinishLoadGame`，`ShaderReferenceEffect` 也 override 这些 hook（`ReferenceEffect.h:21-41`、`ShaderReferenceEffect.h:53-73`）。这只能证明类型有序列化入口，不能证明通过 `ApplyEffectShader` 创建的效果在保存/读档后必然恢复、绑定到正确的新 3D、或与插件自己的 Actor 状态同步；仍需实测。
- **生命周期边界：** 默认 duration 在 C++ 签名中是 `-1.0f`，但本地声明/转发本身没有说明负值在目标运行时中的最终持续语义。确认目标 Skyrim runtime 的行为、Actor 删除/重生、换装和 cell unload 时的自动清理，以及效果累积/去重。

## SLACS 记录与最小迁移建议

当前实现检查第一个 Spell effect 的 `baseEffect->data.effectShader`，并通过 `Actor::HasSpell/RemoveSpell/AddSpell` 增删 Spell：`E:/source/repos/SLACS2 NG/src/integration/Effects.cpp:24-31`、`:41-55`、`:96-117`。CommonLib 本地 record 定义证明 `EffectSetting` 能关联 EFSH；它不需要为了调用 `ApplyEffectShader` 而先构造 `ActiveEffect`。

SLACS 的逻辑组合不是三个布尔值：肛/阴/口三个部位各有 0、1、2 三级，组成 `3³ = 27` 个状态；0 是无效果，其他 26 个状态对应现有 EFSH。当前 `Runtime.cpp` 用密集三进制索引 `anal * 9 + vaginal * 3 + oral`，索引 0..26；反解可用 `anal = i / 9`、`vaginal = (i / 3) % 3`、`oral = i % 3`（`E:/source/repos/SLACS2 NG/src/runtime/Runtime.cpp:1680-1693`）。若希望用位运算打包，可各留 2 bit，例如 `packed = (anal << 4) | (vaginal << 2) | oral`，三位域取值 0..2，3 为非法；这是另一种编码，需要与当前三进制索引明确区分并映射到 26 个表项。当前密集索引已经能放进 `uint8_t`，无需为节省空间重编码。

组合状态和运行时 EFSH 实例是两种状态。当前 `Effects::Applied` 记录组合索引与 revision（`E:/source/repos/SLACS2 NG/src/integration/Effects.h:53-60`），但 reconcile 仍靠 `HasSpell` 检查真实施加状态（`Effects.cpp:96-117`）。改成直接播 EFSH 后，`HasSpell` 不再能回答 shader 是否存在；应将插件维护的期望/已应用组合索引与 revision 和引擎临时 shader 实例探测分开。原生实例指针不可写入 cosave；读档、Actor 3D 重建或 session 清理后，如何重建实例视图/决定是否重放，取决于实机验证所得的存档和生命周期行为，不在此处假定具体指针所有权方案。

并行 ESP 解析给出的记录证据来自 `E:/source/repos/SLACS2 NG/package/Data/SLACS_2_Plus_SCO.esp`：26 个 SPEL 各带 1 个 EFID，链接到 26 个 MGEF，MGEF 的 DATA 中引用 26 个 EFSH；26 个 EFSH 的 ICON 字段引用 `slacs2\\clothes\\` 下的 26 个组合 DDS，其他所查贴图引用字段为空。示例：`SPEL 0x04002DB1 spell_oral1 -> MGEF mf_spell_oral1 -> EFSH shader_spell_oral1`。共享工作区目前可见这 26 张 DDS 于 `E:/source/repos/SLACS2 NG/package/Data/Textures/slacs2/clothes/`；资源修复是与原生调用路径分开的打包事项。直接 EFSH 仍依赖 EFSH 自己的资源引用，不能借此跳过缺失纹理。

最小迁移方向是让 27 个逻辑状态继续使用现有索引、为 1..26 查到对应 `TESEffectShader*`，播放时走 `ApplyEffectShader`，并单独定义逻辑状态与运行时实例状态的同步规则。先通过游戏内实验确定实例识别/停止、换装显示和生命周期，再决定如何替换当前依赖 `HasSpell` 的 reconcile；在此之前不建议开始等价替换。Spell/MGEF/ActiveEffect 仍可保留作资源映射来源，但不是该直接播放 API 的调用参数。

**需验证的验收项：** 无其它 SLACS Spell 时直接 EFSH 是否有同等视觉；每种组合及缺资源报告；多件不同网格装备、换装前后、Actor 3D 重建、cell 卸载/加载、保存/读档、重复 reconcile 不叠加、关闭后只停 SLACS 效果、不误停其他魔法视觉。尤其确认该效果究竟是 shader overlay 还是期望中的装备材质替换；源码 API 名称和 EFSH record 类型不足以证明最终渲染语义。

## 来源边界

主要 API 与布局依据是项目 vendored 的 CommonLibSSE-NG 源码（上述本地绝对路径/行号）及其[上游仓库](https://github.com/CharmedBaryon/CommonLibSSE-NG)。尝试访问 [Creation Kit 的 `PlayShader`](https://ck.uesp.net/wiki/PlayShader_-_ObjectReference) 和 [`StopShader`](https://ck.uesp.net/wiki/StopShader_-_ObjectReference) 页面时，网页读取器拒绝了这两个 URL；本笔记不依赖这些页面来声称 Papyrus 签名或持续语义。当前结论只覆盖可从本地 C++ declaration/forwarder 和对象定义直接确认的路径；最终附着/渲染/恢复行为需在游戏内核实。
