# 1.6.3 资源适配记录

2026-10-15。本轮只读本地客户端与 VFS，没有启动游戏、服务端或发布包，没有计算产物哈希。运行时 Hook 与宿主检查由本轮其他记录说明。

## 当前来源与差异

有效覆盖顺序为 StreamingAssets → Persistent。当前三张表重新从 VFS 提取并解码：CharacterTable 35 行（含占位管理员）、WeaponBasicTable 84 行、I18nTextTable_CN 164802 行，均来自 Persistent，完整解码值与版本化输入相同。两版本 manifest 的 Version GUID 相同，故版本复核使用当前 asset 依赖闭包、实际解码字节与对象图，不能仅比较 GUID。

新增可玩表角色为祀 `chr_0036_jsspsi`，默认双手剑。明河的三个 PrefabInfo 条目不在可玩表，不作为支持角色。新增装备为 `wpn_claym_0018`、`wpn_claym_0019`、`wpn_sword_0021_ink`、`wpn_sword_0024`。

34 角色重新读取当前骨架与几何。女管理员有绑定/挂点微小浮点变化；昼雪、黎风、别礼、汤汤的绑定或实际网格有变化；另外 28 个旧角色的骨架与完整几何 payload 精确相同。五名变更角色与祀重扫六模式，太刀在新接触 mask 发布后再次重扫。204 个普通组合由 36 项重扫及 168 项实际输入等价复用组成；68 个特殊组合由 12 项重扫及 56 项等价复用组成，均保持 `runtime_verified=false`。

84 件普通装备重新提取 330 个 bundle，实际根、网格、palette/bindpose 与几何齐备，含 260 个 SMR 和 393 个静态 renderer。两把特殊模型另行提取 52 个 bundle，其变换、renderer、mesh 结构及火刀 3 个、弓 2 个 LOD0 网格与历史数据精确相同，保留原专项挂载修正。

## 可复现入口

以下路径变量由本机填写；原始资源和结果必须留在 ignored 目录。输出目录要求全新，避免旧 bundle 混入。Python、NumPy、SciPy、项目的 EndfieldWeaponInspector 和 BE 资源工具须已准备。

```powershell
$mheGame = '<当前终末地安装目录>'
$mheResourceTools = '<BE资源工具仓库>'
$mheInputs = '<BE资源工具仓库>/inputs/1.6.3/Windows'
$mheCurrent = 'research/client-update-1.6.3/rebuild-current-inputs'

python tools/refresh_endfield_profile_inputs.py --game $mheGame --resource-tools $mheResourceTools --input-root $mheInputs --game-version 1.6.3 --output $mheCurrent
```

提取器会先重新解码角色、装备和文本三表并核对输入，再按当前 manifest 重算每个 prefab 的完整依赖闭包，合并提取并串行读取对象图。缺少表或依赖立即失败。任何后端解析告警会导致默认非零退出，并保留 summary 与 reader.log；已审查的非目标依赖告警可显式使用 `--allow-backend-warnings`，但仍逐项记录 `graph_decode_complete=false`，不将其冒充完整依赖解析。

将每个 `chr_*/raw.json` 以相同角色 ID 保存为 `research/character-coverage/<角色ID>.json` 后，显式生成当前 profile：

```powershell
python tools/endfield_profiles.py --input-root $mheInputs --game-version 1.6.3 --graphs research/character-coverage --overlay-report "$mheCurrent/summary.json" --table-report "$mheCurrent/roster/table-verification.json"
python tools/verify_profile_geometry.py --current-inputs $mheCurrent --report research/client-update-1.6.3/rebuild-geometry-comparison.json
```

生成器没有隐式旧表回退。`--characters` 用于增量生成，校验报告分别记录 registry 总量与实际验证量；当前 source roster 必须仍完整。`--allow-backend-warnings` 只放行已明确审查的依赖告警，主骨与真实蒙皮引用检查始终执行。几何比较遇新增或变化返回非零，表示需要将当前几何迁移到标准目录并重扫；不能为了使比较通过而删掉变化记录。旧额外鞋底顶点和手部修正须先按实际顶点/权重证据保留或重建。

```powershell
python tools/endfield_mesh_audit.py --sole-only
cmake --build build/offline-retarget --config Release
& build/offline-retarget/Release/endfield_retarget_oracle.exe (Get-Location).Path research/character-coverage/oracle
python tools/endfield_mesh_audit.py --characters <新增及变化角色ID列表>
```

太刀接触候选与两种特殊模型需要按同一目标变化名单重算，随后 `publish_longsword_contact_masks.py` 发布完整名单，再重扫对应普通太刀和特殊模型。不要凭新增 profile 数量自动复制接触 mask。全部检查结束后再生成产品目录。

普通装备使用 `refresh_endfield_profile_inputs.py --no-characters --all-equipment` 提取当前共享闭包；为其导出实际几何后运行：

```powershell
python tools/endfield_weapon_profiles.py --skip-special --game-version 1.6.3 --manifest <当前manifest.json> --raw <当前equipment/raw.json> --geometry <当前equipment/geometry> --overlay-report <当前equipment提取summary.json>
```

`--skip-special` 保留火刀与弓的专项校准。普通装备生成器要求当前 roster、当前 asset 闭包和实际 geometry 同时成立，并按 ID 替换 registry，重复生成不会追加重复条目。

## 保留的缺口与验证

安塔尔、洛茜、庄方宜及装备依赖中的默认 MRO 纹理有读取告警；诀存在一条未知 ClassID 告警。目标主骨、renderer/mesh、palette/bindpose 和实际顶点已单独核验，但不宣称完整依赖解析零告警。原生 shader、衣物物理、动态挂载、输入音画与场景恢复仍需游戏内验证。

本轮本地证据位于 `research/client-update-1.6.3/`，包括有效 VFS/骨架/几何比较、三表直接解码比对、鞋底顶点迁移、特殊模型等价与接触/特殊扫描记录。`tests/test_resource_overlay.py` 覆盖当前 asset 改指新 bundle 而旧 bundle 仍存在、依赖增量、缺少 asset 身份、缺失依赖等情况；只有闭包正确和逐字节验证同时成立才输出 `current_vfs_verified=true`。
