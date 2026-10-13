# 0.6.0 开发包：实际 EML 隔离导入与 OfflineHost 验证

最终候选包 `dist/endfield.longsword.showcase-0.6.0-dev-win-x64.zip`（8,200,525 字节）已经通过真实 EML 导入与离线宿主验证。这不是仅检查 ZIP 文件列表。所有操作在 G 盘项目的独立验证目录执行，没有修改用户正式模组库、游戏配置、源码或候选包，没有计算产物哈希。

## 使用的实际组件

验证程序位于 `local/verification/eml-harness`，以 ProjectReference 直接引用 `G:/Endfield Mod Loader/src/EModLoader.Core/EModLoader.Core.csproj`，调用实际 `ModuleStore.ImportAsync`。每轮创建新的 `local/verification/.../isolated-library`，模块状态和认证令牌仅保留本地；未输出令牌或把索引加入 Git。

导入后通过实际 `HostConnection.StartOfflineAsync` 启动 `G:/Endfield Mod Loader/build/native/windows/Release/EModLoader.OfflineHost.exe`。该宿主的 ModuleRuntime 实际加载导入目录中的原生 DLL，核对模块入口与 ABI 并调用 initialize；通信使用宿主实际 HTTP `/send` 与 `/poll`，配置由实际 `WebBridge.DispatchAsync(..., "saveConfig", ...)` 写入隔离库并经 `/configure` 通知 DLL。

## 验证结果

| 检查 | 结果 |
| --- | --- |
| ModuleStore.ImportAsync 实际解包、清单和原生库/UI 验证 | 通过；ID `endfield.longsword.showcase`，版本 `0.6.0-dev`，Supported=true |
| 独立库只含本次导入模块、导入路径处于该库 | 通过 |
| 实际 OfflineHost 加载 DLL 与 initialize | 通过；宿主模块状态 ready，error 为空 |
| 原生 status 消息与 reply/poll | 通过；active=false，runtime_ready=false，phase=waiting |
| 包内音频资源加载 | sound_ready=true；仅代表资源读取，未在游戏中试听 |
| WebBridge 保存配置 | 通过；隔离库保存 sound_volume=0.42、laevatain_blade=false |
| 配置送达实际 DLL | 后续原生 status 返回 laevatain_blade=false |
| demo / toggle 在无 Unity 条件下 | 正确返回 BE_Result_NotReady=2，没有虚构启动成功 |
| 原生 stop 消息 | 返回 BE_Result_Ok=0，active 保持 false |
| OfflineHost.StopOfflineAsync | 通过；自有进程停止，HTTP listener 不再响应；无遗留 OfflineHost 进程 |

宿主状态 ready 表示本模块 DLL 初始化成功；`runtime_ready=false` 表示离线宿主没有终末地 Unity 游戏运行时。两者应分别解释。此轮没有验证游戏内角色绑定、动画还原、莱万汀长刀显示、shader/火焰、场景接地或真实输入。

## 本地记录与复现

成功结果保存在 `local/verification/eml-060-20261014-b/result.json`，宿主日志在同目录 `offline-host.log`，命令输出在 `local/verification/eml-060-20261014-b-console.log`。记录中的日期是目录标识，未用它推断资源版本。

初轮 `...-a` 的验证器汇总把仍有父节点的 JsonNode 放入新容器时失败。那是验证器记录代码的问题；已对收集的 reply 使用 DeepClone，再创建全新隔离库重跑完整流程。没有修改候选包来绕过失败。

```powershell
dotnet build local/verification/eml-harness/EmlVerification.csproj -c Release
dotnet local/verification/eml-harness/bin/Release/net10.0/EmlVerification.dll "G:/Monster Hunter Endfield/dist/endfield.longsword.showcase-0.6.0-dev-win-x64.zip" "G:/Endfield Mod Loader/build/native/windows/Release/EModLoader.OfflineHost.exe" "G:/Monster Hunter Endfield/local/verification/new-unused-run-directory"
```

最后一个目录必须未存在；验证器禁止覆盖既有库，并检查输出位于本项目 `local/verification`。验证结束自动关闭其自有宿主。
