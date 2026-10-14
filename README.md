# Monster Hunter Endfield

在《明日方舟：终末地》中提供《怪物猎人：世界／冰原》的连续动作、可重绑定操作、自动演示、独立展示武器和本地练习。本版为 **`1.0.0-offline-rc1` 离线候选包**：按本次要求推进离线开发、验证和 EML 打包，尚未进游戏验证。

开发根目录为 `G:/Monster Hunter Endfield`，私有远端为 [Dr-hydra/Monster-Hunter-Endfield](https://github.com/Dr-hydra/Monster-Hunter-Endfield)。模块 ID 保持 `endfield.longsword.showcase`，包格式为 EML format 1 / Native ABI 1，目标为 Windows x64。

## 本版范围

| 终末地默认类别 | 怪猎模式 | 展示部件 |
| --- | --- | --- |
| 单手剑 | 太刀、片手剑 | 剑／刀与片手盾；兼容型号可沿用装备 |
| 双手剑 | 大剑 | 独立大剑与双手握持 |
| 长柄武器 | 长枪 | 长枪与左手盾 |
| 手铳 | 轻弩 | 独立弩、装填、瞄准与本地弹体 |
| 施术单元 | 弓 | 独立弓、拉弦、箭与本地弹体 |

33 个角色均有独立骨架、手指、体型与脚底配置，可通过独立展示武器使用六模式；80 件装备有逐型号模型记录，两把重点模型另有配置。角色与武器分别适配，同名挂点不视为安装矩阵相同。

- **莱万汀太刀**：独立火刀模型、附属骨驱动和原声音；保留源手腕和源武器装配依据，登龙按源阶段组合。部分飞行与下降属于展示控制，未声称重放怪猎原生战斗控制器。
- **提夫罗斯弓**（表名提弗洛斯）：原弓挂载依据、弓弦／右手指拉弦与本地箭矢；可跨角色展示。
- **1185 个可加载身体动作槽**、源手指／鞋底／武器轨道及来源记录。槽数包含上下文、合成与明确标记的旧条目，不等于招式数。
- **196 条 PCM 音源**：188 条模式音源与 8 条特殊模型原声；统一调度、分类音量、变体、循环、取消、淡出和清理。火刀有限垫音的重复为模组展示策略，原混响／RTPC／加权随机未模拟。
- 输入支持组合、按住／松开、输入缓存、派生提示与手动接管；演示可选完整、基础或招牌序列，单次或循环，并可暂停／慢放。
- 七页网页工坊支持模式、角色／武器资料、动作和实际网格参考、六份源猎人＋原武器有声演示、试听、预设与离线草稿。游戏 HUD 提供教学、精简、摄影布局和位置编辑。
- 本地练习默认关闭，独立维护模拟 HP、资源、格挡／反击与箭弹。怪物可作为只读位置参考；不新增真实伤害、技能、奖励、存档写入或战斗发包调用。模型展示位移不写真实角色位置。

**198 个角色×模式组合的实际网格离线检查不等于全部握持和最终玩家观感通过。** 原生 shader／VFX、衣物与链条动态、场景接地／墙面／低顶、动态挂载与换装恢复、实际输入音画时序及 HUD 遮挡仍须后续游戏运行检查。离线结果、宿主结果和实机状态分别记录。

## 安装与操作

将 `dist/endfield.longsword.showcase-1.0.0-offline-rc1-win-x64.zip` 导入 E Mod Loader，打开设置页，选择模式与展示武器后保存。两个推荐入口分别为莱万汀太刀和提夫罗斯弓；查看角色资料不会切换游戏角色。

- **F9**：自动演示，攻击键可手动接管。
- **F8**：开启／结束手动模式；**Esc**：退出。
- 网页提供暂停、继续、停止、演示序列及循环设置；未连接时可编辑草稿、预览、试听和管理预设。
- 各模式操作以“动作与键位”页为准；可选已核对的怪猎官方 5 键／3 键鼠标物理预设，也可重绑定。各招派生、GUI 组合图标与本地阈值另列证据，不随物理预设一并认证。

莱万汀长刀目前仅支持太刀，提夫罗斯弓仅支持弓；不兼容选项会拒绝。升级保留原模块 ID 与 schema 2 配置迁移，先停止旧演示再导入更新。

详细操作、声音、本地练习和退出说明见 [使用说明](docs/USER_GUIDE.md)。

## 构建、资源与验证

源码使用 C++20、CMake 3.25+ 和 Windows x64 MSVC。SDK 与 JSON 单头在源码中；游戏资源、提取数据、烘焙轨道、媒体与大型预览保留本机，不入 Git。只克隆源码不会自动取得完整运行素材。

```powershell
Set-Location -LiteralPath 'G:\Monster Hunter Endfield'
cmake -S . -B build/development -A x64
cmake --build build/development --config Release --parallel 4
ctest --test-dir build/development -C Release --output-on-failure
python tests/test_package_validation.py
node tools/ui_verify_rules.cjs
python tools/build_product_catalog.py
python tools/package.py --package-root build/development/package --validate-only
python tools/package.py --package-root build/development/package
```

[素材重建与离线交付](docs/RESOURCE_REBUILD.md) 记录六模式烘焙、特殊模型原声、实际目标网格、版本来源和包依赖闭包。网页实际浏览器检查、原生回归、长时循环与真正 EML 隔离导入各自留证；浏览器／SDK mock 不代替 EML，OfflineHost 不代替游戏运行验收。默认不计算产物哈希。

## 开发资料

| 路径 | 用途 |
| --- | --- |
| [成品计划](docs/PROJECT_PLAN.md) | 六模式、全角色、重点模型至成品的范围和验收 |
| [角色与武器覆盖表](docs/ADAPTATION_COVERAGE.md) | 33 角色、80 装备与两把特殊模型的分层状态 |
| [离线候选版验收记录](docs/implementation/offline-rc1.md) | 最终构建、完整回归、离线宿主与包验证 |
| [网页与 HUD](docs/implementation/ui-workshop.md) | 界面实现、数据来源和真实浏览器证据 |
| [特殊武器原声](docs/implementation/special-weapon-audio.md) | 事件／HIRC／Media、展示生命周期与已知限制 |
| [键位与操作核对](docs/implementation/keyboard-controls.md) | 官方两套鼠标预设、原 Hunter's Notes 与操作差异 |
| [EML 离线宿主验证](docs/implementation/eml-verification.md) | 独立 ModuleStore 和真实 OfflineHost 验证方法 |
| [历史基线审计](docs/BASELINE_AUDIT.md) | 迁移时库存，不代表当前版本状态 |
| `native/`、`tests/`、`tools/` | 原生模块、回归、资源与离线验证工具 |
| `profiles/` | 角色与武器独立配置 |
| `assets/`、`research/`、`local/` | 本地素材、研究和工具；大型游戏数据不入 Git |

源代码许可证为 [AGPL-3.0-only](LICENSE)，游戏资源和工具许可见 [第三方来源说明](THIRD_PARTY_NOTICES.md)。
