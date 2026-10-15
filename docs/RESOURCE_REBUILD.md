# 素材重建与离线交付

命令在仓库根目录运行，使用合法取得且匹配版本的本地素材。默认不计算产物哈希。原资源、解包索引、Blender 工具和烘焙数据保留本地；源仓库只保存实现、配置与来源说明。

## 输入与工具

| 输入/工具 | 路径与要求 |
| --- | --- |
| 怪猎世界/冰原 | `<怪猎安装目录>`，读取 `chunk` 原版与补丁有效覆盖 |
| 终末地客户端 | `<终末地安装目录>`，本期源 manifest 为 `2954fa80-23c1-1579-2b22-4ecfd6d70418` |
| BE 导出资料 | `<BE资源工具目录>`，当前有效 VFS、角色表、NativeAssetReader 及保留引用的对象图 |
| Blender | `local/tools/blender/blender-4.5.14-windows-x64/blender.exe`；MHW Model Editor 与 MHW Anim Tools 放在本地 tooling 路径 |
| Python | 3.12，NumPy、SciPy、Pillow；脚本各自读取本地格式工具和依赖路径 |
| 音频 | `local/tools/vgmstream/vgmstream-cli.exe` 与本地 wwiser；从 Bank/HIRC/Media 解码，不触发游戏事件 |
| Windows 构建 | CMake ≥3.25、Visual Studio 2022 C++ x64，`third_party/include` 的 EML ABI 1/format 1 SDK 与 JSON 头 |
| 网页检查 | Node/npx、`@playwright/cli`、本机 Edge；只读本地 HTTP 用于资源加载 |

工具和原游戏素材不在源码 Git 中。先按 [资源工具链准备](RESOURCE_TOOLCHAIN.md) 获取已核定的 BE、Endfield 与 MHW 解析器固定源码及补丁，构建 Reader/Inspector/ResConv/ooz，并运行 `python tools/check_resource_toolchain.py --be-root '<BE资源工具目录>'`。新机器还需 [第三方来源说明](../THIRD_PARTY_NOTICES.md) 所列工具与匹配版本资料。部分脚本仍有本地绝对路径、tooling 目录和缓存约定；不能把源码来源核证、预检通过或复制 Git 等同于完整素材重建。

## 数据流水线及边界

怪猎有效 chunk 索引由 `tools/mhw_extract_original.py` 生成；`tools/mhw_multi_extract.py` 按覆盖索引定向提取其余模式，支持重复 `--select` 正则。完整原版太刀、pl043 身体/鞋底/手模型和六模式附属武器也需要原始资源选择，不借其他游戏或重绑骨架代替。

`tools/mhw_multi_bake.py` 在 Blender 后台求值 21 主骨、左右手/挂点、手指和源姿势；缺层组件使用明确记录的原版 idle 上下文。`tools/mhw_multi_ground.py` 用真实源鞋底网格/权重生成接地轨道，保留原 LSM 髋部参考。源武器模型由 MOD3/MRL3 读取，UV/纹理由 `tools/weapon_texture_decode.py` 定向转换。

源控制/派生证据包括 FSM、MBD、COL 作者招式名称及 LMT TIML。音频按 `TIML → WWEV ReqNo → Wwise HIRC → Media → PCM` 发布。部分原生 C++ 控制分支已有来源核证（见 [控制来源核证](implementation/combat-source-closure.md)）；完整原战斗控制器、Wwise RTPC／容器选择和武器 carry 状态未重放；未找到源 own-skin 触发的动作保留规范化绑定姿势，不补猜测动画。

`tools/endfield_profiles.py` 和 `tools/endfield_weapon_profiles.py` 读取 BE 的完整引用图；当前 1.6.3 为 34 角色、84 装备和两把特殊模型配置，集合以当前三表与索引为准。主骨、30 指骨、鞋底 probe、按 renderer 的 bind 注册矩阵分别保存。`tools/weapon_reference_decode.py` 按真实 RefTypes 完整解码角色 CharacterData 的 `ReferencedObjectData`；禁止省略后再把错位字段当挂点证据。

角色生成先在 `output/resource-staging/` 准备整批 profile、rig、geometry 和索引，所有导出与最终集合检查成功后才替换；准备失败保留旧产物，发布中的可处理异常回滚已替换文件。它不是跨目录文件系统的断电原子性保证。纹理重跑按完整源资源路径区分同名文件，并把当前尺寸、PNG、RGBA 和元数据一起发布；旧 RGBA 存在不再跳过像素更新。

`build_product_catalog.py` 同样把 WEBP 参考图、源预览和 provenance / product / audio 三个目录 JSON 放入同一批事务。任何准备或末尾发布失败都会保留上一批完整产物，避免目录版本已更新而声音索引仍是旧版。

