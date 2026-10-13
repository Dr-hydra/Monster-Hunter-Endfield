# 原版太刀素材、源姿势与实验组合（2026-10-13）

本轮开发与工具、资源、预览均在 `G:/Monster Hunter Endfield`。没有运行 MOD 插件、启动怪猎、修改终末地真实伤害或发送战斗消息，没有计算产物哈希。

## 已完成

1. 使用用户安装的 MHW 13 个 `chunkG*.bin`，按数字补丁顺序合并目录。171,615 个有效文件，保留每条资源实际来自哪个 chunk 及覆盖记录。没有展开约 55 GB 容器。累计定向提取 107 个原文件、61,300,723 字节；其中一部分是之前选择的 common 动作，后续只提取需用条目。
2. 作者 MHWNoChunk 的官方 Warframe Oodle DLL 地址返回 403。改为下载并本地编译 [powzix/ooz](https://github.com/powzix/ooz)，13 个容器目录及所选原文件均成功解压；没有从第三方网盘找 DLL。
3. 从 [Blender 官方镜像](https://mirror.blender.org/release/Blender4.5/)下载 4.5.14 LTS portable，在 `local/tools/blender` 运行背景任务。下载 [vgmstream 作者 r2117](https://github.com/vgmstream/vgmstream/releases/tag/r2117)，22 个原太刀媒体均解为 PCM16 WAV。
4. 原版 `w03_00.lmt` 是 v95，164 个非空条目；154 个无需外部父级基姿的条目完整求值。10 个 NeedsParent 条目没有用假基姿冒充原版。运行清单目前包含 154 个原版条目、一个明确实验的登龙组合、4 个保留的旧 MOD 条目，共 159 个；未丢弃旧 LSM 或其原 MOD 输入来源。
5. 原版 `m_body043_0000.mod3` 有 104 根骨；其绑定矩阵与现 ZXC donor 的最大差为约 1.91e-5（原文件厘米单位）。新 source rig 来自原版身体；双手解剖参考来自原始指根，未按视频拟合手腕。
6. 输出全部 ground、grip、source-hand-support、moves、local practice windows、UI source preview 和女管理员 native socket profile。握持数据使用原版左右腕分离与真实右手旋转，只帮助左臂接触，不覆盖剑向。

## 登龙：哪些是直接证据，哪些仍是展示控制

之前 Rise MOD 的 170 只有 32 头部帧、106 曲线末帧且 NeedsParent；**原版 170 为 245 帧，不等于完整登龙**。原版 170 的最低脚峰值只有约 0.053m。仅据帧数或相同槽号推断完整招式会继续造成错误。

[Strackeror/MHW-Fsm](https://github.com/Strackeror/MHW-Fsm) 用其兼容的 Construct 2.10.56、Blender Python 3.11 解码成功，原 FSM 共 90 个节点。控制链是：

| FSM 节点 | ActionNo | 原节点意义 |
| --- | --- | --- |
| 45 | 87 | 必杀攻击 |
| 55 | 89 | 必杀攀升 |
| 56 | 90 | 必杀攀升跳跃 |
| 58 | 91 | 必杀空中攻击 |
| 57 | 92 | 必杀着地 |

这些 ActionNo 是游戏动作类编号，**不能直接当作 LMT 槽号**。LMT 阶段的对应是根据源姿势连续性与 MBD 同槽记录推断，尚未反汇编 native 动作类来逐项确认。

| 原曲线连接 | 21 骨最大旋转差 | 右腕差 | 处理 |
| --- | ---: | ---: | --- |
| 170:89 → 171:0 | 约 1.71° | 约 0.043° | 原曲线，接续 X/Z |
| 171:46 → 172:0 | 接近浮点误差 | 接近浮点误差 | 原曲线，接续 X/Z |
| 172:133 → 173:0 | 左脚趾约 15.44° | 接近浮点误差 | 6 帧最短路径旋转混合、FK，手腕不改方向 |

1000 组合是 `170:0..89 → 171:0..46 → 172:0..133 → 6帧连接 → 173:0..114`，392 帧 / 60 FPS，约 6.517 秒。不是恢复历史 303 帧 MOD 手工编排，也没有添加“刀尖必须朝下”的手腕或剑向关键帧。

原 171 的髋从约 0.46m 升到 3.6384m；原 172 首髋同为 3.6384m，但其 root Y 持续固定为 241.2358cm。原 172 结束时脚仍离地约 2.44m；下降需要 native 控制，并不全在 LMT 骨曲线内。

因此目前下降是**本地展示控制**：从原 172 右腕举刀最高帧 19 开始，以 51 帧 / 0.85 秒将该帧实际最低脚逐渐锚到舞台平面；达到接地后保持脚底约束，173 全段按原姿势接地。2.3936m 的原 172 末髋与 173 首髋差是连接对照值，不是凭感觉设置跳高。该时间安排与用户 1:08–1:11 视频的升空、举刀、下劈、落地顺序对照，但没有声称解出了怪猎原生重力或逐帧时长。

## 原刀安装候选

原 `bs_swo001.mod3` 与 `bs_saya001.mod3` 各有 4 根自己的骨骼，EVWP 也已提取。用[作者 EVWP 模板](https://github.com/zStatykz/MHW_EVWP_Template)解析的字段包括展架、铁匠展示、收刀和挂件变换；没有可直接引用的 drawn weapon-to-hand 安装矩阵。

原刀实际 geometry 在 Blender MOD3 导入基下长轴为 -Y，其原 root bind 含导入的 +90° X。研究预览的候选实例使用 `source247_world × weapon_root_bind^-1`，抵消真实导入坐标基后保留原刀内部骨架、geometry；没有手工加一个 90° 或按刀尖倒推手腕。原 172 的候选画面有举刀和竖直下劈，方便与视频核对。

但 **247 是否就是 native drawn attachment 的完整装配入口仍未获得 native 代码证据**，故它只用于标明的研究候选，未作为运行时剑向覆盖。运行时 source_weapon_tracks 的 rotation 是原右手 world rotation，只运输双手间距；莱万汀刀身继续保留原生 prefab/skin 装配。研究模型没有原衣物物理/显隐控制，不能将衣物或辅助网格的离线外观当作实机认证。

## 原声与普通挥刀

22 条原音源来自合并覆盖后的 `wp03_swo_epvsp.nbnk`；编号顺序保留，动作标签沿用用户提供的 22 条表，原表的用途仍需试听校准。特技 cue 是明确的演示时间轴，不声称完整解出了 native 音频事件时序。

- 起跳：`35004545.wav`（表第 3 项），1000 的 1.50s。
- 下劈：`280637586.wav`（第 11 项），1000 的 3.0833s。
- 见切：`367971202.wav` / `387684418.wav`（13 / 14）。
- 小居合：`12607224.wav`（1）；白刃居合：`111614630.wav`（6）。
- 收刀：`858105059.wav`（19）。

MHW `wp_swo_cmn` / `wp_swo_bs_001` 普通刀身 bank 也已提取，但本轮未将无法确认用途的片段随意标成普通斩。普通挥刀由另一个任务从终末地的确知事件与媒体提取，最后进入同一 PCM 链路；其来源、ID 和 cue 合并由主代理记录。

## 验证与审阅文件

原版骨架/四元数/手脚派生均由真实解析数据生成。159 个 LSM、22 个 PCM、16 个初始特技 cue，尺寸、文件长度、归一化旋转、有限值、全部引用和时间边界检查通过；普通挥刀合并后由主代理再做整包验证。游戏内握刀、衣物、地面和声音仍需实机验收。

- [源阶段联系图](../../research/mhw-original/source-combo-contact.png)
- [源阶段 GIF](../../research/mhw-original/source-combo-review.gif)
- `research/mhw-original/source-original-air172-candidate-mount.blend` 与 `source-air172-candidate-*.png`：原猎人、原刀、未验证的 attachment 候选。
- `merged-index.json`：13 容器覆盖与有效路径。
- `original-bake-audit.json`：154 支持条目、10 NeedsParent、原控制记录。
- `fsm-helm-iai-states.json` / `fsm-helm-conditions.json` / `mbd-decoded.json`：原始控制关联。
- `helmbreaker-composition.json`：每个组合帧的源槽/源帧、展示下降和连接依据。
- `source-boundary-rotations.json`：源阶段连接误差。
- `source-sword-geometry.json` / `evwp-decoded.json`：原 geometry、bind 和装配未知边界。

## 重建顺序

1. `tools/mhw_extract_original.py`：所有容器目录合并后，按 `--select` regex 只取所需输入。运行时保留完整 merged index；`selection.json` 是最后一次选择，累计输入以 extracted 实际库存为准。
2. 背景 Blender 执行 `tools/mhw_bake_original.py`，仅写研究 staging。
3. `tools/mhw_publish_baseline.py`：有证据的原样求值、明确的本地下降和腿连接，发布运行数据及 UI JSON。
4. `tools/mhw_bake_audio.py`：解码原声并按组合阶段生成 cue；之后合并确知终末地普通挥刀 cue。不要在合并后重跑并丢掉额外来源。
5. 根构建、runtime 初始化、资源验证、EML 包检查由主代理完成。

第三方下载只在本地工具目录。ooz 研究 DLL 不链接到模组；作者许可与仓库来源保留，不随模组发布原版 raw chunk、MOD3、LMT、FSM、工具 ZIP 或 .blend。
