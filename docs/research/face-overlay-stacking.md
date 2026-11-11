# Face 一层与五层视觉叠加调查

日期：2026-11-11；程序基线：1.0.7 / `b452ec1`。来源：用户提供的 NPC 对比截图、旧 Papyrus 脚本、本地 RaceMenu 源码、DDS 分析及生产适配器的桩接口探针。本轮没有游戏渲染复现。

## 结论

没有发现 C++ 把五层压成一层的证据。五槽条件下，生产适配器能分别写入五个 Face 节点的覆盖记录。旧脚本同样使用多个 Face 槽；当前脸部资源与旧版完全一致。因此应继续区分实际节点生成、面部材质/透明度、绘制顺序和资源可见范围。

菜单五层是逻辑状态；适配器 `Applied/5` 是五个槽位的提交结果。两者都不能单独证明游戏里五个面部几何节点已正确渲染。

## 已确认事实

### 旧脚本也使用多个 Face 槽

旧版 Oral 分配独立的六槽 Face 区间，每区业务上限五层，使用 `head` 贴图。其公共更新路径与 Body/Hands 一样，通过 NiOverride key `9` / index `0` 写 diffuse，通过 key `8` / index `-1` 写透明度，再请求节点更新；没有面部单层合成逻辑。脚本本身不决定引擎怎样混合这些面部几何。

源码：[Oral 路由](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:575>)、[六槽 Face 区间](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:1081>)、[纹理更新](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:722>)、[刷新](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:828>)、[五层上限](<E:/source/repos/SLACS2 p+ 0.7/Source/Scripts/SLACS_Main.psc:44>)。

### 当前适配器可写五个不同 Face 槽

使用真实 `OverlayAdapter.cpp`、现有 overlay stubs 和五个 Face 槽，目标从一层增长到五层，结果为 `Applied / slotsApplied=5`：

| 节点 | diffuse | 其他写入 |
| --- | --- | --- |
| `Face [Ovl0]` | `Oral/head1.dds` | 对应 `_n.dds`，alpha=1 |
| `Face [Ovl1]` | `Oral/head5.dds` | 对应 `_n.dds`，alpha=1 |
| `Face [Ovl2]` | `Oral/head4.dds` | 对应 `_n.dds`，alpha=1 |
| `Face [Ovl3]` | `Oral/head3.dds` | 对应 `_n.dds`，alpha=1 |
| `Face [Ovl4]` | `Oral/head2.dds` | 对应 `_n.dds`，alpha=1 |

旧层保留已有槽位，新层分配剩余槽位。Face 写 diffuse index `0`、normal index `1` 和透明度，不写 FaceGen 专用 index `2`。Core 的 Face 资源池有 12 张，五层内会优先选择未使用的贴图索引。

探针以 `clang++ -std=c++23` 编译实际适配器及 `Settings.cpp`，使用 `tests/overlay_stubs`，运行退出码 0。它验证 CPU 提交路径，不验证目标机器的 RaceMenu ABI、节点生成或 GPU 混合。源码：[Face profile](<E:/source/repos/SLACS2 NG/src/integration/OverlayAdapter.cpp:41>)、[槽分配](<E:/source/repos/SLACS2 NG/src/integration/OverlayAdapter.cpp:623>)、[材质写入](<E:/source/repos/SLACS2 NG/src/integration/OverlayAdapter.cpp:979>)、[纹理选择](<E:/source/repos/SLACS2 NG/src/core/Core.cpp:57>)。

容量不足仍是可区分的分支：可用槽少于逻辑层时只提交可用数量，并报告 `NoOwnedCapacity`。此次截图中的核心逻辑上限提示不能当作 RaceMenu 容量不足的证据。

### 资源与旧版一致，并非五张相同图片

将当前 `package/Data/Textures/slacs2/Oral/` 下全部 **60 个 DDS** 与旧版 `Textures/slacs2/Oral/` 同名文件逐个比较 SHA-256，全部一致。Pillow 解码 DXT5 后，NumPy 分析普通 `head1..12` 得到：