精确重定向 oracle 直接调用当前 `native/retarget.h`，由 `tools/endfield_mesh_audit.py` 扫描当前 204 个角色×模式组合并产图；`tools/endfield_special_mesh_audit.py` 另扫描全部 68 个角色×特殊武器组合。P0 使用每个核心帧，其余为默认 stride 12 加片段边界；这是网格数值检查，原生 shader、布料/链条动态、P0 动态装配、场景和实际输入/声音留待游戏检查。

## 从原始资源重建六模式

以下是现有素材工作区的流水线记录，依赖 ignored 本地格式工具、对象图和缓存。它不是新克隆的自动从零构建脚本；先逐个确认输入已准备齐全。

以下流程会重写本地烘焙资料，应在角色/挂载人工校准已写入脚本和版本控制后执行。所有原动作、COL 名称、上下文和不确定项由对应发布脚本保留，禁止重建后手动改成“原生已确认”。

```powershell
$mheBlender = Join-Path (Get-Location).Path 'local/tools/blender/blender-4.5.14-windows-x64/blender.exe'
python tools/mhw_extract_original.py --select '(^|/)hm/wp/wp03/' --select '(^|/)sound/.*wp03' --select '(^|/)wp/(w03|swo)/' --select '(^|/)pl/m_equip/pl043_0000/(body|arm|leg)/'
python tools/mhw_multi_extract.py
& $mheBlender --background --factory-startup --python tools/mhw_bake_original.py
python tools/mhw_publish_baseline.py
& $mheBlender --background --factory-startup --python tools/mhw_multi_bake.py
& $mheBlender --background --factory-startup --python tools/mhw_multi_ground.py
& $mheBlender --background --factory-startup --python tools/mhw_multi_weapon_tracks.py
python tools/mhw_multi_collision.py
python tools/mhw_multi_audio.py
python tools/mhw_multi_publish.py
python tools/weapon_texture_decode.py
& $mheBlender --background --factory-startup --python tools/mhw_source_mount_metadata.py
python tools/mhw_finalize_modes.py
& $mheBlender --background --factory-startup --python tools/mhw_complete_core_fingers.py
python tools/mhw_prop_skin_tracks.py
python tools/mhw_verify_greatsword_bind_fallback.py
& $mheBlender --background --factory-startup --python tools/mhw_verify_own_skin.py
```

多模式发布前如需刷新 FSM/MBD 辅助证据，再运行 `tools/mhw_multi_controls.py`；旧太刀另有 `mhw_decode_fsm.py`、`mhw_decode_mbd.py`、`mhw_fsm_refs.py`。作者 COL 名称是实际招式依据，不用连续槽位猜气刃、蓄力或突刺。`tools/data/mhw_longsword_moves.json` 保留当前有依据的太刀映射。`mhw_finalize_modes.py` 为可重复执行的末端发布处理，统一默认键、合成动作音频、排序和命中窗口；省略它会丢失末端修正。

顺序有实际依赖：先生成历史太刀组合 recipe，再从六模式源数据拼装同一阶段；鞋底求值保留原 LSM 髋部基准；源挂点与模型规范化在纹理解码后更新；补齐核心手指及弓拉弦标记后，按原始 weapon motion bank 生成 LSK 并直接对照 Blender 逐帧求值。源 own-skin 缺少触发的片段保留明确静态回退，不能据此证明原生 carry 状态也静止。

大剑三段持姿和九种释放的原 TIML 均没有自身动画触发；`mhw_verify_greatsword_bind_fallback.py` 直接核对原 LMT/TIML 与 MOD3 bind，出版显式回退记录，不制造 LSK。具体证据与边界见 [大剑蓄力源资料](implementation/greatsword-charge-source.md)。控制器改动后重编译 `source_demo_timeline`，运行 `python tools/mhw_refresh_source_tours.py` 刷新六模式有声源影片，再运行 `python tools/verify_source_tours.py` 全程解码；不能把旧影片当成最新 F9 的播放结果。

### 两把特殊武器的原声

```powershell
python tools/endfield_extract_weapon_audio.py
python tools/publish_endfield_weapon_audio.py
python tools/verify_endfield_weapon_audio.py
```

定向读取当前有效 VFS 的角色事件配置、匹配 Wwise Event/Bank/HIRC 与 Media，按真实默认 Switch 与 Random 播放列表出版 8 条 PCM 和 `assets/audio/theme_cues.json`。发布后校验原始解码 PCM 的字节一致以及事件/变体闭包。原火焰有限垫音由模组按展示生命周期重复，弓在本地蓄力/释放标记播放；不调用游戏音频事件，也不声称复现原技能完整 RTPC、原混响与加权随机。依据和限制见 [特殊武器原声](implementation/special-weapon-audio.md)。

