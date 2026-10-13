# Monster Hunter Endfield

在《明日方舟：终末地》中提供怪猎式键位、连续动作、自动演示和独立视觉武器。开发根目录为 `G:/Monster Hunter Endfield`，私有远端为 [Dr-hydra/Monster-Hunter-Endfield](https://github.com/Dr-hydra/Monster-Hunter-Endfield)。

## 0.6.0-dev 当前状态

原生实现、SDK、测试和关联素材已恢复，可生成 Windows x64 E Mod Loader 包。模块 ID 继续使用 `endfield.longsword.showcase`，包格式为 EML format 1 / Native ABI 1。迁移时的缺项见 [历史基线审计](docs/BASELINE_AUDIT.md)，不代表本版状态。

- 重点适配莱万汀，保留女管理员；其他角色尚未开放。
- 从完整怪猎安装按补丁覆盖提取 154 个可完整求值的原版动作，另有 4 个明确标记的旧 MOD 条目和一个实验登龙组合，共 159 个槽位。
- 登龙连接原 170→171→172→173 的姿势，保留源手腕方向。下降由本地演示控制，不能称为完整实机还原。
- 莱万汀可加载独立长刀刀身、自身骨架与原生材质，无需触发大招。安装仍属实验；环、链和完整火焰尚未接入。关闭“莱万汀演示长刀”可沿用装备。
- 22 条怪猎原声音源与 3 条离线提取的终末地普通挥刀 SFX 共用 PCM 链路。F9 演示覆盖五段气刃、见切、居合、登龙与收势。
- 本地练习默认关闭，只读取目标位置并维护独立模拟 HP；不新增真实伤害、技能或战斗发包调用。游戏自身正常通信仍然存在。

**尚未游戏内验收。** 握柄、下劈方向、双手接触、坡地/障碍/低顶、材质与声音时机需实机核对；地面处理不等于完整人体和衣物碰撞。

## 使用

将 `dist/endfield.longsword.showcase-0.6.0-dev-win-x64.zip` 导入 E Mod Loader，启用后使用莱万汀或女管理员。

- **F9** 自动演示；攻击键可接管。
- **F8** 开启/结束手动演示，**Esc** 退出。
- 左键普通斩，右键突刺，鼠标向前侧键或 Ctrl 气刃；组合键见配置页。
- 网页可选择独立长刀、调整速度/过渡/音量，或启用实验本地练习。

## 构建与本地素材

源码使用 C++20、CMake 3.25+ 和 Windows x64 MSVC。SDK 与 JSON 单头已纳入源码；游戏、提取数据、烘焙动作和音频仅存本机，新克隆不会直接拥有完整运行素材。

```powershell
cmake -S . -B build/development -A x64
cmake --build build/development --config Release --parallel 4
ctest --test-dir build/development -C Release --output-on-failure
python tools/package.py --package-root build/development/package
```

素材流程见 [原版怪猎实现记录](docs/implementation/mhw-original.md)。普通音效先运行 `tools/endfield_extract_swings.py`，再运行 `tools/merge_endfield_swings.py`；重新烘焙怪猎音频后须重新合并。

## 开发资料

| 路径 | 用途 |
| --- | --- |
| [项目计划](docs/PROJECT_PLAN.md) | 多武器扩展顺序与验收要求 |
| [本版实施记录](docs/implementation/0.6.0.md) | 已完成、验证与实机待办 |
| [莱万汀刀身](docs/implementation/laevatain-weapon.md) | 蒙皮、加载、安装证据与限制 |
| [运行接入审查](docs/implementation/runtime-integration-review.md) | 挂点、恢复与生命周期 |
| native/、tests/、tools/ | 原生模块、回归与离线工具 |
| profiles/ | 角色与武器分别适配 |
| assets/、research/、local/ | 本地素材、研究输出与工具；大型游戏数据不入 Git |

后续先完成莱万汀太刀实机校准，再扩片手剑、大剑、操虫棍和轻弩；提弗洛斯的弓仍为候选。源代码许可证为 [AGPL-3.0-only](LICENSE)，游戏资源和工具许可见 [第三方来源说明](THIRD_PARTY_NOTICES.md)。
