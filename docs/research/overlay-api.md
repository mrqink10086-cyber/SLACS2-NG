# RaceMenu / SlaveTatsNG 覆盖层接口调研

调研日期：2026-11-05。范围：为 Skyrim SE 1.6.1170 的 SLACS2 NG 规划原生覆盖层实现，面向新档，允许重新设计数据结构。本文只有源码调研和候选方案。本轮未实现桥接、安装依赖或构建插件。

文中“事实”来自当前本地源码；“方案”是建议的设计；“待验证”需要选定的 1.6.1170 实际 DLL、资源或游戏环境。当前 RaceMenu 源码声明的是 Steam **1.7.99** / GOG **1.6.1179**，因此接口存在与 1.6.1170 可运行是两个独立验收项。[RaceMenu/skee64/main.cpp:719](<E:/source/repos/RaceMenu/skee64/main.cpp:719>)

## 结论与决策状态

- **候选首选路径：** 用 RaceMenu 的公开 `IOverlayInterface` / `IOverrideInterface` 管理覆盖层和持久节点覆盖值；SLACS2 NG 自己维护逻辑状态、槽位所有权、衰减和脏数据队列。公开接口已覆盖节点纹理、alpha、光泽度、镜面强度以及重建通知。[IPluginInterface.h:360](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:360>)、[IPluginInterface.h:442](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:442>)
- **RaceMenu C++ ABI 需先验收：** 当前两个接口都标为 v2，但本地源码并非 1.6.1170 的运行时匹配证据。实施前选定 1170 版 `skee64.dll` 和对应公开头文件，核对虚函数顺序、参数及布尔语义，再做最小接入试验。
- **已确认直接使用 RaceMenu：** 用户选择不要求安装 SlaveTatsNG，由 SLACS2 NG 管理自身状态和覆盖层。SlaveTatsNG 作为实现参考与共存测试对象。本地实现另有纹身目录/JContainers 的高级 API，以下保留其调查结果供理解共存行为。
- **定时器不取代引擎事件/安装回调：** 覆盖层安装、3D 重建和外部事件仍需要入口；计时器或协程只负责唤醒、取消和调度。任何 `co_await` 都不能扩大 RaceMenu 回调内指针、锁或调用线程的有效范围。

## 1. 可接入的接口和 ABI

### 1.1 获取流程

**事实：** `InterfaceExchangeMessage::kMessage_ExchangeInterface = 0x9E3779B9`，消息只带 `IInterfaceMap* interfaceMap`。RaceMenu 收到交换消息后同步填入自己的全局接口表；表按 `"Overlay"`、`"Override"` 等名称发布接口。[IPluginInterface.h:24](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:24>)、[IPluginInterface.h:42](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:42>)、[main.cpp:555](<E:/source/repos/RaceMenu/skee64/main.cpp:555>)、[main.cpp:943](<E:/source/repos/RaceMenu/skee64/main.cpp:943>)

**事实：** RaceMenu 在未启用 early registration 时于 SKSE `PostLoad` 注册交换消息监听。SlaveTatsNG 的现有绑定使用 `SKSE::GetMessagingInterface()->Dispatch(..., "SKEE")`，随后 `QueryInterface("Overlay")` / `QueryInterface("Override")`；其初始化发生在 `kDataLoaded`。[RaceMenu/main.cpp:572](<E:/source/repos/RaceMenu/skee64/main.cpp:572>)、[nioverride_wrapper.h:12](<E:/source/repos/SlaveTatsNG-master/include/nioverride_wrapper.h:12>)、[SlaveTatsNG/main.cpp:95](<E:/source/repos/SlaveTatsNG-master/src/main.cpp:95>)

**方案：** 在所有 DLL 完成加载后获取接口，具体时点与主插件初始化流程统一；依次检查消息派发成功、非空 map、两个非空接口、分别支持的接口版本、所需回调能力与 overlay 配置。消息长度传 `sizeof(message)`。现有参考代码传 `sizeof(InterfaceExchangeMessage*)`，在当前仅含一个指针的结构上恰好相等，不能作为通用规则。[nioverride_wrapper.h:14](<E:/source/repos/SlaveTatsNG-master/include/nioverride_wrapper.h:14>)