### 终末地配置与实际网格

先用 BE 导出器更新匹配 manifest 的角色/武器对象图、蒙皮/网格/材质引用和 CharacterData 的 raw/RefTypes；这些导出不需要进游戏。已具备导出资料时：

```powershell
python tools/endfield_profiles.py --input-root "<当前版本 Windows 输入目录>" --game-version 1.6.3 --graphs "<当前角色对象图目录>" --overlay-report "<当前 VFS 直接字节核对报告>" --table-report "<当前有效三表核对报告>" --export-geometry
python tools/endfield_weapon_profiles.py --game-version 1.6.3 --manifest "<当前manifest.json>" --raw "<当前装备对象图>" --geometry "<当前装备几何目录>" --overlay-report "<当前装备提取报告>"
python tools/weapon_reference_decode.py
cmake -S tools/offline_retarget -B build/offline-retarget -G 'Visual Studio 17 2022' -A x64
cmake --build build/offline-retarget --config Release --clean-first
python tools/endfield_mesh_audit.py --sole-only
$mheRoot = (Get-Location).Path
& 'build/offline-retarget/Release/endfield_retarget_oracle.exe' $mheRoot (Join-Path $mheRoot 'research/character-coverage/oracle')
python tools/audit_oracle_provenance.py --rebuild-compare
python tools/endfield_mesh_audit.py
python tools/endfield_special_mesh_audit.py
python tools/endfield_special_previews.py
```

`mhw_publish_baseline.py` 首次发布只需要原版烘焙 staging，不读取或要求旧 `assets/catalog.json`。合成在临时副本进行，不反写原始 baked 输入；默认排除被旧发布器写入 staging catalog 的 `Legacy` 条目。升级时只有显式传 `--preserve-existing-legacy` 才保留旧发布 catalog 中已标明 Legacy 且文件真实存在的条目，保持原来源，不把未知旧动作改标 Legacy。干净原版首发不自动制造历史四个 Legacy 槽，不能仅凭原版首发命令宣称重建了含它们的历史 1185 槽候选；使用已有 bake 副本的首发验证也不等于从游戏全新烘焙。

原生包/类型布局变化时先重导出、核对完整引用解析，再生成 profile。角色每个 renderer 的坐标注册需分别保留；不能把一个全局旋转重复应用到已转成 Y-up 的头部。重点动态挂点的最新配置来源和未决项看各 `profiles/weapons/*.json`，不要用重新生成的静态默认挂点覆盖已确认的专项证据。

两种 mesh 扫描都会保存 schema 2 `input_contract`：直接记录运行相关 profile 值，并记录实际图、geometry、源轨道、rig、oracle、实际 solver / oracle 二进制和完整本地数值代码链的路径、大小、纳秒修改时间；包括 `analyze`、`Graph`、矩阵实现、`native/core.h`、oracle/solver C++、CMake 和 JSON 头。另记录 CPython 版本/实现/位数/平台、NumPy/SciPy/Pillow 版本及 OpenBLAS 标识，排除构建机临时路径；special 另记录特殊图、网格和完整武器 profile。无需哈希，也不构成密码学完整性证明。修改时间被手动伪造且文件大小不变时不保证发现；普通重生成或参数改变会保守失效。旧报告不能补贴当前合同冒充重新验证，`--resume-log` 也必须核对原已保存合同。扫描期间不要重写这些输入。

全量扫描可在重建当前 solver / oracle 并重新生成 oracle 后，改用 `python tools/run_mesh_audits.py --workers 4 --output output/mesh-audit-current`。输出目录必须为空；每角色只有一个 worker，P0 仍扫所有核心帧，其他角色仍为 stride 12 加边界。各 worker 的 `--report` / `--gallery` 独立落盘；严格核对 204 / 68 集合及全部当前合同后才成批替换两份正式覆盖报告，任一失败保留之前报告，不降低精度或补贴旧结果。

扫描编排器、目录生成器和公共/私有打包共用 native 构建时间门禁：实际选择的 solver DLL 与 oracle EXE 都不得早于声明的 C++/CMake/JSON 头输入，缺失或过期时要求 `--clean-first` 重建。该时间检查同样不是密码学来源证明；它用于阻止忘记重编译后重新审计旧算法。若存在 `build/offline-retarget-dual/Release/endfield_pose_solver.dll`，实际加载优先使用此目录，重建和生成 oracle 时也必须使用同一目录。

