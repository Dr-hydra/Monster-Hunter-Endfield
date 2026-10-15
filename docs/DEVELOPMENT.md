# 开发与构建

在仓库根目录运行以下命令。主模块使用 Windows x64、C++20、CMake 3.25+、Python 3 和 Visual Studio 2022 的 C++ 工具链；网页是静态 HTML/CSS/JavaScript，无需 npm 构建。EML SDK 与 JSON 单头位于 `third_party/include`，来源见 [第三方说明](../THIRD_PARTY_NOTICES.md)。

## 原生模块

```powershell
cmake -S . -B build/development -G 'Visual Studio 17 2022' -A x64
cmake --build build/development --config Release --target longsword
```

DLL 输出到 `build/development/package/native/windows-x64/endfield.longsword.showcase.dll`。需要其他 SDK 头目录时，configure 可传 `-DSDK_INCLUDE=<SDK头目录>`。编译 DLL 不会生成游戏动作、模型、音频或完整运行包。

## 测试

```powershell
cmake --build build/development --config Release
ctest --test-dir build/development -C Release --output-on-failure
```

CTest 会先为 `packed_tracks` 生成当前构建目录的运行资源银行，不依赖另一个构建目录的旧缓存。DLL 内嵌模块 ID 和 manifest 版本，打包时拒绝旧 DLL 或混用模块。

完整回归会读取本地 `assets/`、已生成 profile 和测试素材；新克隆未准备这些输入时，资源相关测试无法通过。目标选择、碰撞、规则和几何等独立测试可在构建后单独运行：

```powershell
ctest --test-dir build/development -C Release -R '^(targeting|combat_collision|combat_rules|target_geometry|pose_lease)$' --output-on-failure
```

源码自带的资源研究脚本仍依赖 ignored 本地工具、游戏文件、对象图及部分既有缓存；部分脚本保留工作区路径约定。它们没有统一的自动依赖安装和从零重建入口，不能承诺克隆后即可完成全量素材重建。运行前按 [素材重建](RESOURCE_REBUILD.md) 核对各脚本输入和工具来源。

## 完整包

准备好完整本地素材并通过相应检查后：

```powershell
python tests/test_package_validation.py
node tools/ui_verify_rules.cjs
python tools/build_product_catalog.py
python tools/package.py --package-root build/development/package --validate-only
python tools/package.py --package-root build/development/package
```

`tools/package.py` 从 `module.json` 读取版本，默认输出 `dist/endfield.longsword.showcase-1.0.0-compat-1.6.3-rc1-win-x64.zip`。`--package-root` 指向该记录 CMake 输出目录；`--output` 可另选 ZIP 路径。`--validate-only` 检查完整包依赖，不写 ZIP。

当前完整素材覆盖六模式、1185 动作槽、34 角色、84 普通装备和两把特殊模型、196 PCM 及全部网页依赖。打包器检查模式／角色／模型覆盖、完整资源引用、轨道、派生、来源和 EML 文件限额；缺素材时会拒绝生成完整包。大型资源、构建目录和 ZIP 均留在本地。

浏览器检查与真实 EML 隔离验证分别见 [素材重建](RESOURCE_REBUILD.md) 和 [宿主验证](implementation/eml-verification.md)。这些离线检查不代替游戏内验收，默认不计算产物哈希。