**方案：** map 和接口指针作为 RaceMenu 所有的借用对象；SLACS 不 `delete`、不 `Revert()`、不向全局 map 移除其他接口。声明和虚函数顺序采用选定发布版本的完整公开头文件，避免裁剪后重新声明虚表或混用旧地址表。`IPluginInterface` 有虚析构函数、`GetVersion()`、`Revert()`，`IInterfaceMap` 有 `QueryInterface/AddInterface/RemoveInterface`，这些都会影响 ABI。[IPluginInterface.h:24](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:24>)

### 1.2 主要 API

| 用途 | 本地公开声明 | 使用约束 |
| --- | --- | --- |
| 是否启用覆盖层 | `bool HasOverlays(TESObjectREFR*)` | 玩家固定返回 true，不能用它证明某个节点已经存在或 3D 已加载。 |
| 首次建立 NPC overlays | `void AddOverlays(TESObjectREFR*, bool)` | 当前实现会为演员加入集合并发起整体构建；不可每个渐隐 tick 调用。 |
| 配置与名称 | `GetOverlayCount(OverlayType, OverlayLocation)`、`GetOverlayFormat(...)` | Normal/Spell 分开；位置为 Body/Hand/Feet/Face。优先读取实际运行配置。 |
| 安装通知 | `RegisterInstallCallback(key, cb)` / `UnregisterInstallCallback(key)` | v2，借用回调参数；回调内只发出短消息。 |
| 持久覆盖记录 | `AddNodeOverride(..., SetVariant&)`、`GetNodeOverride(..., GetVariant&)`、`HasNodeOverride(...)`、`RemoveNodeOverride(...)` | 一次值访问；返回字符串必须在 visitor 内复制。 |
| 应用记录 | `SetNodeProperties(refr, immediate)` | 会遍历演员所有已记录节点及第一/第三人称根；单次合并应用，不能认为 false 会把全部操作延后。 |
| 精确即时表现 | `SetNodeProperty(refr, firstPerson, nodeName, key, index, value, immediate)` | 与持久记录的职责不同；渐隐选择哪条路径需做载入/重建一致性试验。 |

声明依据：[Overlay API](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:360>)、[Override API](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:403>)、[节点 API](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:442>)。玩家行为：[OverlayInterface.cpp:821](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:821>)；整体构建行为：[OverlayInterface.cpp:850](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:850>)、[OverlayInterface.cpp:1044](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1044>)。

**事实 / 方案：** 计数返回的是配置值，构建流程还受 `g_enableOverlays` 与 `g_enableFaceOverlays` 门控；数量大于零不能证明功能启用或节点已生成。接入验证必须分别检查容量和实际构建结果。[OverlayInterface.cpp:897](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:897>)、[OverlayInterface.cpp:1046](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1046>)、[OverlayInterface.cpp:1095](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1095>)

### 1.3 布尔参数存在源码不一致，必须显式处理

**事实：** 公开 `IOverlayInterface::AddOverlays(..., bool defer = true)` 的参数名和默认值，与实现 `OverlayInterface::AddOverlays(..., bool immediate)` 的实际意义不同。实现中 `false` 走 `g_task->AddTask(...)`，`true` 直接 `Run()`。通过公开接口省略参数会实际传入 `true`。[IPluginInterface.h:372](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:372>)、[OverlayInterface.cpp:850](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:850>)

**方案：** 对选定 1170 DLL 验证后，包装层只暴露有明确定义的操作（例如“请求初始化覆盖层”），内部显式传参；不向业务层泄漏这个 bool。其他 overlay 的 `defer/immediate` 参数也逐项核对。

### 1.4 1.6.1170 的证据边界

**事实：** 当前 RaceMenu `SKSEPlugin_Version` 标明 `not version independent`，Steam 分支 `RUNTIME_VERSION_1_7_99`，GOG 分支 `RUNTIME_VERSION_1_6_1179_GOG`。[main.cpp:719](<E:/source/repos/RaceMenu/skee64/main.cpp:719>)

**事实：** SlaveTatsNG 携带的现代 `SKEE_IPluginInterface.h` 使用 `RE::` 引擎类型，其 Overlay v2 / Override v2 方法形状与当前公开头文件相应区段一致，说明 CommonLibSSE 风格的直接接口已有参考。接口版本数本身不确认目标 DLL 的游戏地址、对象布局或导出的 ABI。[SKEE_IPluginInterface.h:361](<E:/source/repos/SlaveTatsNG-master/include/skee-modders-resource-v0-4-19-16/SKEE_IPluginInterface.h:361>)、[SKEE_IPluginInterface.h:400](<E:/source/repos/SlaveTatsNG-master/include/skee-modders-resource-v0-4-19-16/SKEE_IPluginInterface.h:400>)

