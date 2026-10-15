# 本地资源工具链准备

源代码构建不需要游戏、Blender、BE 或资源缓存，见 [开发说明](DEVELOPMENT.md)。本文只适用于从已取得的本地资源重建数据。工具不随本项目发布，预检不会下载、启动游戏、修改客户端或计算产物哈希。

## 预检入口

在本项目根目录运行：

```powershell
python -m pip install -r requirements-dev.txt
python -m pip install pycryptodome==3.23.0
python tools/check_resource_toolchain.py --be-root '<BE资源工具目录>' --report output/toolchain-preflight.json
```

可用 `--stage python`、`--stage endfield`、`--stage mhw`、`--stage audio` 单独检查或重复组合。缺少文件、空文件、Python 包版本不符、.NET 运行时缺失都返回非零退出码，并列明实际检查路径。`--root` 可预检另一份素材工作区。报告只证明文件和运行时可用，不证明解析器 ABI、游戏数据、许可证或几何结果兼容。

BE 根目录优先使用 `--be-root`，其次环境变量 `MHE_BE_ROOT`，最后默认为本项目同级的 `Better Endfield` 目录。当前提取入口 `refresh_endfield_profile_inputs.py` 的 `--resource-tools` 必须传相同目录；预检不会改写它的参数。`endfield_profiles.py` 的当前生成路径只消费显式 `--input-root` / `--graphs` / 证明报告和本项目 Inspector，不再读取硬编码游戏表路径；其他工具导入的历史 CACHE 也遵循 `MHE_BE_ROOT`。

本流程验证版本为 Python 3.12，离线数值工具按 `requirements-dev.txt` 锁定 NumPy 2.2.5、SciPy 1.15.2、Pillow 12.3.0；预检的最低解释器检查不代表这些固定依赖已验证所有更新 Python 版本。VFS 解密另需 PyCryptodome 3.23.0。当前本机为 .NET SDK 10.0.300；SDK 10 能构建下面的 net8/net10 项目，执行 NativeAssetReader/Inspector 仍需要 .NET 8 运行时，ResConv 需要 .NET 10 运行时。

## BE、NativeAssetReader 与几何 Inspector

