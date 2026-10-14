# 离线网格验收（1.0.0-offline-rc1）

本记录对应当前 33 个角色、六种模式的实际导出网格、蒙皮权重、绑定矩阵与原生重定向求值。验收未启动游戏或服务端，不计算产物哈希。

## 最终范围

| 项目 | 最终数量与结果 |
| --- | --- |
| 普通角色×模式 | 198 行，全部通过 |
| 莱万汀火刀／提夫罗斯原弓跨角色 | 66 行，全部通过 |
| 普通角色源姿势求值 | 423,885 帧，含身体与实际手指 |
| 普通角色实际网格采样 | 74,707 帧 |
| 特殊武器实际网格采样 | 24,699 帧 |
| 新增大剑持姿 | 20145／20245，共 33 人、5,511 源帧、1,003 实际网格采样 |
| 非有限／失控网格与超限鞋底 | 最终记录均为零 |

普通核心帧对三个 P0（莱万汀、提夫罗斯、大潘）全帧求值，其余角色采每 12 帧及片段边界。新增大剑持姿对上述三人和实际最小／最大尺度角色（噗切娜／骏卫）全帧采样，其余角色各片段取首、中、末三帧。采样数量不能解释为每个角色的所有片段都逐帧验证。

## 该版本修复与回归依据

大潘片手剑前滚 10011、蓄力释放 10112 曾出现真实手掌网格钻地。最终系统根据原动作鞋底／手支撑关系，分别采用必要的展示 root 接地或独立手臂固定长度 IK；保留原腕 world 旋转，不拉长骨骼，不修改原曲线。两个片段全部 204 帧的实际手／鞋网格检查通过：最低手掌分别为 −13.915 mm、−3.537 mm。大潘整个片手剑核心的 2,261 帧已重新扫描，最低鞋底 −22.825 mm，位于记录的 30 mm 门限内。

新增大剑强／真蓄力持姿来自原 145／245 槽。原 LMT embedded TIML 没有 `mWeaponMotionNo` 或武器插值触发，不能生成假 own-skin 动画。缺少显式触发的片段沿用原 MOD3 标准化 bind 网格，并核对七根源武器骨骼 bind→identity 最大误差 5.96×10⁻⁸。模组切换动作时恢复该网格；怪猎原生可能保留的上一动作武器状态仍未知，未宣称完全还原该状态。

新编译的原生 oracle 重新求值后，发现安塔尔原记录的尺度有一个 float ULP 差异，root 最大偏差约 0.00048 mm。未以宽容差隐藏差异：保留旧记录，重新生成其六种 oracle，并重扫六行普通网格和两行特殊武器。最终 198 行旧核心的 418,374 帧身体／手指四元数与 root float 数组均与新 oracle 精确相等。大剑旧核心 116,556 帧和新增 held 5,511 帧也分别通过独立精确数组比较。

## 可复核记录

- `research/character-coverage/coverage.json`：198 行普通模式，含新增 held 独立采样来源。
- `research/character-coverage/special-cross-coverage.json`：66 行特殊武器。
- `research/character-coverage/final-mesh-provenance.json`：最终 264 行语义来源门禁。
- `research/character-coverage/final-core-oracle-exact-gate.json`：198 行原生姿势精确数组比较。
- `research/character-coverage/core-oracle-refresh.json`：安塔尔旧尺度差异与修复记录。
- `research/character-coverage/greatsword-core-oracle-exact-gate.json`：旧核心与新增 held 的独立数组比较。
- `research/character-coverage/greatsword-held-mesh-gate.json`：33 人新增 held 实际网格采样。
- `research/character-coverage/final-hand-body-ground.json`：大潘两片段逐帧手／鞋实际蒙皮坐标。
- `research/multiweapon/greatsword/bind-fallback-validation.json`：原 LMT TIML、MOD3 的无触发／bind 依据。

## 结论边界

这些门禁验证真实几何、有限蒙皮、已列核心覆盖、鞋底与明确手掌接地修复、源武器装配和证据一致性。它们不证明所有衣服／发丝／身体／武器完全没有相交，也不模拟原生 cloth、材质、火焰 VFX、弓臂蓄力曲线或真实场景碰撞。游戏内镜头、帧率、控制接管和实际场景接地仍按用户决定留到后续实机验证。
