# 迁移后的构建与资源基线审计

审计日期：2026-10-13。开发根目录：`G:\Monster Hunter Endfield`。
仅检查现存源码、资源、文档与构建残留，没有编译、启动游戏/服务端、修改运行代码或计算产物哈希。机器可读逐路径结果见 [baseline-audit.json](baseline-audit.json)。

## 结论

迁移已由主代理按清单核对：647 个文件同盘移动，零清单差异。下面的缺失在迁移前已有，不能归因于本次重命名。

**当前源码不能完成原生构建，资源不能完成模块初始化或 EML 打包。** 现存身体动作、源骨架、网页预览、Blender 场景和扩展研究仍有价值，不能把整个项目或所有研究认定为不可用。

关键缺口是 **assets/clip_1000.lsm：catalog 中的 303 帧登龙组合**。网页有该槽位的嵌入预览并不表示可运行的 LSM 文件存在。旧 README 的测试通过描述不是当前文件树已通过验证的证据。

## 实际库存

| 目录 | 文件数 | 字节数 |
| --- | ---: | ---: |
| `native` | 2 | 96,760 |
| `tests` | 1 | 7,791 |
| `third_party` | 0 | 0 |
| `profiles` | 1 | 325 |
| `assets` | 153 | 4,830,075 |
| `preview` | 158 | 27,687,422 |
| `ui` | 4 | 309,272 |
| `tools` | 3 | 16,473 |
| `build` | 48 | 1,577,008 |
| `output` | 0 | 0 |
| `dist` | 0 | 0 |

- native 仅 module.cpp、core.h；tests 仅 core_tests.cpp。
- assets：150 个 LSM、catalog.json、**source_rig.json**；没有 ui_preview.json。
- ui：HTML、CSS、JavaScript 和嵌入预览 4 个文件齐全，是直接加载的静态 UI，不依赖 npm 或 UI 编译。
- build/package、output 为空，dist 不存在。构建树唯一 EXE 是 CMake 编译器识别程序 CompilerIdCXX.exe，不是模组或测试程序。

### 身体动作与源骨架

catalog 有 151 条目，存在 150 个动作文件。对 150 个 LSM 按 native/core.h::ReadClip 格式约束逐字节读取：LSM1 头、21 骨维度、帧数/FPS/文件长度、四元数有限性与归一化容差、髋部位置范围均通过。此检查不证明重定向姿态、武器方向或游戏内效果正确。

缺失：`clip_1000.lsm`（slot 1000，303 帧，catalog 时长约 5.03 秒）。**保留现存 150 个动作，不通过删除 catalog 条目掩盖登龙缺失。**

source_rig.json 存在，21 个主骨位置有效，并保留左右手掌解剖参考；它不是缺失项。

## 构建契约缺失

CMake 明确列出的主模块源码存在；默认 include 所需文件和测试源未齐全。直接 include 缺失 8 个本地实现头：

- `native/retarget.h`
- `native/weapon_pose.h`
- `native/grip.h`
- `native/grounding.h`
- `native/practice.h`
- `native/practice_hud.h`
- `native/audio.h`
- `native/audio_timeline.h`

默认 SDK_INCLUDE=third_party/include 下两个直接需要的头缺失：

- `BetterEndfield/ThirdPartyModule.h`
- `nlohmann/json.hpp`

整个 third_party 目录不存在。可以明确 SDK_INCLUDE 指向已确认的真实 SDK，但不能假设旧构建目录已经正确指向它。

CMake 另列出的 5 个测试源缺失：

- `tests/runtime_tests.cpp`
- `tests/audio_tests.cpp`
- `tests/retarget_tests.cpp`
- `tests/practice_tests.cpp`
- `tests/weapon_tests.cpp`

没有使用替代头、空实现或空测试凑可构建状态；应恢复真实源码后重新验证。

## 运行与打包契约缺失

ReadAssets() 依次读取 catalog、全部 LSM、角色配置、源骨架、源武器轨道、地面轨道、双手握持轨道、命中窗口和动作派生。**它最先会在 catalog 中缺失的 slot 1000 抛错**；补回动作后仍会遇到配置/清单缺失。

逐路径确认的缺失配置与清单：

- `profiles/female_admin.json`
- `assets/source_weapon_tracks.json`
- `assets/ground_tracks.json`
- `assets/grip_tracks.json`
- `assets/hit_windows.json`
- `assets/moves.json`
- `assets/audio/cues.json`

