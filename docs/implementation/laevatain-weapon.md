# 莱万汀太刀：独立刀身与角色配置

日期：2026-10-13。历史刀身接入证据；当前离线版本见 [combat-rc2](combat-rc2.md)。角色、武器与材质证据来自本机当前 VFS 定向提取（manifest `2954fa80-23c1-1579-2b22-4ecfd6d70418`），没有运行游戏或服务器，没有修改游戏文件或计算产物哈希。离线检查与 SDK 宿主模拟通过，实机显示、握持与完整火焰仍待验收。

## 交付

- `profiles/laevatain.json`：`chr_0016_laevat` 主骨架完整路径、21 个主骨语义、右手挂点和内嵌 `visual_weapon`。主骨均实际存在于身体蒙皮 palette，不能用武器或子骨架代替。
- `profiles/weapons/laevatain_longsword.json`：单独可复用的武器配置，与内嵌对象相同。
- `native/visual_weapon.h`：主线程调用的纯视觉对象 helper。读取原生 prefab 的网格、共享材质、局部 Transform 和自身骨架，仅创建 GameObject/Transform/一个 SkinnedMeshRenderer；没有 Instantiate 整个技能 prefab，也不复制 Animator、WeaponAnimatorMono、EntityRenderHelper、MonoBehaviour、Collider 或战斗组件。
- `tests/visual_weapon_tests.cpp`：异步加载、退出释放、纯渲染对象、安装验证和失败回滚的 SDK 宿主模拟。
- `tools/laevat_inspector`、`tools/laevat_profile.py`：离线提取骨架/装配依据的小型复现工具。原始 TypeTree、几何和生成证据仅留本机 `research/laevatain`，不入 Git 或模组包。

## 确认的模型与坐标

资源为 `assets/beyond/dynamicassets/gameplay/prefabs/weapons/wpn_misc_0016.prefab`，当前 manifest 的路径索引值为十进制字符串 `381157020907152362`。该数字来自现有 manifest，没有自行计算资源哈希。

选取 `Mesh_Sword/lod0/S_wpn_misc_00016_02_lod0`：3070 顶点、8880 索引，1 根自身骨 `Root/wep_quan_jnt/wep_jian_jnt`，共享原生 `M_wpn_misc_0016_01` 材质。保持原材质、纹理与 shader 引用，只读共享对象。

刀的实际 Mesh 坐标长轴是 **+Z**。原始 TypeTree 的 `m_BindPose.eRC` 把它变到自身刀骨的 **+Y**，而刀骨静止旋转不改变 +Y，因此武器 prefab 根坐标的刀尖方向是 **+Y**。3070 个解码顶点经 `boneInRoot * bindpose` 求值后，沿 +Y 刀尖距原点约 **2.068854 m**，网格全长还包括负方向的柄/配件。

Renderer 自身局部位置为 `(2.717, -0.132, 0)`。它不等同蒙皮网格的真正装配位置。直接使用 `renderer.worldMatrix * Mesh.bounds` 推断蒙皮剑轴会把位置和朝向算错；实际蒙皮应通过自身骨骼矩阵与 bindpose 求值。运行 helper 直接保留游戏提供的 Mesh/bindpose，不重建网格或转置其矩阵。

历史 NativeAssetReader 的扁平矩阵使用后端字段布局，不能仅凭那个数组函数名推断为 Unity 列主序。新证据从确切 TypeTree `e00` 至 `e33` 构造矩阵，并与顶点和 SMR AABB 交叉验证。

莱万汀 `wep_R` 是主骨架右手的直接子节点，局部位置 `(-0.050087966, 0.04988098, 0.0018067264)`，局部四元数约 `(0.7225668, -0.0022813526, 0.0034098057, 0.69128895)`。配置从实际资源读取，不复制女管理员的手部偏移。

## 安装证据的边界

Prefab 的 WeaponModelData 存在 `weaponMountPoints[100] → wep_jian_jnt`。此字典用于 WeaponMountPoint，不是“右手安装矩阵”的证明；当前枚举资料没有确认 100 为物理握柄。**没有把该刀骨 90° 旋转取逆后当作武器修正。**

当前模式为 `experimental-authored-prefab-root-at-wep_R`：保留 prefab 根作者变换（位置 0、旋转 identity），挂到资源定义的 `wep_R`，保持全部刀自骨与 renderer 作者变换。`mount_runtime_verified=false`，尚未观测真实技能的 view-part 附着配置或实机握持。helper 会复核安装参考的实际姿态与 profile；不一致时停止独立刀路径，交由主模块保留原装备。