| 检查 | 结果 |
| --- | --- |
| 普通 diffuse 与 alpha 遮罩 | 12 组均不同 |
| `head1` 非零 alpha 覆盖率 | 整张 UV 图像的 0.1470566% |
| `head1..5` 按 `1 - ∏(1 - alpha)` 合成后的非零覆盖率 | 整张 UV 图像的 6.2349558% |
| `head7` diffuse 最大 alpha | 122/255 |

这说明增加不同贴图在二维模型下会扩大覆盖范围。上述百分比不是角色脸部的可见面积；UV、镜头和材质仍影响实际显示。若对原始纹理 alpha 使用 128 的裁切阈值，`head7` 会完全被裁掉，但 **128 不是已知用户配置，也不是所读 RaceMenu 源码的默认值**。

文件示例：[当前 head1](<E:/source/repos/SLACS2 NG/package/Data/Textures/slacs2/Oral/head1.dds>)、[旧版 head1](<E:/source/repos/SLACS2 p+ 0.7/Textures/slacs2/Oral/head1.dds>)、[当前 head7](<E:/source/repos/SLACS2 NG/package/Data/Textures/slacs2/Oral/head7.dds>)。

### Face 的几何入口与身体不同

本地 RaceMenu 的 Face 安装路径选择头部 `FaceGen` shader 几何，使用 `face_overlay.nif`；身体选取 `FaceGenRGBTint` 来源，使用对应身体/手部 NIF，之后共用安装器。安装器复制几何/蒙皮数据、设置 decal 标志并应用覆盖记录。这确认了面部有独立入口，但尚不能据此判定渲染故障。

SLACS 的透明度 key `8` 修改材质 alpha，不控制 `NiAlphaProperty` 的 alpha test。RaceMenu 另从 `[Overlays/Data]` 读取 `bAlphaOverride`、`iAlphaFlags`、`iAlphaThreshold` 并用于安装节点。所读源码默认分别为 `true`、`4845`、`0`；flags 4845 的 alpha-test 位已开启，阈值为零。目标游戏实际生效的 NIF 和这些属性尚未读取。

源码：[Face 安装](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:544>)、[身体安装](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:653>)、[alpha 属性](<E:/source/repos/RaceMenu/skee64/OverlayInterface.cpp:242>)、[默认值](<E:/source/repos/RaceMenu/skee64/main.cpp:130>)、[配置读取](<E:/source/repos/RaceMenu/skee64/main.cpp:783>)、[alpha-test 位](<E:/source/repos/SLACS2 NG/lib/commonlibsse-ng/src/RE/N/NiAlphaProperty.cpp:10>)。上游参照：[RaceMenu 安装实现](https://github.com/expired6978/SKSE64Plugins/blob/master/skee64/OverlayInterface.cpp)、[CommonLib alpha 属性实现](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/src/RE/N/NiAlphaProperty.cpp)。本地 provider 源码不是测试者实际 DLL 的版本证明。

另有 [RaceMenu issue #58](https://github.com/expired6978/SKSE64Plugins/issues/58) 报告 Skyrim 1.6.1170 / RaceMenu 4.19.16 的覆盖层顺序反向，涉及全部区域。维护者以“not planned”关闭，未给出根因；这是绘制顺序线索，不能证明此次 Face 异常。

## 下一次实机对照

固定同一 NPC、镜头、光照与 RaceMenu 配置，仅操作 Face：

1. 记录一层到五层的逻辑数量、`slotsApplied/requested`、Face 总容量和每个槽的覆盖记录。
2. **另外统计真实 3D 中的 Face 几何节点，读取节点实际使用的纹理、shader 和 alpha 属性。** `GetNodeOverride` 读取的是保存的覆盖表，五条记录不能代替五个实际节点；应核对安装节点与记录是否一一对应。参照：[覆盖记录查询](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:128>)与[当前节点属性查询](<E:/source/repos/RaceMenu/skee64/OverrideInterface.cpp:766>)。
3. 分别单独显示五张贴图，再把同样五张一起显示。之后以带不同编号、颜色的诊断 DDS 重做；使用空间分开的标记检查是否都能出现，使用重叠标记判断上下顺序。
4. 若实际节点不足，检查安装入口、头部模型与容量；若节点/材质齐全但原图差异小，对照 UV/alpha；若诊断图也丢层或顺序异常，再定位透明混合、深度/decal 与绘制顺序。

此对照尚未执行。
