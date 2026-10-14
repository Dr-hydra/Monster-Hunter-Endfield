# 大剑三段蓄力的原始资源依据

当前离线候选采用原猎人 `w00_00.lmt` 的三段持姿和九个释放动作。基础蓄力是槽 144（53 帧），接 152/153/154；强蓄力是 145（53 帧），接 155/156/157；真蓄力是 245（114 帧），接 146/189/190。模组编号分别增加 20000。原 `wp00_tree.at` 的三行 `115/144/114`、`123/145/122`、`246/245/247` 提供持姿引用，不能把 FSM ActionNo 按固定差值猜成 LMT 槽。

同一持姿的释放等级由展示控制器的按住时长决定，0／0.65／1.25 秒释放等级阈值属于模组展示设置；没有把它们标成原怪猎完整帧窗口、战斗派生或命中规则。F9 采用九对持姿和释放，交替展示基础、强、真三段，随后切到下一等级。当前原生时间线包含全部三种持姿和全部九种释放，33 个配置阶段共 897 个 10 fps 预览采样，连同回收尾约 89.8 秒。

这十二个 LMT 槽的原嵌入 TIML 中均没有 `PlMotionCommon.mWeaponMotionNo` 或 `mWeaponMotionInterpolationTime`。已直接重读原二进制 TIML，核对原帧数、完整属性名称序列与解码文件一致。源大剑 `bs_two001.mod3` 的七根自身骨骼按原 bind/rest 规范化后，矩阵与单位矩阵最大误差为 `5.96046446270422e-8`。

因此这些槽使用模组已有的“动作切换时恢复原规范化 bind 顶点”回退，没有制造 LSK 自身动画。此回退有真实几何依据，但不证明原怪猎的 retained/carry 动画状态也是静止。`source_skin_tracks.json` 的 `bind_fallbacks` 明确记录这个边界，原有 233 条真实触发 LSK 的数量保持不变。验证脚本 `tools/mhw_verify_greatsword_bind_fallback.py` 会检查原 LMT/TIML、MOD3 资源、时间维度、无触发条件与 bind identity；`mhw_prop_skin_tracks.py` 重建时自动执行它。

源参考影片使用原怪猎猎人、原武器与实际原生控制器时间线，由 Blender 离线兼容材质渲染，再混合统一 PCM 调度导出的声音。它用于观察源动作和声音，未验证终末地的游戏内姿势、材质、镜头、物理、音频设备时序或 Wwise 条件图。影片更新后执行 `tools/verify_source_tours.py` 全程解码音视频，并核对纳刀后源武器隐藏的结尾。

主要本地证据：

- `research/multiweapon/greatsword/bind-fallback-validation.json`
- `assets/modes/greatsword/source_skin_tracks.json`
- `research/multiweapon/greatsword/source-tour/timeline.json`
- `research/multiweapon/greatsword/source-tour/render-evidence.json`
- `assets/previews/source-tours/greatsword.mp4`

全过程未启动游戏或服务端，也没有计算产物哈希。
