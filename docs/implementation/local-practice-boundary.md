# 本地练习与通信边界审查

该记录只做源码与最终原生库依赖检查，没有启动游戏、服务端或抓取游戏流量。要求为模块不新增真实战斗发送／服务端写入调用；游戏和宿主自身的正常通信不由这一结论保证为零。

## 实际调用路径

- `native/module.cpp` 的怪物快照只读取 `ObjectContainer.get_allEnemies`、`Entity.get_position`、`get_instanceUid`、`get_name` 与对象有效性；没有读取后再写回真实 HP、AI 或战斗状态。
- `Practice.Refresh/Update/Strike` 的 HP、命中去重、计数与统计均位于模块自己的 `PracticeTarget` 容器。默认配置 `local_practice=false`；开启后也只修改这个独立容器。
- 弓箭、轻弩弹体、练习靶与命中反馈由模块创建并回收。原生解析的 gameplay 方法只用于角色／怪物读取和 View 层武器显隐优先级，未解析真实 Attack、Damage、RPC、奖励、掉落或存档接口。
- 位移通过角色显示模型的 Transform 与骨骼应用；场景 Raycast／SphereCast 用于视觉接地与限位。没有调用实体位置 setter。显示位置、骨骼、装备、输入、声音及道具通过统一生命周期恢复。
- EML 的消息／配置回执通过宿主 bridge 传递。最终 OfflineHost 检查会使用本机 HTTP，但这是隔离宿主通信，不是游戏战斗请求；用户服务器与正式模组库不参与验证。

## 库和链接检查

构建只显式链接 `user32`、`winmm`、`gdi32` 与 Windows 基础运行库。最终 DLL `/imports` 保存于 `build/development/final-native-imports.log`；未导入 Winsock、WinHTTP、WinINet、libcurl 或游戏网络库。连同源码中所有动态解析的方法表，这支持“模块没有新增战斗发送接口”的静态结论。

该审查不能替代正式游戏运行中的通信对照，也不能保证游戏原有代码不会对展示状态作正常同步。后续实机验证仍需核对显示模型与真实实体隔离、输入拦截及场景切换恢复；当前尚未进行游戏内验证。

## 离线行为验证

原生回归覆盖模拟 HP、命中去重、资源、箭弹池、停用／暂停／切换清理及宿主模拟。真实 EML 隔离导入只检查 DLL 初始化、六模式资源、配置／消息及停止，不虚构游戏角色或战斗行为。最终结果统一记入 [候选版验收](offline-rc1.md)。