**待验证：** 1170 发布 DLL 是否提供 v2 Overlay/Override、安装回调和相同 bool 行为；若目标包只提供旧接口，优先调整最低 RaceMenu 发布版本或缩减使用能力。是否支持旧地址表须另立产品决策，当前候选路径不需要它。

## 2. 材质、纹理与应用代价

### 2.1 属性映射

**事实：** RaceMenu 的 shader key 定义：`0` emissive color、`1` emissive multiple、`2` glossiness、`3` specular strength、`6` texture set、`7` tint color、`8` alpha、`9` texture。非索引属性经 `SetValueVariant` 归一化为 `0xFF`，因此应在封装中用明确的无索引常量代替业务层散布 `-1`。[OverrideVariant.h:25](<E:/source/repos/RaceMenu/skee64/OverrideVariant.h:25>)、[OverrideInterface.cpp:2525](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:2525>)

| 表现 | 候选更新内容 | 源码依据 / 验证点 |
| --- | --- | --- |
| 主纹理 | key `9`，index `0` | 旧版和 SlaveTatsNG 都使用此通道。 |
| normal / gloss | key `9`，index `1` | CommonLib 将 `kNormal` 与 `kGloss` 设为同值；需核对 normal 的 alpha 数据。 |
| 透明度 | key `8`，无索引，float | 实际写入 lighting material alpha；alpha 数值与 NIF 的混合/测试配置共同决定外观。 |
| 光泽度 | key `2`，无索引，float | 独立标量，不能等同 specular 贴图。 |
| 镜面强度 | key `3`，无索引，float | 独立标量。 |
| tint / emissive | keys `7/0/1` | 仅当资源 shader 与效果设计需要时使用。 |

依据：[SlaveTatsNG/overlays.cpp:134](<E:/source/repos/SlaveTatsNG-master/src/overlays.cpp:134>)、[BSTextureSet.h:19](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/B/BSTextureSet.h:19>)、[ShaderUtilities.cpp:289](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:289>)、[OverlayInterface.cpp:242](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:242>)。

**迁移发现：** 旧版把 `_n.dds` 写 key `9,index 1`，把 `_s.dds` 写 key `9,index 2`。[SLACS_Main.psc:774](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:774>) CommonLib 的 `BSTextureSet` 将 index `2` 命名为 EnvironmentMask/SubsurfaceTint，index `7` 才有 Specular 别名；RaceMenu 的 index `2` 又会随 shader 类型分派到不同材质字段。[BSTextureSet.h:19](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/include/RE/B/BSTextureSet.h:19>)、[ShaderUtilities.cpp:398](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:398>)

**方案 / 待验证：** 为现有 overlay NIF、body/face shader 与 DDS 通道做材质 profile，实机确认 `_s` 的真实用途，再选具体通道、标量或资源重制。文件后缀不足以决定 shader slot；也不能把 CommonLib 的枚举名称直接套入所有 legacy RaceMenu 材质类型。

### 2.2 区分“记录值”和“更新 3D”

**事实：** `AddNodeOverride` 将输入 variant 同步转换并写入按演员 formID / 性别 / 节点名索引的覆盖表；字符串先转换为 RaceMenu 的字符串类型。`SetNodeProperties` 随后解析 formID、取得第一/第三人称根、查找节点并调用 `SetShaderProperty`。[OverrideInterface.cpp:56](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:56>)、[OverrideInterface.cpp:2525](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:2525>)、[OverrideInterface.cpp:2723](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:2723>)、[OverrideInterface.cpp:777](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:777>)

**事实：** 更新纹理会复制当前 texture set、加载新纹理、绑定到材质并 `InitializeShader`。alpha/gloss/specular 则直接写标量；两者代价不同。[ShaderUtilities.cpp:147](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:147>)、[ShaderUtilities.cpp:291](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:291>)

**方案：** 纹理和材质 profile 只在新增/替换/重建时写；衰减 tick 只提交实际变化的 alpha。每演员合并一次脏更新，限制每帧提交量。先用公开持久覆盖记录 + 合并应用完成正确性基线，再测量是否需要用精确 `SetNodeProperty` 减少重复遍历。精确即时写仍应同步更新自有期望状态及所需持久记录，以便 3D 重建后重放。

