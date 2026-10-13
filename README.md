# Monster Hunter Endfield

在《明日方舟：终末地》中实现怪物猎人风格的多武器动作、连招和独立展示武器。项目从太刀动作演示迁入，后续开发根目录为 G:/Monster Hunter Endfield。

私有远端：[Dr-hydra/Monster-Hunter-Endfield](https://github.com/Dr-hydra/Monster-Hunter-Endfield)。

先阅读 [项目计划书](docs/PROJECT_PLAN.md)、[当前基线审计](docs/BASELINE_AUDIT.md) 和 [迁移记录](docs/MIGRATION.md)。

## 当前状态

这是现有项目与研究资料的迁移基线，不是新的可运行发行版。现存源码仍缺少部分实现头文件、测试、SDK、角色配置和动作关联资源；编译、打包及游戏内验收需在补齐后重新执行。详见基线审计。

历史模块清单版本为 0.5.6-dev，模块 ID 保留 endfield.longsword.showcase，继续使用 E Mod Loader format 1 / Native ABI 1。项目名与仓库名更新，运行模块 ID 和配置迁移暂不改变。历史能力与测试说明在 [太刀项目原始 README](docs/LEGACY_LONGSWORD_README.md)，不能作为当前工作副本已通过验证的证明。

## 开发方向

- 共用输入、动作播放、地面处理、音效、生命周期和本地练习；按怪猎武器建立各自的动作与连招数据。
- 从主角色蒙皮读取绑定姿势和比例；角色与武器配置分别管理，特殊体型或装备保留修正。
- 优先验证原版太刀及武器装配，再增加片手剑、大剑、操虫棍、轻弩与候选弓模式。
- 声音离线提取为 PCM，通过同一播放链路调度。
- 本地练习维护独立模拟血量，不接入游戏真实伤害或主动发送战斗数据。游戏自身正常通信不在该保证范围内。

## 目录

| 路径 | 用途 |
| --- | --- |
| docs/ | 计划、决策、基线与迁移记录 |
| native/、tests/、tools/ | 现有 C++ 接入、测试和转换/打包工具 |
| ui/ | EML 配置页源码；生成预览仅在本地 |
| profiles/ | 后续角色与武器配置，当前缺运行配置 |
| assets/ | 本地烘焙动作、音频和关联数据，不纳入 Git |
| research/expansion-20261013/ | 多武器、通用骨架及特殊武器初查 |
| research/mhw-longsword/20261012/ | 本地怪猎来源与工具副本，不纳入 Git |
| build/、output/、preview/、dist/ | 本地生成产物，不纳入 Git |

## 素材与构建

关联怪猎研究已复制到本项目 research/mhw-longsword/20261012/，保留 Better Endfield 原资料；tools/bake_actions.py 已使用项目相对路径。终末地既有缓存和当前客户端仍是外部研究输入，来源在各报告中说明。

Git 仓库包含源码、文档和配置页源码。原始游戏/模组、解包 bundle、烘焙动作、解码音频与生成预览留在本机。新克隆须按计划完成工具与本地素材准备，当前不能直接构建或生成完整 EML 包。

源代码沿用 AGPL-3.0-only，完整文本见 [LICENSE](LICENSE)。游戏资源和第三方工具不适用本项目源码许可证，见 [第三方来源说明](THIRD_PARTY_NOTICES.md)。