BE helper 基线固定为 [Better-Endfield `5c1fbc1890d6eb0831fbdebbc6d2881dae7c6e35`](https://github.com/Dr-hydra/Better-Endfield/commit/5c1fbc1890d6eb0831fbdebbc6d2881dae7c6e35)。下列 helper / Reader 均与该提交的已跟踪文件一致；仅取得仓库不能自动得到 ignored 工具链：

| 用途 | 相对 BE 根目录的路径 |
| --- | --- |
| 有效 VFS 与表刷新 | `scripts/RefreshEndfieldResourceInputs.py`、`scripts/workspace_config.py` |
| 定向 Bundle 闭包 | `tools/CustomModel/extract_native_bundles.py` |
| 原生引用图 | `tools/CustomModel/NativeAssetReader/NativeAssetReader.csproj` |
| 有界后端补丁 | `tools/CustomModel/prepare_native_backend.py` |
| 解密依赖 | `toolchains/EndfieldUnpacker/decrypt_vfs.py`、`config.py`、`decode_sparkbuffer.py` |
| 表和 manifest 解码 | `toolchains/FkArkEnd/ResConv/ResConv.csproj` 及其同级 Beyond 项目 |
| Reader 与兼容后端部署 | `toolchains/native-asset-reader/identities/` |

已通过本地源树逐文件比较核定下列来源。文本仅归一 CRLF，二进制直接比较，不计算产物哈希：

| 依赖 | 固定来源与补丁 | 核对结果 |
| --- | --- | --- |
| AnimeStudio 后端 | [EIHRTeam/EndfieldStudio `87b017727bf1301a5813b5d4ac28d278027bf756`](https://github.com/EIHRTeam/EndfieldStudio/commit/87b017727bf1301a5813b5d4ac28d278027bf756) 加 BE 的 `bounded-vfs-blocks-v1` | 已修补源树 187 个文件一致 |
| ResConv / FkArkEnd | [shiikwi/FkArkEnd `04e3384ffedd755f664fc77ec4edee1445d9515d`](https://github.com/shiikwi/FkArkEnd/commit/04e3384ffedd755f664fc77ec4edee1445d9515d) | 35 个已跟踪文件一致 |
| EndfieldUnpacker | [endGuaGua/EndfieldUnpacker `3017a799d2920a279c2175e9db80033883391f0c`](https://github.com/endGuaGua/EndfieldUnpacker/commit/3017a799d2920a279c2175e9db80033883391f0c) 加 [选择性 VFS 补丁](../tools/patches/endfield-unpacker-selective-vfs.patch) | 187 个已跟踪文件中仅选择过滤扩展不同，补丁保留此差异 |

以下命令从空 BE 根目录开始。已有该 BE 工作树可从外部依赖 clone 开始；其他已有工作树使用独立目录后显式传参：

```powershell
$beRoot = '<BE资源工具目录>'
$mheRoot = (Get-Location).Path
git clone -c core.autocrlf=false --no-checkout https://github.com/Dr-hydra/Better-Endfield.git $beRoot
git -C $beRoot checkout --detach 5c1fbc1890d6eb0831fbdebbc6d2881dae7c6e35
git clone -c core.autocrlf=false --no-checkout https://github.com/EIHRTeam/EndfieldStudio.git "$beRoot/toolchains/EndfieldStudio"
git -C "$beRoot/toolchains/EndfieldStudio" checkout --detach 87b017727bf1301a5813b5d4ac28d278027bf756
git clone -c core.autocrlf=false --no-checkout https://github.com/shiikwi/FkArkEnd.git "$beRoot/toolchains/FkArkEnd"
git -C "$beRoot/toolchains/FkArkEnd" checkout --detach 04e3384ffedd755f664fc77ec4edee1445d9515d
git clone -c core.autocrlf=false --no-checkout https://github.com/endGuaGua/EndfieldUnpacker.git "$beRoot/toolchains/EndfieldUnpacker"
git -C "$beRoot/toolchains/EndfieldUnpacker" checkout --detach 3017a799d2920a279c2175e9db80033883391f0c
git -C "$beRoot/toolchains/EndfieldUnpacker" apply --check "$mheRoot/tools/patches/endfield-unpacker-selective-vfs.patch"
git -C "$beRoot/toolchains/EndfieldUnpacker" apply "$mheRoot/tools/patches/endfield-unpacker-selective-vfs.patch"
```

clone 的局部 `core.autocrlf=false` 保留 LF，避免 Windows 全局换行设置破坏补丁应用；不改用户全局 Git 配置。保留各上游已有的许可证和通知。固定 FkArkEnd 提交未提供 LICENSE / COPYING / NOTICE，本项目不替它指定许可证。BE 后端 staging 脚本只复制必需编译输入，不自动复制上游根目录 LICENSE，应另外保留原 checkout 的许可证。已有且通过当前目标解析验证的同源后端也可直接复用；任意同名 DLL 或最新上游 HEAD 不视为等价。

从兼容、未补丁的完整 AnimeStudio 源树准备新后端时，目标目录必须为空，保留 source 中的 `Directory.Build.props`、`AnimeStudio/` 与 `AnimeStudio.Libraries/AnimeStudio.Ooz.dll`：

```powershell
$beRoot = '<BE资源工具目录>'
$animeSource = Join-Path $beRoot 'toolchains/EndfieldStudio'
$mheRoot = (Get-Location).Path
$backendSource = Join-Path $mheRoot 'output/toolchain/backend-source'
$backend = Join-Path $mheRoot 'output/toolchain/backend'
python "$beRoot/tools/CustomModel/prepare_native_backend.py" --source $animeSource --output $backendSource
dotnet build "$backendSource/AnimeStudio/AnimeStudio.csproj" -c Release -f net8.0 -p:CopyLocalLockFileAssemblies=true -o $backend
dotnet build "$beRoot/tools/CustomModel/NativeAssetReader/NativeAssetReader.csproj" -c Release "-p:AnimeStudioDir=$backend" -o "$beRoot/toolchains/native-asset-reader/identities"
dotnet build tools/endfield_weapon_inspector/EndfieldWeaponInspector.csproj -c Release "-p:AnimeStudioDir=$backend"
dotnet build "$beRoot/toolchains/FkArkEnd/ResConv/ResConv.csproj" -c Release
python tools/check_resource_toolchain.py --be-root $beRoot --stage endfield
```

若已有经过 `bounded-vfs-blocks-v1` 修补的 source，不要重复运行补丁器；从第一条 `dotnet build` 开始即可。已有后端仅需传入其目录，重建 Reader 和 Inspector。Inspector 缺少后端时会在引用解析前给出明确错误。

本机已修补源树在 BE 根目录下 `research/custom_model/1.5.3/Windows/native-parser/source`；`backend-patch.json` 记录补丁 `bounded-vfs-blocks-v1` 与 512 MiB 单块上限。现用 DLL 的 FileVersion 为 `1.1.0`，ProductVersion 后缀 `72700055cb4267f5b21ec315702d38fe5577d0c9` 来自当时 BE 构建工作树，不能当作 AnimeStudio 上游 revision。上表的固定来源由源文件比较核定，不能仅靠程序集版本号推断。工具源获取和局部构建可复核，但本轮没有从全新游戏输入执行整个烘焙流水线；完整首次素材重建仍需在用户本地验证。

提取命令显式传入 `--unpacker`、`--resconv`、`--game` 与空 `--output`；刷新配置用 BE 的 workspace 配置指向实际游戏。始终以 StreamingAssets 后叠加 Persistent，只提取目标资源依赖闭包，不做全量导出。提取后执行项目 `verify_resource_overlay.py` 和当前三表验证，才可生成新 profile。

## MHW 解包、烘焙和音频

以下是现有脚本消费的目录布局；保留上游许可证。上游更新可能改变 API，首次安装后须运行解析、烘焙和源姿势核验。

| 工具 | 获取入口 | 本项目预期位置 |
| --- | --- | --- |
| MHWNoChunk | [固定 `427fced319f6e412a49fc5860a8eb4d634ddf10c`](https://github.com/JodoZT/MHWNoChunk/commit/427fced319f6e412a49fc5860a8eb4d634ddf10c) | `local/tools/mhw-nochunk/MHWNoChunk/Chunk.cs` 与 `Resources/chunk.key` |
| ooz | [固定 `05038060aa68f9187ae9923b2388ca8db40e58d1`](https://github.com/powzix/ooz/commit/05038060aa68f9187ae9923b2388ca8db40e58d1) | `local/tools/mhw-ooz-source/`；编译下方专用 wrapper |
| Blender | [官方 4.5.14 Windows x64 ZIP](https://download.blender.org/release/Blender4.5/blender-4.5.14-windows-x64.zip) | `local/tools/blender/blender-4.5.14-windows-x64/blender.exe` |
| MHW Model Editor | [固定 `b0c45ac7ebe6a51f4db4c95c7aa2926381e77740`](https://github.com/chikichikibangbang/MHW_Model_Editor/commit/b0c45ac7ebe6a51f4db4c95c7aa2926381e77740) | `research/mhw-longsword/20261012/tooling/MHW_Model_Editor/` |
| MHW Anim Tools | [固定 `38967df523d4ec3b317b7c41be756be559a94f96`](https://github.com/wowo-dot/mhw-anim-tools/commit/38967df523d4ec3b317b7c41be756be559a94f96) | `research/mhw-longsword/20261012/tooling/mhw-anim-tools/` |
| vgmstream | [官方 r2117 Windows ZIP](https://github.com/vgmstream/vgmstream/releases/download/r2117/vgmstream-win64.zip) | `local/tools/vgmstream/vgmstream-cli.exe` 及同包 DLL |
| wwiser | [固定 `08e937b649bc7007c234bbd59e34a41d5eef3b38`](https://github.com/bnnm/wwiser/commit/08e937b649bc7007c234bbd59e34a41d5eef3b38) | `local/tools/wwiser-source/wwiser-master/wwiser.py` 及同树 Python 模块 |

源码工具使用各表格链接对应仓库 clone 后 `git checkout --detach <完整revision>`，或下载 `https://codeload.github.com/<owner>/<repo>/zip/<完整revision>`；解压时去掉外层 `<repo>-<revision>/`，按第三列放置。ooz 测试数据较大，当前 wrapper 只需要该固定提交的 `kraken.cpp`、`bitknit.cpp`、`lzna.cpp`、`stdafx.h`、`targetver.h`，以及应保留的 `README.md`；可从对应提交的 raw 路径定向取得这六个文件。

本机核证覆盖 Anim Tools 225 文件、wwiser 114 文件、ooz 六个构建源和 vgmstream 固定包的 15 文件。Model Editor 的 257 文件中，备用材质名字典曾损坏；本轮已用上述官方提交的原 `addons/MHW_Model_Editor/modules/mrl3/dict/various_hash_dict.json` 替换，原件留在 ignored 备份，真实字典加载函数成功读取 266,665 项。Blender 本机报告 4.5.14 LTS / 内嵌源码 revision `62c1db4208e8`，本轮未重新下载整包逐字节比较。

在 VS 2022 x64 Native Tools PowerShell 中，从仓库根目录编译独立解压 DLL。这不是游戏注入模块，也不会链接到模块：

```powershell
$mheRoot = (Get-Location).Path
Push-Location local/tools/mhw-ooz-source
try {
    cl /nologo /LD /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS kraken.cpp bitknit.cpp lzna.cpp "$mheRoot/tools/mhw_ooz_wrapper.cpp" /link /OUT:mhe_ooz.dll
} finally { Pop-Location }
python tools/check_resource_toolchain.py --stage mhw --stage audio
```

`mhe_ooz.dll` 必须导出 `MHE_Decompress`，参数与项目 wrapper 一致。不能改名其他 `ooz.exe` 或 Oodle DLL 来替代。Model Editor 目录下必须有 `addons/MHW_Model_Editor/modules/mod3/mod3_functions.py`；Anim Tools 必须有 `core/formats/lmt/reader.py` 与 `core/formats/timl/reader.py`，不能把 GitHub 压缩包多包一层目录直接放入。

以上是已核定的固定来源，本项目没有替这些上游宣称任意最新版本直接兼容；工具可用后仍须执行 [重建流水线](RESOURCE_REBUILD.md) 的实际格式和离线网格验证。本轮未在空白机器执行两个游戏的全部资源重建。预检不会访问 chunk.key 内容，也不输出解密材料。