**待验证：** `SetNodeProperties` 应用演员的全部记录，渐隐时是否重复触发已有纹理的加载/初始化；采用精确属性路径是否同时覆盖第一/第三人称、装备重建和读档恢复。以帧耗时及真实纹理加载次数比较，不以调用次数推断性能。

## 3. 槽位分配、其他 mod 共存和缓存

**事实：** RaceMenu 的节点覆盖数据按 `formID → gender → nodeName → (key,index)` 存储；同一 key/index 会 erase 后 insert，数据没有 mod owner 字段。[OverrideInterface.cpp:48](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:48>)、[OverrideVariant.h:19](<E:/source/repos/RaceMenu/skee64/OverrideVariant.h:19>)

**事实：** `AddOverlays` 是建立演员覆盖层集合；不是为调用者返回一个专属 slot。`SKSETaskUpdateOverlays` 会按当前 Body/Hand/Feet/Face 的 Normal/Spell 计数卸载并重装节点。[OverlayInterface.cpp:850](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:850>)、[OverlayInterface.cpp:1044](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1044>)

**事实：** SlaveTatsNG 的 `external_slots` 扫描每个配置槽的 diffuse 覆盖路径，并兼查 index `6`，用自己的纹理前缀和默认纹理辨别外部占用。这是约定式检测，没有全局原子预留 token。[tattoo.cpp:19](<E:/source/repos/SlaveTatsNG-master/src/tattoo.cpp:19>) 它还用 JContainers 的 `NiNodeOverrideLock` 条目做 compare-exchange 协作锁；只有参与相同协议的调用者受其约束。[ni_node_override_lock.cpp:11](<E:/source/repos/SlaveTatsNG-master/src/ni_node_override_lock.cpp:11>)

**方案：**

1. 从 `GetOverlayCount/Format` 取得真实容量和名称；将用户可配置的允许槽位范围与实际容量取交集。容量不足时按产品选定规则拒绝新增/替换本 mod 最旧层/降低视觉层数，绝不覆盖外部占用。
2. 状态层保存 `actor identity + area + logical layer + material profile`；表现层单独保存 `slot + expected property signature + actor/3D generation`。不把槽号当业务层层数或衰减顺序。
3. 分配时扫描允许槽位；同时检查纹理及准备写入的属性，默认纹理路径做大小写和斜杠规范化。选定后在提交前重新验证。自身预留由一个游戏阶段串行化，降低本插件内部竞争。
4. 清理只删除或重置仍与本插件最后写入签名相符的具体 key/index。发现外部改写时放弃该槽并重新分配；不恢复过期快照去覆盖其他 mod 的新值。
5. 禁用或移除本 mod 时只清理自己的槽。`RemoveAllNodeOverrides*`、全演员 `RemoveOverlays/RevertOverlays` 影响范围过大，不作为普通清理路径；玩家的 `RemoveOverlays` 在当前实现还会直接返回。[IPluginInterface.h:450](<E:/source/repos/RaceMenu/skee64/IPluginInterface.h:450>)、[OverlayInterface.cpp:834](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:834>)

**共存边界：** “检查后写入”与本插件自己的 mutex 不能阻止其他 DLL 同时抢同一槽。严格的跨 mod 原子预留需要共同协议；主计划应把 SlaveTatsNG、其他 overlay mod 的实际冲突行为纳入验证，必要时采用明确保留范围或共同分配入口。

**事实 / 优化机会：** 旧版固定 Body 20、Face 6、Hands 2、Feet 2 个区间，起始槽默认 7；MCM 改透明度时遍历整个配置区间写入，未按本插件实际拥有的层过滤。[SLACS_Main.psc:9](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:9>)、[SLACS_MCM.psc:579](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_MCM.psc:579>) 新档设计可用实际活动层、容量和所有权记录驱动写入，减少空槽写入与共存干扰。

## 4. 线程、指针、堆栈和重入

### 4.1 执行阶段和锁

**事实：** `SetNodeProperties(refr,false)` 同步解析演员、遍历当前 3D，并直接修改 alpha/gloss/specular；只有部分纹理操作因 `immediate=false` 进入任务队列。[OverrideInterface.cpp:777](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:777>)、[ShaderUtilities.cpp:291](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:291>)、[ShaderUtilities.cpp:303](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.cpp:303>) 因此 deferred 标志不构成“整条调用可从任意线程运行”的契约。

