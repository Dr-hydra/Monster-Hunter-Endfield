# 项目名称与兼容性记录

2026-10-13，早期太刀展示工程扩展为 Monster Hunter Endfield。模块继续使用 `endfield.longsword.showcase`，保留 EML format 1 / ABI 1，避免名称调整破坏已有安装标识。

早期单模式库存和构建缺口记录见 [历史基线审计](BASELINE_AUDIT.md)，后续实现见 [0.6.0 版本记录](implementation/0.6.0.md)。历史研究素材与机器库存保存在本地；公开文档使用仓库相对路径。

在新目录或新机器上构建时，重新运行 CMake configure，不沿用旧目录生成的工程。资源工具仍有部分本地路径与缓存约定，需要按 [素材重建](RESOURCE_REBUILD.md) 核对。源码、模块身份和游戏派生资源分别管理；克隆源码不表示取得完整运行素材。
