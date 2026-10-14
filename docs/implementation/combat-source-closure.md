# 四武器控制来源核证

2026-10-14。证据来自本地《世界／冰原》有效资源覆盖、原 LMT/TIML、COL、武器参数、SHLL/SHLP、FSM，以及原游戏可执行文件的离线派生副本。没有启动游戏，没有计算产物哈希。原游戏文件只读；Steamless 3.1.0.5 来自 [atom0s/Steamless](https://github.com/atom0s/Steamless)，只对 ignored `research/multiweapon/executable/` 中的副本作解包。解包工具和游戏派生文件不进入发行包。

`mhw_native_controller_probe.py` 从原反射注册关联类和构造方法，再核对原机器指令；不把 FSM ActionNo 与 LMT 槽位按固定差值换算。报告保存在 ignored `research/mhw-original/combat-native-controller-proof.json`。当前原程序版本的证据检查使用 PE 地址、类注册关系及指令内容，版本改变会失败并要求重新核证。

| 分支 | 原来源证据 | 发布／宿主控制边界 |
| --- | --- | --- |
| 长枪准备 | `cChargeStart` 构造选择 LMT 120；181 帧，循环起点 150，无出手 COL 窗口；源 ActionPhase 0／1／3 在 0／52／115 帧 | 按住保持准备并按源末段循环；防御资格仍由本地 guardable／朝向／耐力事件判断。115/60 秒的手动松键分档明确标作源 phase-3 到本地释放策略的映射，不声称原引擎全部防御／反击时序已经重放 |
| 长枪短释放 | `cChargeStartStab` 保存 LMT 122，初始化提交该槽；其更新方法等动画结束，再提交 LMT 106 | 发布 30122→30106。不能直接猜成 105。满释放 `cChargeEndStab` 选择 121；源窗口 20…32 帧 |
| 长枪受击类别 | 原 FSM 78／79／80／81／82 对应 S 中段／上段、M 中段／上段、L 防御；原类构造分别选择 126／123／124／127／125；满释放 83 对应 121 | 本地合规反击使用已确认的 121；没有复刻原防御后退 S/M/L、Guard 技能和原受击动画分类 |
| 大剑真蓄力 | 146／189／190 首击 37…43 帧、末击 101…113 帧，末击 MV 120／175／211；加强 Finish 275／276／277 窗口 46…58 帧，MV 144／210／264 | 同一动作捕获首击的明确弱点资格，末击按倍率加强；不要求末击留在首击对象／部位，也不拼造加强姿态。宿主 Renderer 包络不是原怪猎肉质数据 |
| 片手精准突击 | 原 `cWp01Rush0…5` 共用判定在按键时捕获 SourceCommon `mFlag2` bit 27，再由 bit 28 输入 gate 放行；结果为普通／Just 两类且不会在等待过程中升级 | 344 的 Just 窗口 90…110 帧；345／347 为 85…105；346／348 为 45…65。普通 gate 分别从 80／80／35 帧开始，到对应源动作结束。按键时刻和派生许可分别保存 |
| 片手后段 | 普通／Just 构造对应 345／347、346／348，再到 319／341；刺入后的原 EmStick 类使用 320→321／342→343 | 刺入派生需要实际本地目标接触；脱离完成派生沿原槽位。没有把局部目标包络伪装成原引擎敌人约束点 |
| 片手刺入碰撞银行 | 原加载器将 `wp01_02.col` 注册到 bank 3，不等于文件后缀 2。其 node 46／54 为 Rush3／Strong Rush3，Move 0／1，源切断类型 1、MV 45／65，对应 319／341 的 28…40 帧；41…46 帧是普通银行的擦中 MV 8／13 | 发布角色 `constraint_attack`。原 named-bone 关系尚未解码，Names override -1 与未命名几何字段原样保留；宿主使用明确的本地武器扫掠近似。不能称盾击／防御窗，也不能给缺失关联填原 MV 100 |
| 弓箭数与蓄力 | 原参数加载器将三个箭数表读到结构偏移 0x8f8／0x908／0x918；原发射代码读取前两行分别为普通／刚射。普通箭数 1／2／3／3，刚射 3／3／5／6，第三行 3／3／3／3；原蓄力阈值读取第 0 行 0／1／2／3 秒 | 普通动作 125，刚射 136。第三行保留原始值，未把它猜作普通／刚射或额外普通连段 |
| 弓发射与伤害 | 普通 TIML 发射 bit 在 0 帧，刚射在 19 帧；SHLL 完读 1638 字节，普通 shell ID 0…3，刚射 10…13。SHLP 指向 COL 普通 MV 7／9／10／11，刚射 6／9／11／11 | 发射快照按类型／等级读取真实箭数、MV、速度、MaxRange 和 COL 半径。cm→m 为现有发布单位转换；原弹道、箭群角度、肉质、瓶效果及距离伤害修正未全部重放 |
| 登龙迟发 MV | 12 份 SHLP 的 Obj_Collision_Index 经 shell COL Names→Move 关联；七段原 MV 按无／白／黄／红为 0／10／15／25 | 各 pulse 保留四级 MV，按触发时的刃色快照读取。空声音字段仍不产生额外七声音效 |

六模式每条发布命中窗口均保存源 Move 与 `motion_value_by_window`。远程 Atk3 的原字段名是 `Damage_Motion_Value`，解析后统一为 `Motion_Value` 并保留字段来源。找不到源 Move 的窗口明确标作教学回退。真实弓箭在发射时已经应用 tier MV，其动作接触窗的 100 表示归一化，不再次乘同一个 MV。

本地伤害为**本地基础教学伤害 × 源 MV / 100**，并按已接入的本地分支及刃色倍率调整。这不是完整怪猎伤害公式；没有重放完整攻击力、肉质、斩味、会心、技能、元素、状态、部位破坏、眩晕及减气结算。

可重跑核证命令：

```powershell
python tools/mhw_native_controller_probe.py
python tools/mhw_combat_fidelity_probe.py
python tools/mhw_combat_rules.py
python tools/combat_source_verify.py
python -m unittest discover -s tests -p test_package_validation.py
```

最后一项覆盖发行门禁负向测试。源检查独立重读 6 份原 LMT/TIML、11 份 COL、283 条窗口、282 条 Move、428 个 w03p 字段及另 2115 个武器参数字段，并核对控制器来源和发布字段；这些检查不能替代最终原生控制／命中回归或游戏内验收。