- 没有任何 .lsg 握持文件或 assets/audio 目录。
- moves.json 的 commands 必须引用实际存在槽位，必需 idle 和 slot 306。
- 源武器/地面轨道要求 FPS、帧数逐动作匹配；旋转、支撑权重、地面 clearance、握持轨道和命中窗口另有数值/边界检查。不能用空 JSON 替代真实动作数据。
- ReadAudio() 捕获资源错误并禁用声音，不直接导致初始化失败；**package.py 要求有效 cues、clips 和每个 WAV，因此音效缺失仍阻挡打包**。
- package.py 还要求原生 DLL、角色配置、UI、源骨架及五类重定向/练习数据；目前 DLL、角色配置最先就不满足。
- README 描述的 tools/bake_audio.py 缺失，不能宣称可直接重收集音效。

### UI 仍可用部分与缺口

ui/preview-data.js 是有效嵌入数据，含 17 槽位：1, 9, 11, 101, 102, 103, 104, 105, 106, 107, 108, 127, 128, 306, 307, 308, 1000。静态 UI 源骨骼预览仍可研究；assets/ui_preview.json 是缺失的备用 fetch/重嵌入来源。

HTML 四个试听源全部缺失：

- `assets/audio/367971202.wav`（见切）
- `assets/audio/12607224.wav`（居合）
- `assets/audio/35004545.wav`（登龙）
- `assets/audio/endfield_swing_1.wav`（普通挥刀）

本次没有启动浏览器或游戏验证按钮行为，以上仅为静态依赖审计。

## 版本一致性

| 位置 | 当前值 |
| --- | --- |
| module.json | 0.5.6-dev |
| CMake project VERSION | 0.5.5 |
| CMake 最低工具版本 | 3.25 |
| UI 页脚 | 0.5.5-dev |
| 历史 README 最新说明 | 0.5.6 |
| 历史 README 导入示例 | 0.5.5-dev |

这些差异作为迁移前证据保留，恢复发布前统一。根 README 已更新为新项目入口，旧说明保留在 docs/LEGACY_LONGSWORD_README.md。目录改名本身不要求立即更换旧 EML module ID 或 ABI。

## 源素材与研究证据

主代理已把原 BE 20261012 来源复制到项目内 `research/mhw-longsword/20261012`：**518 文件、44,065,901 字节（约 42.03 MiB）**，忽略 .git 和 __pycache__，原 BE 来源保留。

本审计确认当前身体烘焙直接需要的身体 MOD3、Rise LMT、MHW Model Editor 三个 MOD3 模块、MHW Anim Tools reader/evaluation/semantics 与 Blender space/armature 模块均存在，无需重复复制。

**烘焙风险**：bake_actions.py 只输出源骨架、身体 LSM、catalog 与源骨架预览，不输出缺失 source_weapon/grip/ground/hit/moves/audio，也没有 slot 1000 组合生成逻辑。重跑会覆盖现存身体与 catalog；先恢复真实派生脚本或按证据重建，并另存输出对照。

现存 research/expansion-20261013：**274 文件、1,157,173,697 字节**，包括：

- 完整 MHW 13 容器头/子块表可读检查及提取路线。
- 32 角色骨架/挂点研究及机器可读证据。
- 莱万汀长刀、提夫罗斯弓的资源包输入、解析对象图、配置树及通用化报告。

这些仍是可利用的本地证据与模型输入，保留“缓存来源/尚未实机验证”的限定。历史音频或登龙研究目录缺失，不等于这一整组研究不可用。

## 迁移路径引用

- package.py、preview_sheet.py 已从 __file__ 推导项目根，不依赖旧项目路径。
- bake_actions.py 的源库存路径已由主代理改为 ROOT/research/mhw-longsword/20261012，当前声明记录在 JSON。原 BE 绝对路径仍是可读来源，不是已丢失目录。
- 扩展 scout 的 G:\Better Endfield 指原始读取来源，要与开发工作区重命名区分。
- 根 README 已更新为新入口；旧说明保留在 docs/LEGACY_LONGSWORD_README.md，迁移映射见 docs/MIGRATION.md。历史报告来源路径保留，不能为消除搜索结果改写历史证据。
- 旧 build 有 **13 个**含旧根的 vcxproj/filter/tlog/compiler 残留，清单在 JSON。恢复源码后新建构建目录重新 configure，**不手改生成文件**。

## 下一步顺序

1. 恢复真实实现头、SDK 和缺失测试源。
2. 恢复 slot 1000、女管理员 profile、派生数据与音效，保留现存身体 LSM 和来源证据。
3. 统一版本及活动入口，重新 configure、构建、有效原生/资源检查、EML 打包及离线导入。
4. 基线可验证后扩充武器系统，逐武器与角色实机校准，不让扩规模掩盖当前缺失和姿态问题。

本报告提供可审计迁移基线，没有声称完成代码恢复或游戏内修复。
