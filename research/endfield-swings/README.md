# 当前终末地普通挥刀原声

2026-10-13，从本机终末地安装 `<ENDFIELD_ROOT>` 的有效 VFS 读取并离线解码，
没有启动游戏、服务端或播放 Wwise 游戏事件。

| 普攻来源 Event | FNV-1 Event/Bank ID | 选用 Sound sourceID | 输出 WAV | 时长 |
| --- | ---: | ---: | --- | ---: |
| `au_chr_0003_endmin_attack1` | 1747210745 | 929949674 | `endfield_swing_1.wav` | 1.55325 s |
| `au_chr_0003_endmin_attack2` | 1747210746 | 198925091 | `endfield_swing_2.wav` | 1.349208 s |
| `au_chr_0003_endmin_attack3` | 1747210747 | 484159784 | `endfield_swing_3.wav` | 1.690208 s |

三段均为 **48 kHz、双声道、PCM16**，帧数分别为 74556、64762、81130。
可复制到模组 `assets/audio` 后走同一 `AudioPlayer` PCM 链路。
普通斩 101–103 的触发点由 `tools/merge_endfield_swings.py` 合并到
`assets/audio/cues.json`。

## 核验过程与实际包关系

按 VFS 覆盖顺序、FNV-1、Wwise v150 Sound `sourceID` 与索引关联定向读取。
有效 VFS 次序是 StreamingAssets 后
Persistent 覆写。只解当前 manifest 的 Brotli 头与当前 AudioDialog，
读取音频 PCK 索引，并提取上述三个 Bank 和三段 WEM；没有全量资源导出。

三个 Bank 均是 Wwise **v150**，`BKHD.bankID`、计算得到的 Event ID 与唯一
HIRC Event 对象 ID 一致。只从可达 Sound 对象体偏移 **9** 的 `sourceID`
字段读取媒体，不把其他整数当 Media ID。

原版的实际布局将 Bank 和流媒体分开，不能声称它们来自同一个物理 PCK：

- Bank：`Audio/PCK/Windows/Audit/audit_banks.pck`，418 个 Bank，无媒体。
- 媒体：`Audio/PCK/Windows/Audit/audit_stream.pck`，1634 个媒体。
- 配对约束：同一当前 **Audit 包命名空间**与 **`s` 非本地化 SFX 语言**；
  三个 sourceID 在该流包索引中各唯一存在。

有效 Persistent 条目引用的实际 CHK 可以位于 StreamingAssets；读取的是
当前覆写条目的 chunk identity，而非按目录猜测旧包。
完整 Event→HIRC→Sound→媒体范围见 `mapping.json`（本地生成）。

这些是 `attack1/2/3` 的已确认音效变体；原 Event 还带随机/切换结构，
不等于重现整个原游戏 Wwise 事件。三个 Event 分别有 4、4、3 个可达
sourceID，选用本项目之前使用的三个变体，并在当前 Bank 中重新验证。
没有选取 `*_sv` 语音 Event 或另一个命中 Event；当前 AudioDialog 中也无这
三个 SFX 的精确语音行。这里的角色/招式名称来自已知 SFX Event 别名经 ID
核对，不根据无标签 Bank 臆测角色。

## 版本证据

当前 manifest `Version` 仍为 `2954fa80-23c1-1579-2b22-4ecfd6d70418`。
但当前 manifest 内置内容标识为 `61f57069d1c1c9a09891151c6af0d8dc`，旧 BE
缓存为 `8b9cf90e2097d4b4767c76ae00807eb6`，原始 manifest 字节并不一致。
提取使用当前 VFS 的头部，不把旧缓存认定为当前数据。AudioDialog 当前
资源与 BE 缓存直接字节一致。内置标识是游戏原有字段；验证没有计算文件
或产物哈希。

当前元数据与逐路径证据在 `current-metadata.json`（本地生成） 和
`package-routing-evidence.json`（本地生成）。初次严格缓存
比较留下的 `metadata-mismatch.json` 记录这项差异，不代表最终提取失败。

## 重复生成

```
python tools/endfield_extract_swings.py --game "<ENDFIELD_ROOT>" --be "<BE_ROOT>"
```

将命令中的路径占位符替换为本机实际目录。需要 Better Endfield 的 VFS/HIRC
解析器、PyCryptodome、Brotli 与 vgmstream。此记录使用 Brotli 1.2.0，位于
忽略的 `local/python-deps/endfield-swings`；只用于解析 manifest 头。
vgmstream 复用 `local/tools/vgmstream/vgmstream-cli.exe`（作者 release r2117）。
三条解码日志保留在本目录。Bank/WEM/WAV 都是本地研究输入，未提交到
Git，也不要把原 PCK/BNK/WEM 随源代码发布。
