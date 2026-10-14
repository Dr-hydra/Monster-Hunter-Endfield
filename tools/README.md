# 研究与验证工具

工具读取本机游戏输入和本地构建产物。原始游戏资源、解包数据、烘焙动作、音频、预览和运行日志不进入源码仓库；参见 [资源重建说明](../docs/RESOURCE_REBUILD.md) 和 [第三方来源](../THIRD_PARTY_NOTICES.md)。

研究文档中的 `<MHW_ROOT>`、`<ENDFIELD_ROOT>` 和 `<BE_ROOT>` 分别表示本机《怪物猎人：世界／冰原》安装、终末地安装和 Better Endfield 源码目录，不要求固定盘符。

## EML 离线验收

`eml_verification` 直接引用 E Mod Loader Core，验证隔离安装、OfflineHost 初始化和消息／配置生命周期。需要 .NET 10 SDK、E Mod Loader 源码、已构建的 `EModLoader.OfflineHost.exe` 和本项目的完整本地安装包。它不启动游戏；离线结果不能替代游戏内验收。

默认将 E Mod Loader 源码放在本项目的同级目录 `Endfield Mod Loader`。目录布局不同时，用 `-p:EMLProjectRoot` 覆盖源码位置。以下命令从本项目根目录执行；将输入变量改为本机实际路径：

```powershell
$projectRoot = (Get-Location).Path
$emlRoot = (Resolve-Path '../Endfield Mod Loader').Path
$archive = (Resolve-Path 'dist/endfield.longsword.showcase-1.0.0-combat-rc2-win-x64.zip').Path
$offlineHost = Join-Path $emlRoot 'build/native/windows/Release/EModLoader.OfflineHost.exe'
$run = Join-Path $projectRoot 'local/verification/new-unused-run'

dotnet build tools/eml_verification/EmlVerification.csproj -c Release "-p:EMLProjectRoot=$emlRoot"
dotnet tools/eml_verification/bin/Release/net10.0/EmlVerification.dll "$archive" "$offlineHost" "$run"
```

可选的第四个参数是旧版安装包路径，用于核验升级保留启用状态和配置。验收程序先从安装包的祖先目录定位本项目；安装包位于项目树外时，回退到验收程序自身所在的项目树。定位要求同时存在 `module.json`、`CMakeLists.txt` 和验收项目文件。

输出必须是该项目 `local/verification/` 下尚不存在的子目录；现有目录、文件和途经符号链接／目录联接的路径会被拒绝。程序仅创建隔离模块库并写入该次运行的结果及日志，不能指定正式 EML 模块库为输出位置。
