# 莱万汀火焰长刀与提弗洛斯弓：定向资源调查

日期：2026-10-13。所有工作目录与游戏输入均在 G 盘。只读检查本机当前客户端 `G:\Arknights Endfield`；未启动游戏、Unity、服务器或安装模组。使用 endfield-resource-manifests 的当前覆盖层原则：StreamingAssets → Persistent。没有全量解包，没有计算产物哈希。

## 结论

**两把武器都有独立的原生 prefab、网格和武器自身骨架，资源并非仅存在于原角色身体模型中。可以把它们列为跨角色观赏武器的候选，无须把莱万汀或提弗洛斯的整个人物模型一起搬过去。**

这里确证的是当前模型、骨骼、材质与贴图引用可被离线读取。挂到其他角色并正常显示是由结构支持的实现推断，尚未实机验证。火焰生成/消失、完整粒子与材质状态控制、弓臂变形/弓弦拉动，以及动画中挂点移动仍须单独接管，不能宣称加载 prefab 即完整还原。

用户所称“提夫罗斯”在本地已验证角色目录中为 **提弗洛斯 / Typhoea / `chr_0034_typhoea`**。莱万汀为 `chr_0016_laevat`。

## 当前资源证据

当前 manifest：`2954fa80-23c1-1579-2b22-4ecfd6d70418`，从本机 G 盘当前 VFS 提取。历史 BE 研究记录仅用于找线索，下列两把武器又以当前 VFS 复核。

| 对象 | 独立 prefab | LOD0 网格 | 顶点 / 索引 | 类型 / 骨骼 |
| --- | --- | --- | --- | --- |
| 莱万汀刀身 | `wpn_misc_0016` | `S_wpn_misc_00016_02_lod0` | 3070 / 8880 | SkinnedMeshRenderer / 1 根 `wep_jian_jnt` |
| 莱万汀环部件 | 同上 | `S_wpn_misc_00016_01_lod0` | 3859 / 10680 | SkinnedMeshRenderer / 1 根 `wep_quan_jnt` |
| 莱万汀 VFX 蒙皮部件 | 同上 | `S_wpn_misc_00016_03_lod0` | 2953 / 11940 | SkinnedMeshRenderer / 38 根武器、spring、chain 骨骼 |
| 提弗洛斯弓身 | `wpn_misc_0051` | `S_wpn_misc_0051_01_lod0` | 21019 / 52296 | SkinnedMeshRenderer / 9 根弓骨骼 |
| 提弗洛斯弓弦 | 同上 | `S_wpn_misc_0051_02_lod0` | 184 / 540 | SkinnedMeshRenderer / 3 根弦挂点 |

精确 prefab 路径：

- `assets/beyond/dynamicassets/gameplay/prefabs/weapons/wpn_misc_0016.prefab`
- `assets/beyond/dynamicassets/gameplay/prefabs/weapons/wpn_misc_0051.prefab`

莱万汀刀身位于 `wpn_misc_0016/Mesh_Sword/lod0/S_wpn_misc_00016_02_lod0`，使用武器自己的 `wpn_misc_0016/Root/wep_quan_jnt/wep_jian_jnt`。环与 VFX 分别在 `Mesh_Ring` / `Mesh_VFX`。这支持先独立保留刀身网格，把武器根挂在其他角色手上的路线；VFX 的 38 骨链不应硬绑定为角色同名骨骼。

提弗洛斯弓身和弓弦位于 `wpn_misc_0051/Mesh_all/lod0/`。武器根为 `Root/wpn_bow_01_jnt`；弓臂 `wpn_up_bow_*` / `wpn_dn_bow_*` 是武器自身子骨骼。弦用 `slot_up_string_01_vfx`、`slot_dn_string_01_vfx`、`slot_M_string_01_vfx` 三根骨骼。因此持弓手只需驱动根挂点，而弓的形变仍要保留并驱动这些自身骨骼。离线数据中不同 LOD 的弓身九骨顺序不同，不能把 LOD0 数字索引表套在所有 LOD 上。

## 材质与火焰依据

莱万汀刀身材质 `M_wpn_misc_0016_01`：

- 原生 CharacterNPR shader 路径；颜色/法线/发光/金属贴图 `T_wpn_misc_0016_01_D` / `_N` / `_E` / `_P`。
- `_VFXSpecialBlendTex = T_fx_mask_531_M`，`_VFXSpecialMainTex = T_fx_flow_520_M`。
- VFX 蒙皮部件另用 `M_fx_laevat_hx_321`，Shader 为 `vfxbasev2.shader`，并引用 `T_fx_flow_808_M` / `T_fx_mask_524_M`。

