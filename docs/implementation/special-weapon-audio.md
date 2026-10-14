# 两把特殊展示武器的原声音源

原声音频来自当前终末地有效 VFS；离线提取、解码后使用模组统一 PCM 链路。
不调用终末地音频 Event，不启动游戏或服务端。普通招式仍使用各模式原版
怪猎的动作音效，特殊武器声音由独立的展示生命周期管理，不能每次换身体
动作都重新播放火焰底声。

## 可重建的精确对应

| 展示用途 | 终末地源 Event | Media / 源选择 |
| --- | --- | --- |
| 莱万汀火刀持续底声 | `au_chr_0016_laevat_ultimate_skill_pad` | `521486327`；16.002 秒 |
| 莱万汀火刀结束 | `au_chr_0016_laevat_ultimate_skill_end` | `439293254`；2.075 秒 |
| 提夫罗斯弓蓄力起声 | `au_chr_0034_typhoea_power_attack_pre` | 原 Random 播放列表：`660834130`、`638967873`、`233167323` |
| 提夫罗斯弓释放 | `au_chr_0034_typhoea_power_attack_shot` | 原 Random 播放列表：`349264779`、`544435958`、`633621165` |

火刀底声由当前 `buff_chr_0016_laevat_ultimate_sfx_loop.json` 的准确字符串
指向对应 Event；结束声由 `buff_chr_0016_laevat_ult_end.json` 指向。弓的
两个 Event 来自当前 `chr_0034_typhoea_power_attack.json`。出版清单保留配置
路径、Persistent 覆盖、字符串偏移/长度、Event/Bank ID、HIRC 默认分支、
Random 播放列表和 Media 编号。

每条路线都验证 v150 Bank ID 与 Event ID 一致，严格沿 Event 的 `ulActionID`、
Play 的 `idExt`、Switch 的默认 `NodeID`、Random 的 `ulPlayID` 到 Sound 的
`sourceID`，然后读取当前同 SFX 命名空间 Media；没有将任意整数当成音频 ID。
八条出版 WAV 共 6,072,168 字节，与 vgmstream 解码出的 PCM 直接字节一致。
未计算文件哈希，未复制 BNK/WEM/PCK 进入模组。

## 源事实与展示选择

- 火刀底声是有限的 16 秒原录音，源 HIRC 没有无限循环。模组在持续展示火刀
  时重复底声是自身的展示策略，`source_loop=false` 和 `lifecycle_policy`
  对此作了区分。进出展示武器驱动它；身体动作切换不应打断或重启它。
- 弓蓄力源 Event 除普通 Random 列表，还包含一个 State 分支。该分支默认
  为空，只有其他状态才使用 `390433704`；这条媒体也只有 6 kHz，因此既不
  纳入默认 Random 候选，也不为兼容而偷偷重采样后充当普通蓄力音。
- 蓄力起声和释放声都是有限 one-shot。蓄力开始只播放一次，释放或取消停止
  自有蓄力组。取消不拿另一个普通攻击的结束 Event 冒充原蓄力取消声。
- 火刀 pad/end 的源 MakeUpGain 为 −3 dB；蓄力 Random 容器为 +1 dB。
  清单保留对应线性增益。原游戏的混音总线、DSP、空间状态和 RTPC 没有完整
  重放；统一链路的软件均衡、音量和 40 ms 停止淡出属于模组播放策略。
- 源 Random 的实际候选与顺序保留；模组可以轮换候选，不宣称实现 Wwise 的
  带权随机和状态模拟。触发时点来自本模组展示生命周期和既有怪猎释放标记，
  不称为终末地原技能的完整帧时序还原。

完整定向调查解出了 17 个已由当前配置证明的 Event、81 个 Media 引用。
其中可选 `ult_attack3` 的三个引用仅在 Audit PCK 命名空间找到，没有在
对应 Main SFX 命名空间找到；保留为显式研究缺项，未跨命名空间猜用。这些
可选攻击 Event 不影响上述四条必需路线。所有原始 Bank、WEM、解码报告和
完整图都留在 `research/endfield-special-audio`，不进入 Git 或发布包。

## 复现与只读核验

在项目根目录执行：

```powershell
python tools/endfield_extract_weapon_audio.py
python tools/publish_endfield_weapon_audio.py
python tools/verify_endfield_weapon_audio.py
```

第一步只读取当前有效 VFS 的目标 Skill/Buff 配置、PCK 索引及必要音频范围；
第二步根据实际默认分支出版 PCM 和 `assets/audio/theme_cues.json`；第三步
只读复核 HIRC 路由、配置来源、PCM 字节/幅度、候选隔离与进入/结束分组。
运行时独立主题生命周期、网页试听和发布依赖由原生模块、产品清单与打包器
分别检查；本文的源验证不等于实机听感或同步验收。
