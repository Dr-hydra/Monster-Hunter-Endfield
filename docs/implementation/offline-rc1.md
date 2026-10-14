# 1.0.0-offline-rc1 离线交付记录

本次完成用户授权的离线实现、验证与 E Mod Loader 打包。未启动终末地、怪猎或游戏服务端，没有计算产物哈希。项目位于 `G:/Monster Hunter Endfield`，游戏资源及派生媒体留本机。模块 ID 保留 `endfield.longsword.showcase`，版本 `1.0.0-offline-rc1`。

## 实现与覆盖

- 太刀、片手剑、大剑、长枪、轻弩、弓，1185 个可加载身体动作槽；槽数不等于招式数。各模式有输入、派生、按住／松开、自动演示和退出恢复。大剑完整演示覆盖三段持姿、九种释放。
- 33 个角色、80 件普通装备、莱万汀火刀和提夫罗斯弓分别配置。198 个普通角色×模式与66个特殊武器×角色通过实际网格门禁；原模型、蒙皮 palette、手指和鞋底参与求值。
- 233 条真实触发的源武器 LSK 自身形变。大剑十二槽原 TIML 无自身动画触发，保留明确的原 MOD3 bind 回退，不制造动画。
- 188 条模式 PCM＋8 条特殊模型 PCM，共196个唯一音源；统一混音、分类音量、变体、持续声、取消和清理，不另调游戏原声音事件。
- 七页网页工坊、逐模式按键、两套官方物理预设、自定义／迁移、106个源动作预览、六部有声源网格影片、试听与预设。游戏 HUD 支持教学、精简、摄影、位置和缩放。
- 本地练习默认关闭，使用独立目标、模拟 HP／资源／反馈和本地箭弹。真实怪物只读；不新增真实攻击、战斗发送、HP／AI／位置／奖励／存档或服务端写入。

## 验证记录

| 项目 | 结果与记录 |
| --- | --- |
| Windows x64 / C++20 Release | `build/development/final-build.log` |
| 原生回归 | `final-tests.log`，15/15通过；含实际硬件输入、三段三级蓄力、PR续接、装填取消、瞄准中释放及生命周期 |
| 发行银行等价与坏输入 | `test-fixtures/packed/packed-native-roundtrip.json`；1185条／107277帧源浮点、派生挂点和握柄逐bit相同；49个损坏输入拒绝，两个模式失败完整回滚 |
| 包内资源实际加载 | `final-packed-assets.log`；解包发行资料下六模式、33角色、动作／声音与两轮演示通过 |
| 六模式源闭包 | `research/multiweapon/final-source-runtime-validation.json`；1185身体、188PCM、233LSK与12个明确bind回退 |
| 特殊武器原声 | `final-special-audio.log`；4 Events、8 PCM与7生命周期动作闭合 |
| 最终网格来源 | `research/character-coverage/final-mesh-provenance.json`；264行来源与核心覆盖均当前，无变化行 |
| 真实网格与新增持姿 | 普通423885源帧／74707实际网格采样，特殊24699采样；GS新增5511帧／1003网格采样；无非有限或超30mm鞋底记录 |
| 长时状态 | `offline-soak-result.json`；发行解包资料下每模式30虚拟分钟、24次资产加载，通过；六模式PrivateUsage增量均0 |
| 六源影片 | `research/multiweapon/source-tour-tail-verify.json`；全程音视频解码、收纳尾帧通过；合计18822501字节 |
| 网页源码 | 实际Edge：七页61项、预设115项、目录279项、发布控件43项、影片49项、嵌入回退4项；`output/playwright/finish-*.txt` |
| 打包负向与边界 | `build/development/finish-package-negative.log`；14/14通过，含轨道损坏、路径、单文件／总量／条目限制 |
| 真实EML | 隔离ModuleStore与OfflineHost：六模式／PCM、配置回执、非法模式、NotReady、升级保留配置、停用／重装／移除与关闭通过 |

完整网格依据见 [网格验收](offline-mesh-final.md)。CPU检查机器为Ryzen 7 5700X3D、约32GB RAM，最终约2.52–2.93微秒／步；没有测量Unity蒙皮、GPU、shader、游戏帧率或音频设备延迟。

## 存储与安装包

交付包：`dist/endfield.longsword.showcase-1.0.0-offline-rc1-win-x64.zip`。全部动作／手指／自身形变／音源保留；只排除未使用的旧版副本。LSW保留原native的float32输入，LGB逐字节合并完整LSG；JSON仅去空白，不舍入。原可编辑资源不变。发行解包约201MiB，兼容当前EML，无需改变Loader；后续规模增长时允许一起调整EML。

EML方法与实际结果见 [宿主验收](eml-verification.md)。最终包内网页另用 `ui_packaged_browser_verify.cjs`，核对内嵌目录、196PCM、六MP4、两套预设和缺失JSON时的回退；本地结果位于 `output/playwright/final-packaged.txt`。真实最终ZIP验收记录位于 `local/verification/eml-rc1-final-20261014/result.json`。

## 修复依据与保留边界

身体basis与水平位移／world-Y分开；真实鞋底用于接地。双手超程时最少移动主手至双臂可达域，固定骨长求解，保留源双腕偏移和world旋转。太刀接触mask按实际刀柄与目标模型逐帧gate，不称原作者IK标记。大潘片手手掌钻地按源支撑／目标碰撞分别处理；不通过抬脚已着地的整个人物掩盖手掌问题。

物理键位已按官方5键／3键鼠标表核证。各招派生另据Hunter's Notes、COL与源姿势；长按阈值、自动松开时机和本地条件属于模组政策。长枪Power Guard源入口、弓装瓶完整姿势和龙之矢官方硬组合仍有明确未知项，未拿替身动作冒充。详见 [键位核对](keyboard-controls.md)、[大剑来源](greatsword-charge-source.md)。

原生shader／VFX、布料与链条、真实场景碰撞分层、动态换装／镜头、实际输入音画同步和玩家观感仍待后续实机。离线门禁不保证所有衣物或武器相交消失，`runtime_verified`与`visual_quality_certified`保持false。本次交付是离线候选，不宣布未经实机验收的正式1.0。通信范围见 [本地练习边界](local-practice-boundary.md)。
