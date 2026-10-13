# 第三方来源说明

本项目是研究性质的动作展示模块，与 CAPCOM 或鹰角网络无隶属关系。项目源代码许可证不覆盖游戏资源与他人制作的模组资源。

## 动作与骨架资源

动作来自用户提供的 `RiseStyleLongSowrd No GUI Version-7113-1-6-1729133246.rar`，骨架参考来自用户提供的 `ZXC.MHW.10.25.33.rar`。`ValorLongSword 0.16.33 8848 1 2026-10-12T08-57Z NYHNOo2J9.zip` 用于研究对照。研究过程保留原始来源名称；本包仅包含由源骨架烘焙出的动作数据和离线预览，不附带原始模型、纹理、LMT 或 MOD3。

《怪物猎人》相关资源版权属于 CAPCOM 及相应权利人，改制动作和外观模组的权利归相应作者。本地持有压缩包不代表获得了对外重新分发授权；发布含烘焙动作的包前，应确认原资源与模组作者允许的使用和分发范围。

《明日方舟：终末地》相关名称、角色与游戏资源属于其相应权利人。本模块不附带女管理员模型或游戏程序。

## 音效

音效来自用户提供的 `太刀见切音效增强.zip` 内 `wp03_swo_epvsp.nbnk`，使用其中 11 条解码媒体，不附带原始 bank。用户说明该包可能对部分怪猎原声提高分贝；本项目没有将原 bank 的 Wwise 容器音量烘焙进 WAV，展示音量单独调整。招式标签参考用户提供的上木光子 / bilibili 光子Proton 于 2020/7/13 制作的太刀音效对照表，原作者提示可能有误。音效资源的权利归其相应权利人，不受本项目源码许可证覆盖。

音频处理参考 Better Endfield 的 `music/module.cpp` 与 `music/local_track.inc` 中的 PCM 调度、软件音量和退出清理方式；本模块短音效使用独立 WinMM 播放，无需安装 Better Endfield 音乐模块。

普通挥刀另使用本机当前《明日方舟：终末地》客户端中的 3 条管理员普攻 SFX：`au_chr_0003_endmin_attack1/2/3` 各一个随机变体，对应 Media 929949674、198925091、484159784。按用户要求离线解码为 WAV，并使用同一 PCM 播放链路；不调用游戏原声事件。资源属于终末地相应权利人，不受本项目源码许可证覆盖。原始 PCK、BNK、WEM 和游戏配置不随包分发。

## 研究工具

- [MHW Anim Tools](https://github.com/wowo-dot/mhw-anim-tools)，作者 wowo，MIT License。用于解析和烘焙动作；工具本身不随运行包提供。
- [MHW Model Editor](https://github.com/chikichikibangbang/MHW_Model_Editor)，用于参考 MOD3 骨架格式和导入流程；工具本身不随运行包提供，许可证以其仓库为准。
- [vgmstream](https://github.com/vgmstream/vgmstream)，用于将音效包中的 WEM 离线解码为 16-bit PCM WAV；解码工具不随运行包提供。
- [E Mod Loader](https://github.com/Dr-hydra/Endfield-Mod-Loader)，本模块遵循其 format 1 / Native ABI 1 SDK，宿主程序不随本包提供。
- SDK 头文件和输入拦截研究来自 E Mod Loader / Better Endfield，遵循 AGPL-3.0-only；本项目源码使用相同许可证，完整文本见 `LICENSE`。
- [nlohmann/json](https://github.com/nlohmann/json)，3.12.0，Copyright (c) 2013–2025 Niels Lohmann，MIT License。单头文件保留在 `third_party/include/nlohmann/json.hpp`，编译进原生模块；其 MIT 许可条件与下方全文一致。

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
