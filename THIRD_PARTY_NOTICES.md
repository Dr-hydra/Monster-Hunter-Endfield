# 第三方来源说明

本项目是独立开发的动作展示与本地练习模块，与 CAPCOM 或鹰角网络无隶属关系。公开仓库提供源码、适配配置及工具说明；源码采用 AGPL-3.0-only，许可证不覆盖游戏资源与第三方模组素材。仓库不提供原版资源、烘焙动画、音频、纹理、模型预览视频或包含这些素材的安装包。

## 动作与骨架资源

`1.0.0-combat-rc2` 的本地构建使用《怪物猎人：世界／冰原》素材，按 13 个 chunk 的数字覆盖定向提取。本地运行包使用 1185 个烘焙身体动作槽、手指／接地／挂点轨道，以及派生的源猎人动作预览和原武器展示几何／纹理。槽数不等于招式数；部分条目依赖外部上下文，登龙阶段间的飞行／下降另有展示控制。4 个旧条目明确标为 Legacy，来自 [Rise style long sword](https://www.nexusmods.com/monsterhunterworld/mods/7113)；[Valor Long Sword](https://www.nexusmods.com/monsterhunterworld/mods/8848) 用于动作对照。早期骨架参考为原生 MOD3 外观模组，完整原版数据导入后仍保留来源区分。原 LMT、MOD3、FSM、MBD 与 chunk 文件不随运行包提供。

《怪物猎人》相关资源版权属于 CAPCOM 及相应权利人，改制动作和外观模组的权利归相应作者。取得游戏或模组文件不等于取得其重新分发授权。源码公开不包含素材授权；生成的资源和安装包默认留在本地。

《明日方舟：终末地》相关名称、角色与资源属于相应权利人。仓库包含角色／装备适配配置；本地运行包另使用派生的离线网格参考图与提取 PCM；不附带完整可加载的终末地角色模型、特殊武器原 mesh/material 包或游戏程序。莱万汀火刀、提夫罗斯弓的运行时实例从客户端资源读取，作为独立视觉道具。

## 音效

本版六模式共 188 条 PCM（含下述终末地普通挥刀），另有 8 条终末地特殊模型 PCM，共 196 个唯一音源。怪猎部分从本地游戏文件的原版武器 Bank 提取，包括太刀 `wp03_swo_epvsp.nbnk` 及各模式的 common／model Bank；不再以“太刀见切音效增强.zip”为运行音源。源时间线按 TIML → WWEV → HIRC／Media 核对，模组另编排持续声和条件事件，不声称重放完整原游戏 RTPC、容器选择与战斗控制。原 Bank 不随包提供，展示音量独立平衡。旧太刀标签参考上木光子 / bilibili 光子Proton 于 2020/7/13 制作的对照表，原作者提示可能有误；源依据与未知项保留在音频元数据。音效不受源码许可证覆盖。

音频处理参考 Better Endfield 的 `music/module.cpp` 与 `music/local_track.inc` 中的 PCM 调度、软件音量和退出清理方式；本模块短音效使用独立 WinMM 播放，无需安装 Better Endfield 音乐模块。

普通挥刀另使用本机当前《明日方舟：终末地》客户端中的 3 条管理员普攻 SFX：`au_chr_0003_endmin_attack1/2/3` 各一个随机变体，对应 Media 929949674、198925091、484159784。离线解码为 WAV，并使用同一 PCM 播放链路；不调用游戏原声事件。资源属于终末地相应权利人，不受本项目源码许可证覆盖。原始 PCK、BNK、WEM 和游戏配置不随包分发。

三段根据当前有效 VFS 的 Wwise v150 可达 Sound/sourceID 重新核验。Bank 与媒体分置 Audit 命名空间的 banks/stream 两包；不将旧缓存与当前 manifest 认作一致。见 [普通挥刀来源](research/endfield-swings/README.md)。

## 研究工具

- [MHW Anim Tools](https://github.com/wowo-dot/mhw-anim-tools)，作者 wowo，MIT License。用于解析和烘焙动作；工具本身不随运行包提供。
- [MHW Model Editor](https://github.com/chikichikibangbang/MHW_Model_Editor)，用于参考 MOD3 骨架格式和导入流程；工具本身不随运行包提供，许可证以其仓库为准。
- [vgmstream](https://github.com/vgmstream/vgmstream)，用于将音效包中的 WEM 离线解码为 16-bit PCM WAV；解码工具不随运行包提供。
- [ooz](https://github.com/powzix/ooz)，本地定向解压工具；自编译 DLL 不链接或随模组发布，许可按其仓库条件。
- [MHW-Fsm](https://github.com/Strackeror/MHW-Fsm)、Construct 和 [EVWP 模板](https://github.com/zStatykz/MHW_EVWP_Template)，离线核对动作控制和装配，不随运行包发布。
- [Blender](https://www.blender.org/)，4.5 LTS 系列 用于烘焙与预览；程序不随模组提供。
- [E Mod Loader](https://github.com/Dr-hydra/Endfield-Mod-Loader)，本模块遵循其 format 1 / Native ABI 1 SDK，宿主程序不随本包提供。
- SDK 头文件和输入拦截研究来自 E Mod Loader / Better Endfield，遵循 AGPL-3.0-only；本项目源码使用相同许可证，完整文本见 `LICENSE`。
- [nlohmann/json](https://github.com/nlohmann/json)，3.12.0，Copyright (c) 2013–2025 Niels Lohmann，MIT License。单头文件保留在 `third_party/include/nlohmann/json.hpp`，编译进原生模块；完整 MIT 许可文本保留在 `third_party/licenses/nlohmann-json-MIT.txt`。

### MHW Anim Tools 的 MIT License

Copyright (c) 2026 wowo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
