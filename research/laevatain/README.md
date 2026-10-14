# 莱万汀定向研究复现

输入使用同项目 `research/expansion-20261013` 的当前 VFS 定向 weapon/character bundle 闭包和 raw 引用图。没有扩大为全资源导出。资源/TypeTree 输出仅本地保留。

```powershell
dotnet build tools/laevat_inspector/LaevatInspector.csproj -c Release -p:AnimeStudioDir="<BE_ROOT>/toolchains/native-asset-reader/identities"
dotnet tools/laevat_inspector/bin/Release/net8.0-windows/LaevatInspector.dll research/expansion-20261013/laevat-weapon-inputs research/laevatain/native-inspection
dotnet tools/laevat_inspector/bin/Release/net8.0-windows/LaevatInspector.dll research/expansion-20261013/special-weapons-inputs research/laevatain/character-inspection
python tools/laevat_profile.py
```

将 `<BE_ROOT>` 替换为本机 Better Endfield 源码目录。`native-inspection` 输出确切 Mesh TypeTree、3070 顶点、SMR TypeTree、根 WeaponModelData 与其他只读组件。`character-inspection` 输出角色根配置。`profile-evidence.json` 含精简坐标验证，不提交原始资源。

脚本从精确 `eRC` 绑定矩阵和自身骨树求值刀网格，验证 root 刀尖 +Y。不能把历史 reader 扁平数组直接作为 Unity 列主序矩阵。角色配置的 21 主骨来自主身体 palette，wep_R 来自实际右手子节点。

当前实验安装保留 prefab 根原姿态。WeaponMountPoint 100 不被假设成物理握柄，没有制造额外 90° 校正。没有启动游戏、大招或服务器。完整实现/限制见 ../../docs/implementation/laevatain-weapon.md。