**事实：** `Impl_GetNodeOverride` 在局部 `SimpleLocker` 下返回内部 set 元素的裸指针，调用端 `GetNodeOverride` 在函数返回后再解引用并执行 visitor。内部锁不足以保证该读操作与其他线程的增删并发安全。[OverrideInterface.cpp:128](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:128>)、[OverrideInterface.cpp:2730](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:2730>)

**方案：** 所有 RaceMenu 查询、覆盖记录增删、引擎对象解析和场景写入统一在主计划确认的受控游戏任务阶段执行。工作线程仅操作不可变快照和纯值计算；结果携带 session generation 回到游戏阶段再校验。不要用持有 SLACS 全局 mutex 的方式包裹外部虚调用；先取得快照和预留、释放本地锁、调用外部接口，再按 generation 提交结果。跨 DLL 调用期间不等待 future、不阻塞游戏阶段。

**事实：** SlaveTatsNG 在源码注释中明确记录过 SKSE task 的执行线程 ID 随时间变化，其初始化仍把某次 task 的 ID 记录为 main thread。[main.cpp:156](<E:/source/repos/SlaveTatsNG-master/src/main.cpp:156>) 这是不能照搬固定 thread-id 判断的依据；实际 SKSE 调度保证由运行时调研与 1170 验证确认。

### 4.2 安装回调上下文

**事实：** RaceMenu 在新 shape 应用节点覆盖之后、附加到 destination 之前，持有 `m_callbacks` 锁同步调用所有安装回调；注册和注销也获取该锁。[OverlayInterface.cpp:250](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:250>)、[OverlayInterface.cpp:938](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:938>)

**方案：** 回调只复制回调期间可读的演员身份和 session generation，向线程安全队列提交“该演员表现失效”消息；不保留回调传来的 `ref/node`，不直接重建 overlays，不在回调里注册/注销，不等待，不执行深层业务调用。下一次受控游戏阶段重新解析演员和当前 3D 后再应用。由于回调可能先于节点附加完成，不能在回调内把“已回调”当作“完整场景已就绪”。

### 4.3 所有权和生命周期

| 对象 / 数据 | 源码事实 | 方案 |
| --- | --- | --- |
| `Actor*` / `TESObjectREFR*` | RaceMenu 的更新任务保存 formID，并在 `Run()` 时重新查找；SlaveTatsNG 参考异步路径却直接捕获 `a_target`。 | 长期和延迟任务使用 CommonLib handle + session/actor generation；每次执行时解析，检查对象、删除状态与当前 3D。序列化保存可重映射身份，不保存地址。 |
| `NiAVObject*` / material / geometry | RaceMenu 某些纹理任务持有 `NiPointer`，防止释放；它不能保证节点仍是演员当前 3D。 | 不跨等待缓存节点/材质指针。提交时从当前根重新找节点；生命周期引用与场景身份分别验证。 |
| `SetVariant&` | `AddNodeOverride` 同步读取 variant 并把字符串写入自己的值存储。 | 栈上 variant 可限于同步调用；调用期间字符串存储必须有效，不把临时 `c_str()` 放进延迟任务。 |
| `GetVariant` 的字符串 | `GetValueVariant` 在 visitor 里传入字符串的 `c_str()`。 | 在 visitor 内复制到自有 `std::string`/稳定字符串；跨回调不留原始 `const char*`。 |
| 外部接口指针 | 全局接口 map 返回由 RaceMenu 拥有的对象。 | 借用，无跨 DLL delete；初始化成功后使用，结束时先停止提交并清空本地任务。 |

依据：[OverlayInterface.cpp:1048](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1048>)、[OverlayInterface.cpp:1119](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1119>)、[SlaveTatsNG/overlays.cpp:940](<E:/source/repos/SlaveTatsNG-master/src/overlays.cpp:940>)、[ShaderUtilities.h:19](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.h:19>)、[ShaderUtilities.h:59](<E:/source/repos/RaceMenu/skee64/ShaderUtilities.h:59>)、[OverrideInterface.cpp:2525](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:2525>)、[OverrideInterface.cpp:2577](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:2577>)。

**堆栈 / 协程方案：** 不把栈变量的引用、`this` 的借用关系、visitor、外部裸指针或锁跨 `co_await`；协程 frame 只持有自有值、可取消任务 token、handle 和 generation。resume 前回到受控游戏任务阶段并重新解析。完成/取消路径使用有界队列派发，避免回调同步递归触发新的 callback 链；回调 ABI 内不传播异常。协程选型的价值在取消与流程表达，不改变上述生命周期条件。