由此可验证模型独立、自身骨完整和纯视觉生命周期；不能声称已经还原了莱万汀大招的全部火焰、环、链或技能安装。在资源/实机证据补齐前，不添加额外剑轴角度纠正。

## SDK 接口与生命周期

`LS::VisualWeapon::ManagedVisualWeapon`：

```cpp
bool Bind(const BE_HostApiV1*, const Json& visualWeaponProfile);
bool Tick(void* parentSocket, float realFrameDelta);
void Reset(); // 主线程：退出、失焦、换人、换模型、卸载
bool Ready() const;
bool SetVisible(bool visible);
bool Visible() const;
bool Available() const;
void* RootTransform() const;
Vec3 BladeAxis() const; // prefab 根坐标
Vec3 EdgeAxis() const;
float Length() const;
const std::string& Status() const;
```

加载签名来自 `Common.Beyond.dll / Beyond.Resource`：

| 类与方法 | 参数 | 返回 |
| --- | --- | --- |
| I18NAssetLoader.LoadAsync | StringPathHash, System.Type, RootCategory | FAssetProxyHandle |
| FAssetProxyHandle.IsValid | 无 | System.Boolean |
| FAssetProxyHandle.get_isDone | 无 | System.Boolean |
| FAssetProxyHandle.get_hasError | 无 | System.Boolean |
| FAssetProxyHandle.Get | 无 | UnityEngine.Object |
| FAssetProxyHandle.Dispose | 无 | System.Void |
| ResourceManager.IsInitialized / IsAsyncIniting | 无 | System.Boolean |

RootCategory 底层为 Byte，Main=0 沿用 BE 已验证加载路线。每次绑定只请求一次 `LoadAsync`，后续轮询句柄，等待结束再构造可见对象。boxed 值类型句柄由宿主 pinned GCHandle 保存；实例方法操作其 unboxed 数据。不存在臆造的 `FAssetProxyHandle.Release`。

新对象在安装后也保持根 inactive，`Ready` 仅表示资源与对象准备完成。主模块取得握持状态并成功隐藏原装备后，调用 `SetVisible(true)`；`Visible` 才表示实际展示。复制 source GameObject layer、原生 sharedMesh/sharedMaterials、自有 palette/rootBone、SMR localBounds。updateWhenOffscreen 打开，减少独立手挂点运动造成的剔除。渲染层掩码在 SDK 能解析时复制。

清理顺序为停用/Destroy 自建视觉节点 → Dispose 资源依赖句柄 → free 本 helper 持有的 GCHandles。半成品创建失败也收集已创建而未完成父子关系的节点；取消未完成请求、缺失 Dispose 方法、资源错误或超时均不会触发真实技能。`Reset` 同时撤销 Availability，换到无独立刀配置的角色不会沿用旧 profile 重载。显隐 API 失败也销毁实例并释放句柄。原装备的 renderer 隐藏/恢复由主模块统一管理，隐藏成功后才启用独立刀，避免同时出现两把武器。

helper 不包含网络、实体生成、技能、伤害或 HP 写入调用。真实游戏正常通信不在此 helper 范围。

## 离线验证

专用测试已用当前 EML SDK / MSVC（含 Release / DNDEBUG）编译执行，覆盖：资源索引未就绪不加载；异步请求不重复；只添加一个 SkinnedMeshRenderer；骨数组和 rootBone 使用新建自身骨；原 prefab/mesh/material 不销毁或修改；稳定重复 Tick 不复制对象；Reset 幂等且无 visual profile 不重载旧刀；取消未完成请求；克隆中途 setter 失败回滚所有对象/GC 引用；15 秒加载超时；Dispose 未解析时不请求；安装姿态与资源不符时不创建渲染器；资源就绪仍保持 inactive；显隐失败释放全部视觉对象。

另有独立数值蒙皮回归：用原生顶点 `(0.08199812, -0.007199967, 1.6476015)`、精确 TypeTree bindpose、原 renderer `(2.717, -0.132, 0)` 偏移和世界手挂点平移/90° 旋转，比较 cloneOwnBoneWorld × bindpose × vertex 与 socketWorld × sourceOwnBoneWorld × bindpose × vertex，误差小于 1e-6 m。测试同时要求错误的 rendererWorld 算法相差大于 1 m，因此能识别把 renderer 原点当作蒙皮坐标或重复安装矩阵的错误。

测试是宿主模拟，不代表已在终末地验证。实机下一步应确认静止持刀、普通斩/纳刀/登龙的握柄、背面与侧面的刀刃平面、原装备显隐恢复、失焦/换人清理和材质是否需要额外纯渲染参数；完整火焰单独接入视觉时间线。

复现资料见 `research/laevatain/README.md`。
