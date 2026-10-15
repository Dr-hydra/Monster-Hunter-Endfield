# 1.6.3 rc2 缺口处理

当前开发版本 `1.0.0-compat-1.6.3-rc2`。基于 rc1 公开适配，继续处理运行时及资源工具审查发现。尚未进行游戏内验证；本记录只描述公开构建。

## 运行时修复

- 特殊独立模型的 `get_localBounds/set_localBounds` 改为在 `UnityEngine.Renderer` 上解析；严格测试校验声明类型、参数和返回值。它是历史声明错误，不是 1.6.3 才移动的方法。
- 模块 bootstrap 原生线程在执行托管解析/分配前注册 IL2CPP；借用已有注册时不擅自释放，自行注册时在成功、失败和异常退出时释放。
- 禁用后立即启用接受“等待上一轮主线程清理”的状态，旧姿势恢复及 lease 释放后再读取新素材、恢复输入。不会在 worker 强行清理 Unity 对象。
- 新生命周期测试调用真实模块入口和记录的 Tail/Input 回调指针，覆盖重试、快速切换、取消等待和延迟读取失败；仍不等于实机并发/GC验收。

## 构建与验证入口

公开源码新增 `tools/check_source.ps1` 及 Windows GitHub Actions，执行公开 DLL 构建、CTest `source` 标签的无素材回归、合成 Python 回归、两组网页规则检查。Python 测试依赖固定在 `requirements-dev.txt`；无需 npm 构建。

完整素材测试标记为 `local-assets`，与干净检出的源码检查分开。无素材绿灯不表示素材包合格，素材包合格也不表示游戏内效果通过。

完整运行包的本地输入与工具条件见 [素材重建](../RESOURCE_REBUILD.md)。