精确配置资产：`assets/beyond/dynamicassets/gamedata/gameplayconfig/vfxconfig/enemy/vfx_character_laevat_wep_sword_asset.asset`。本轮 TypeTree 读取确认 `useCustomRenderer = 1`，`customRenderers.rendererNameRegex = S_wpn_misc_00016_02`，`type = 12`。这把上述刀身与莱万汀火刀效果配置直接关联起来，不是仅凭编号 0016 猜测。

`p_laevat_skill_ult_wep_01_sword_start_asset.prefab`、`fire_start_asset.prefab`、`sword_light_asset.prefab` 本身是效果控制器，没有 MeshRenderer 或 SkinnedMeshRenderer。`sword_start` 的 TypeTree 中是 `effectEntityRenderVfxCfg.vfxNames` 字符串、0.6 秒 duration 等控制配置。**只拿这些效果 prefab 无法取得刀身。** 与同名 `/effects/vfx/*.asset`、VFX 配置及武器 renderer 的组合需进一步还原。

弓身材质 `M_wpn_misc_0051_01`：D/N/P 原生武器贴图，另有发光 `T_wpn_misc_0047_01_E` 和 `_ParallaxTex = T_fx_flow_47b_M`。弦用特效材质 `M_fx_typhoea_objectline_001_zjm`，具有 dissolve / flow / mask 纹理，显隐效果并不是普通不透明细杆。

NativeAssetReader 将 shader 当元数据读取，没有解析 shader 字节码；图谱中 shader 引用的 `resolved` 可为 false，但路径可辨。不能把网格、骨骼、材质和纹理的离线引用完整报告扩展成“全部 shader 和完整 VFX 已验证渲染”。

## 对“是否只能原角色”的判断

- **已证实：** 这两把模型是独立武器资源，内部蒙皮 palette 只引用武器自己的骨骼，不要求莱万汀或提弗洛斯的人体蒙皮 palette。
- **有实现依据的推断：** 保留其武器根、网格、材质和子骨架，可以在展示模式加载为纯视觉对象并按任意角色的手挂点驱动。现有 BE 的模型克隆工具可作为接入起点，但本轮没有克隆技能实体验证生命周期。
- **需要另外适配：** 握柄姿势、尺寸/朝向、双手位置、刀鞘/收纳表现、弓拉弦动画、火焰和材质状态、各 LOD 和退出恢复。
- **尚未知：** 无原角色技能驱动时能否直接显出全部预期火焰、刀链或弓弦光效；本轮没有读全技能事件时序或运行游戏。模型独立与完整演出独立是两项验证。

建议太刀首先选刀身单骨部件，保持原生材质，再逐步加入火焰；弓保持九骨弓身和三骨弦组成一个独立武器骨架。开发时不要为显示武器去释放原角色大招或启动原战斗技能，展示模块自己管理纯视觉实例及效果时间线。

## 检查记录

- 三批当前定向提取：5 目标 / 161 bundle（两角色与三效果控制器），4 目标 / 43 bundle（弓与配置），1 目标 / 16 bundle（莱万汀武器）。批间有公共依赖，不能把上述相加当作不同 bundle 总数。
- 三次 NativeAssetReader 分别为 2553 / 200 / 271 个对象，backend_issues 均为 0。
- 两个武器的受支持 Renderer 引用完整：弓 8 个（2 部件×4 LOD），莱万汀 12 个（3 部件×4 LOD）；runtime_layout_observed 均为 0。
- 自动图谱解析器对首批三个效果 prefab 的大小写 `P_` / `p_` 有 missing_resources 区分误报；原始图谱与 TypeTree 已证明根存在，属于空 Renderer 控制器，不是 bundle 缺失。
- 本轮输出均在此目录。`special-weapons-evidence.json` 是小型精简证据；`special-weapon-model-parsed.json` / `laevat-weapon-parsed.json` 是当前武器引用图谱；`special-weapon-config-trees/` 是控制配置 TypeTree。大型当前原始图谱和少量 bundle 仅留本机研究目录，不加入模组发布包。

线索来源：`G:\Better Endfield\research\custom_model\1.5.3\Windows\weapon-ultimate-bem\weapon-conversion\README.md` 中的弓身份证据；当前复核工具 `tools/CustomModel/extract_native_bundles.py`、`tools/CustomModel/NativeAssetReader/Program.cs` 与 `tools/CustomModel/parse_native_models.py`。
