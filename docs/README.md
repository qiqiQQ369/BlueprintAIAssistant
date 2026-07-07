# docs 索引

本目录集中存放项目内各 AI 相关插件的文档、路线图与自测用例。

### 通用

- `CalystoWorld_启动指南.md`：`Content/Calysto` 的启动准备、首次运行步骤与常见问题排查

### UE AI Tool Kit（`Plugins/UEAIToolKit`，tool 底座）

- `UEAIToolKit_Architecture.md`：架构说明（模块图 / 核心抽象 / 调用链 / 扩展点）
- `UEAIToolKit_Roadmap.md`：分期路线图（Phase 0 基座 → Phase 1 Level → Phase 2 Blueprint → … → Phase 6 Copilot → Phase 7 工程化）
- `UEAIToolKit_Checklist.md`：分 Phase 勾选单（回归跟踪）
- `UEAIToolKit_TestScenarios.md`：自测用例清单（每个 tool 的烟测矩阵）
- `../scripts/python_smoke_aitoolkit.py`：一键烟测脚本（Tools → Execute Python Script 运行）

### Blueprint AI Assistant（`Plugins/BlueprintAIAssistant`，蓝图场景）

- `BlueprintAIAssistant_Roadmap.md`：路线图与阶段规划
- `BlueprintAIAssistant_Checklist.md`：落地 Checklist（勾选跟踪进度）
- `BlueprintAIAssistant_TestScenarios.md`：自测用例清单（冒烟/回归）
- `BlueprintAIAssistant_LDGuide.md`：地编使用手册
- `BlueprintAIAssistant_PrebuiltPackaging.md`：预编译包（prebuilt_root）打包规则

> 两个插件的关系：UEAIToolKit 是中立 tool 底座，BlueprintAIAssistant 将在 Phase 2 迁入其 Blueprint 工具包（见 `UEAIToolKit_Roadmap.md` §3）。