此外，`audit_oracle_provenance.py --rebuild-compare` 用当前 EXE、实际输入和 profile 字段在独立临时目录重生全部 JSON/LTP，逐字节等于已审计缓存后才写 `oracle-generation-proof.json`，不改已审计的 oracle 文件。该独立证明绑定真实生成输入、二进制和输出身份；后续 runner/catalog/公共与私有打包都要求它仍有效。仅重编译 native 后沿用旧 oracle，或更新 LSM/LSF/rig/profile 后只重跑网格扫描，都不能被当成新生成结果。首次准备先运行 `--sole-only`，让真实 mesh foot-height 在生成 oracle scale 前进入 profile；重复审计写入相同数值不因 profile 文件时间变化被误判。

完整产品/打包要求普通和特殊组合均明确通过且 `errors == []`，当前角色集合完整，实际输入合同仍一致。存在旧参考图、profile 文件或空缺状态都不能替代资格；`failed`、`pending`、遗漏 errors、任意非空错误和过期输入均拒绝。产品保留一份角色 profile 输入快照，特殊角色行引用该快照，避免重复大块数据；打包前仍从研究目录逐个核对 204 + 68 份真实合同。

## 编译、检查与打包

已有素材，或完成上述重建后，在 PowerShell 中执行：

```powershell
cmake -S . -B build/development -G 'Visual Studio 17 2022' -A x64
cmake --build build/development --config Release
ctest --test-dir build/development -C Release --output-on-failure
python tests/test_package_validation.py
node tools/ui_verify_rules.cjs
python tools/build_product_catalog.py
python tools/package.py --package-root build/development/package --validate-only
python tools/package.py --package-root build/development/package
```

先修改 `module.json` 的候选版本（CMake 自动读入并嵌入 DLL），再构建/生成目录/打包。产品目录与 `assets/build_provenance.json` 的版本必须与 ZIP manifest 相同。未通过检查时打包器拒绝生成包，不通过删缺失招式来掩盖缺口。

包包含 LSM 身体、LSG 握持、LSF 手指、LSK 源武器 own-skin、地面/源挂点/命中/发射标记、PCM 和源模型/纹理，以及网页/配置/Markdown 文档。发行时六模式的 1185 个完整 LSG 文件合并到六个 LGB 银行，源挂点 JSON 的实际运行字段合并到六个 LSW 银行；源工作区的可编辑文件保留原格式。打包器校验路径、轨道帧/FPS/调色板、声音用途/时间界限、命令/hold/releases、六模式及当前角色／装备索引集合及全部网页图依赖。研究文件路径表示来源证据，原始解包研究不会作为运行依赖搬进 ZIP。

`tools/mhw_package_banks.py` 的 `prepare(root, package_root)` 生成发行覆盖文件，也可单独运行 `python tools/mhw_package_banks.py --package-root build/development/package`。LSW1 银行头为 `<4sIII>`（magic、version=1、轨道数、每帧 152 字节），帧为 `<36fII>`，保留原 native 使用的 quaternion/support 与主副手/挂点原浮点，附件相对变换仍由原 C++ 计算。LGB1 银行头为 `<4sIII>`（magic、version=1、轨道数、0），载荷复制每份完整原 LSG1 文件；offset/size 和 source_file 来源记录在小 manifest。数值按原 native 的 binary32 精度保存，没有量化、裁帧或减少动作。转换器逐个浮点位模式、valid 标记和 LSG 字节进行精确回读，检查偏移无空洞、无重叠、结尾精确一致；结果写入 `build/development/runtime-bank-report.json`。当前源挂点 JSON 共 159,358,431 字节，发行 LSW 共 16,306,200 字节，LGB 共 1,735,488 字节。

## 网页与真实 EML 隔离检查

只读 HTTP 服务在项目根目录提供 `127.0.0.1:8877`，运行 `tools/ui_browser_verify.cjs` 和 `tools/ui_catalog_browser_verify.cjs`。前者在独立页面注入显式 `?mock=1` 测试桥接；生产页面没有测试宿主。后者读取实际目录、PCM、源姿势和所有当前角色×模式的目标网格入口，检查资源依赖与默认键位。`tools/ui_final_controls_verify.cjs` 另验序列/循环/本地箭弹速度保存与全部 8 条特殊音源试听，`tools/ui_source_tours_verify.cjs` 检查六份实际源网格有声 MP4。网页/原生接口 mock 不能代替 EML 本身。

最终 ZIP 还要通过真实 EML `ModuleStore` 的隔离导入和 `OfflineHost` 的初始化、配置/状态/模式/暂停/停止回执；做法见 [EML 离线宿主验证](implementation/eml-verification.md)。使用独立临时 module store，不写用户现用安装、游戏或服务端。浏览器/HTTP/OfflineHost 结束后关闭该记录创建的进程。

运行日志放 `build` 或 `output`，网页截图放 `output/playwright`。默认以路径清单、文件尺寸、二进制维度和直接数据核对验证，不生成产物哈希。原游戏资源、媒体、网格及大型输出维持 Git ignore。