### 4.4 3D 重建、读档与缓存

**事实：** RaceMenu 重装 overlay 时会重新应用已有节点覆盖，然后触发安装回调；整体构建会替换各槽节点。[OverlayInterface.cpp:250](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:250>)、[OverlayInterface.cpp:1044](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:1044>)

**方案：** 逻辑状态是权威，overlay 是可重建表现缓存。换装/3D 重建后标脏、下一阶段重放；3D 未加载时只更新逻辑值，不反复发起构建。新游戏、revert、读档切换递增 session generation，使排队任务失效；存档中记录本 mod 的层和必要身份，加载后重新认领/验证槽位。读档时与 RaceMenu 自身覆盖记录恢复的顺序需纳入验证，避免两个恢复流程相互覆盖。

## 5. SlaveTatsNG 的参考价值与边界

**事实：** 它提供 v1 `Addresses` 表，公开的 `simple_add_tattoo`、`simple_remove_tattoo`、`synchronize_tattoos` 等函数返回 `fail_t`，其中 `true` 表示错误。表中还暴露 implementation 函数，并在未提高 v1 数字的情况下追加过 `compile_cache`。[SlaveTatsNG_Interface.h:8](<E:/source/repos/SlaveTatsNG-master/include/SlaveTatsNG_Interface.h:8>)、[SlaveTatsNG_Interface.h:19](<E:/source/repos/SlaveTatsNG-master/include/SlaveTatsNG_Interface.h:19>)、[SlaveTatsNG_Interface.h:58](<E:/source/repos/SlaveTatsNG-master/include/SlaveTatsNG_Interface.h:58>) 其接口在 `kPostPostLoad` 发布。[main.cpp:90](<E:/source/repos/SlaveTatsNG-master/src/main.cpp:90>)

**事实：** `synchronize_tattoos` 的异步分支在 task 尚未执行时就返回 `false`，所以返回值不能用作“3D 更新已成功完成”的证据。[overlays.cpp:929](<E:/source/repos/SlaveTatsNG-master/src/overlays.cpp:929>)

**值得借鉴：** 多属性先写记录、后一次应用；外部占用检测；3D 未加载时检查；纹身目录和缓存的组织。具体实现仍需修正：初始化在 `GetVersion()` 前缺少返回接口空检查；现代 singleton 可在查询失败后保留空接口；`apply_overlay_ex` 将 `a_diffuse_color` 传到 `a_emissive_mult` 位置。[nioverride_wrapper.cpp:17](<E:/source/repos/SlaveTatsNG-master/src/nioverride_wrapper.cpp:17>)、[nioverride_impl_current.h:157](<E:/source/repos/SlaveTatsNG-master/include/nioverride_impl_current.h:157>)、[overlays.cpp:172](<E:/source/repos/SlaveTatsNG-master/src/overlays.cpp:172>)

**已决策：** SLACS2 NG 自己管理 overlay，不调用 SlaveTatsNG 的高级纹身 API。因此这些 API 不进入本插件的必需依赖；同时安装 SlaveTatsNG 的共存行为仍需验证。

## 6. 实施前的最小验证清单

以下是规划中的验证任务，当前未执行：

1. 固定实际 1.6.1170 的 SKSE、CommonLib 构建配置、RaceMenu DLL 和公开头文件版本；记录 hash/版本，验证接口获取和每个 v2 能力。
2. 单 NPC 与玩家分别建立一层，验证 normal/face 开关、当前槽数、第一/第三人称、卸载/重载 3D、换装及读档；确认 `AddOverlays` bool 的实际行为。
3. 审计 NIF shader 和 DDS 通道，确认 `_n`/`_s`、alpha、gloss/specular 的显示行为；建立资产 profile，比较旧版外观。
4. 与 SlaveTatsNG 以及另一 overlay 使用者同时分配、修改和清理；外部改写自有槽时不删对方值，容量不足不越界。
5. 在渐隐/切换存档/演员卸载与删除/高频重建中确认 generation 拦截、队列上限、回调不重入、无悬挂指针和锁等待。
6. 测量多演员渐隐时每帧成本、RaceMenu 全量应用与精确 alpha 更新的差异，以及纹理实际加载次数，再决定是否引入更细的应用路径或协程封装。
