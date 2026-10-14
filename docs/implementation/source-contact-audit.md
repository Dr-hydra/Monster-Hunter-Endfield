# 太刀源握持的几何审计

`tools/mhw_source_contact_audit.py` 对六模式核心动作读取当前真实源帧，输出
`research/multiweapon/source-contact-audit.json` 和太刀候选 metadata
`research/multiweapon/longsword-contact-candidates.json`，不改动画、support
权重或 native 模块。候选清单为 `reconstructed_from_hilt_geometry`，明确
`authored_contact_flag=false`、`enabled=false`，须通过目标角色可达与接触
检查后才能启用。原位移 offset 和源世界旋转均逐帧保留。

普通太刀不能以“手腕距离小于 15 或 40 厘米”决定双手握持。实际原刀归一化
柄侧网格延伸到 Y=−0.720 米，源左手腕常处于刀空间 Y=−0.582 米，右手腕
在根附近，因此约 0.64 米的手腕距离仍可能握住同一长柄。挂点 30/47 在许多
帧共位，但它们也会在收刀、翻滚时共位，不能据此声称两只手在接触。

本次以原刀 882 个负 Y 侧索引三角形为柄侧子集。有实际 LSK 的槽位逐帧应用
真实自身蒙皮，其余使用准确归一化绑定网格，不虚构原生武器动画 carry。
源 index0/middle0 的位置由真实 wrist/rest 层级求出；原 LMT 确认这些指根
没有平移轨、主要手臂没有缩放轨，因此没有靠虚构手指位置测量。候选包络为
源模型 index0-to-pinky0 骨距的一半，即 0.03153027 米，判定两根近端指根
是否都处于实际蒙皮柄面附近。该包络只产生保守的几何候选，不是原作的
接触状态，也不是完整手指闭合、目标皮肤或身体穿模认证。

| 太刀槽 | 柄面接触候选区间，零起始帧 |
| --- | --- |
| 102、103、104、105、107、108、112、127、128 | 各动作全部帧 |
| 101 | 20–27、140 |
| 106 | 12–25、111–120 |
| 306 特殊纳刀 | 无；不能强行两手拉向主刀柄 |
| 307 居合 | 32–137 |
| 311 气刃居合 | 43–176 |
| 1000 登龙组合 | 0–70、107–391 |

完整候选包括翻滚、普通收刀和其他核心招式，逐帧 mask/距离/连续区间见 JSON。
片手剑、长枪的左盾与弓的左右独立职责使用各自挂点；这些模式的 inter-hand
support 为零并不能算缺失握持，不据本次太刀调查开启它们的双手 IK。

源接触审计还记录源 JSON 与旧 LSG 的权重差异，不能把旧距离阈值产生的行
当成新增接触证据。最终目标 gate 必须另记录通过帧；未通过区间保持禁用。

`tools/endfield_longsword_source_contact_gate.py` 已对全部 33 个角色的 20 个
太刀核心槽逐候选帧运行真实 native 固定骨长求解，并以实际目标 index/middle
指根对实际 MHW 柄侧三角形测距。每个角色使用其真实半掌宽；只有完成
0.18 秒混合、源腕 offset 残差小于 5 mm、目标指根贴柄且没有 unreachable
求解失败的帧才通过。69,399 个角色候选帧中 63,804 个通过，结果在
`research/character-coverage/longsword-source-contact-candidate-gate.json`。
原腕旋转、原 source offset、源身体曲线均未改写。

普通源刀与终末地火刀分别验证，不将某模型的通过帧推广到另一个模型。
产品 mask 的出版由 `endfield_longsword_contact_candidate_gate` 后续流程统一
合并两套结果，按角色／模型／槽保存；边缘平滑仅发生在通过区间内。此 gate
仍不证明无衣物穿插、原生 cloth/shader 还原或游戏场景运行结果。

## 源演示尾部同步

最新 `source_demo_timeline` 导出真实 `weapons_visible`。运行
`python tools/mhw_source_tour_update_tail.py` 可比较新旧 controller 样本，仅
重渲染发生变化的 10 fps 帧，再通过原统一 PCM 混合、编码六段源参考影片。
本轮六片共变更 43 尾帧、复用 2,206 帧，主体时序未改变。最后保持原标准
收刀动作终帧，隐藏独立源武器；不挂载未证明的刀鞘。

`python tools/verify_source_tours.py` 完整解码六片的画面和音轨，并确认影片
清单、native 样本数和最终收刀状态一致。源猎人影片用于核对原始素材，不
表示终末地角色已通过实机渲染、场景或武器接触验收。
