# 离线候选包：真实EML隔离导入与宿主验收

使用实际 E Mod Loader 的 `ModuleStore.ImportAsync`、`WebBridge.DispatchAsync` 与 `HostConnection`，在本项目 `local/verification` 下新建隔离库。没有改用户正式模组库、启动游戏／游戏服务端或计算产物哈希。

## 已记录的验证流程

先导入原 `0.6.0-dev`，保存自定义配置和启用状态，再升级到离线候选包（`1.0.0-offline-rc1` 与 `1.0.0-combat-rc2` 分别留有验证记录）。模块数保持1，新generation生效，旧配置逐字段及启用状态保留。实际OfflineHost加载新DLL，ABI与initialize通过。

六模式逐个发送 `set_mode`，全部加载且 `sound_ready=true`；保存配置经真实WebBridge落入隔离库并送达DLL。非法模式保留原状态。无Unity时demo、toggle、pause、resume、preview正确返回NotReady=2，没有伪造游戏启动成功；stop返回Ok=0。模式NotFound=3按当前SDK处理。

停止自有宿主后，验证停用／启用，再重新导入同包：只保留一个模块记录，配置不丢失，实际新宿主再次initialize成功。最后禁用、RemoveAsync移除记录；新宿主为空。所有自有宿主结束，HTTP listener停止。

宿主ready表示DLL初始化成功；`runtime_ready=false`表示没有游戏Unity环境。此检查不证明游戏内绑定、渲染、原生特效、真实输入或动画最终外观。

## 记录和复现

审查包记录：`local/verification/eml-rc1-review-20261014/result.json`。combat-rc2 记录：`local/verification/eml-combat-rc2-20261014/result.json`。历史最终 ZIP 记录：`local/verification/eml-rc1-final-20261014/result.json`；日志保留同目录及外层console文件，认证令牌和库索引不上传Git。验收程序已入源码 `tools/eml_verification`，直接引用实际EML Core，不用SDK mock替代。

复现需要 .NET 10 SDK、实际 EML 源码及其 OfflineHost 构建。先将环境变量 `MHE_EML_ROOT` 设置为本地 EML 源码目录；以下命令在本仓库根目录执行：

```powershell
$mheRoot = (Get-Location).Path
$mheEmLRoot = $env:MHE_EML_ROOT
if (-not $mheEmLRoot) { throw '请先设置 MHE_EML_ROOT 为 EML 源码目录' }
dotnet build tools/eml_verification/EmlVerification.csproj -c Release "-p:EMLProjectRoot=$mheEmLRoot"
$mheZip = Join-Path $mheRoot 'dist/endfield.longsword.showcase-1.0.0-combat-rc2-win-x64.zip'
$mheHost = Join-Path $mheEmLRoot 'build/native/windows/Release/EModLoader.OfflineHost.exe'
$mheRun = Join-Path $mheRoot 'local/verification/new-unused-run'
dotnet tools/eml_verification/bin/Release/net10.0/EmlVerification.dll $mheZip $mheHost $mheRun
```

可选第四参数为旧版 ZIP 路径；提供旧包时执行真实升级保留检查。EML源码位置可通过MSBuild属性 `EMLProjectRoot`调整。验证输出目录必须尚不存在，并位于项目local/verification内；不覆盖既有库。EML退役包的物理目录遵循其自身保留政策，移除验收只验证模块记录与新宿主生命周期。

当前发行包通过紧凑银行和未使用副本排除兼容现有4096条目／256MiB总解包／128MiB单条目限制，没有删动作／音效或放宽Loader。银行坏输入、路径和限额同时由原生与打包测试核对；若后续规模确需扩展EML，可以连同容量边界验证一起调整。
