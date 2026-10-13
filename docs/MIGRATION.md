# 迁移记录

日期：2026-10-13。目标开发根目录为 G:/Monster Hunter Endfield。

## 已完成

1. 将 G:/Endfield Longsword Showcase 同盘整体重命名为新目录。迁移前后647个文件、1,191,731,046字节的相对路径/大小清单一致，差异0。没有删除文件或产生一份约1.2GB的项目副本；旧目录已不存在。
2. 将 G:/Better Endfield/research/actions/mhw-longsword/20261012 的关联资料复制到 research/mhw-longsword/20261012。排除 .git 和 __pycache__，共518文件、44,065,901字节；原 Better Endfield 资料保留。复制文件与原文件直接按字节核对，不计算产物哈希。
3. tools/bake_actions.py 的研究目录改为项目相对路径，动作求值和烘焙逻辑没有改动。
4. 旧根 README 原文保存在 LEGACY_LONGSWORD_README.md；新 README、计划、基线审计和架构决策说明当前项目状态。
5. 已初始化独立 main 分支仓库并创建私有远端 https://github.com/Dr-hydra/Monster-Hunter-Endfield。首个提交包含源码、计划、审计和研究报告；素材和大输出仅留本地，推送结果另行核对。

## 兼容与限制

- 模块 ID 仍为 endfield.longsword.showcase，清单版本保留0.5.6-dev；未安装新版包或迁移用户EML设置。
- 部分头文件、测试、SDK、profile及运行资源在本轮迁移前就缺失。详见 BASELINE_AUDIT.md；同盘迁移清单并不意味着这些旧缺失文件已恢复。
- build 中旧路径属于生成配置，后续补齐源码后在新目录重新配置；本轮未修改生成文件或尝试通过旧构建缓存出包。
- 历史研究中的旧绝对路径作为来源记录保留，新运行/烘焙脚本使用新项目根。研究脚本如需终末地旧缓存，仍明确引用 G:/Better Endfield。
- 本轮不改动作、武器校正、音效或练习逻辑，没有启动游戏/服务器，也没有生成新发行包。

完整迁移清单保存在本地 migration-local-inventory.json，未纳入Git。
