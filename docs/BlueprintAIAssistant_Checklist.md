## Blueprint AI Assistant 落地 Checklist

根据 `ue蓝图ai助手落地` 计划整理；**用 Markdown 勾选跟踪进度**（在编辑器里把 `- [ ]` 改成 `- [x]` 即可）。

> **进度说明（2026-04）**  
> - 路线图 **P0（Phase 3.E 核心 + float→PrintString）**：已交付，**用例 25–27 冒烟通过**。  
> - **Phase 4 试用包（面板 + 日志 + 反馈 + 手册 + 撤销）**：代码与文档已落地，**待地编小范围试用验收**（见未勾选的验收项）。

---

### Phase 1：提示 / 问答 MVP

- [x] **插件骨架**
  - [x] `BlueprintAIAssistant` 插件已创建，并在 `.uproject` 中启用
  - [x] `BlueprintAIAssistantRuntime` / `BlueprintAIAssistantEditor` 模块可正常编译加载
- [x] **编辑器面板（Dock Tab）**
  - [x] 在蓝图编辑器中可从菜单/窗口打开 `Blueprint AI Assistant` 面板
  - [x] 面板内支持输入问题、点击 **生成建议**
  - [x] 顶部显示当前 Provider / Model / Endpoint 概要信息
- [x] **云端 API 接入**
  - [x] 至少 1 个 Provider（如 DeepSeek）接入成功，可稳定返回回答
  - [x] 支持 OpenAI 兼容模式（可接入本地 Node 转发网关）
  - [x] 支持多个国内模型预设（如 Doubao、Gemini），并在设置中可单独配置
- [x] **配置管理**
  - [x] Project Settings → Plugins → `Blueprint AI Assistant` 中可配置：Provider 类型、Endpoint / Model / API Key / Timeout
  - [x] API Key 不写入仓库文件（仅存本地用户配置）
- [x] **上下文采集（最小集）**
  - [x] 能从当前正在编辑的 Blueprint 收集：名称、类型、变量列表、函数图列表
  - [x] 在蓝图未打开时，有清晰提示（先打开 Blueprint 再提问）
- [x] **基础交互增强**
  - [x] 支持「复制建议」按钮
  - [x] 支持「一键插入注释到 EventGraph」
- [x] **错误处理与日志**
  - [x] 网络错误时有清晰错误文案（含 Status/Endpoint 等）
  - [x] 请求体在日志中可打印（敏感信息已脱敏）
  - [x] 所有自动图修改均支持 Undo/Redo（蓝图事务）

---

### Phase 2：引导式生成（步骤清单）

- [x] **系统 Prompt 设计（步骤 JSON）**
  - [x] 单独 System Prompt，要求严格 JSON：`{\"steps\":[\"步骤1...\",\"步骤2...\"]}`
  - [x] 明确要求不输出 Markdown / 代码块 / 额外前后缀
- [x] **面板 UI**
  - [x] **生成步骤清单** 按钮；与「生成建议」复用同一输入框
- [x] **JSON 解析与展示**
  - [x] 解析 `steps` 数组；失败时有错误提示与原始输出
- [x] **DSL 与半自动执行（Phase 2.5）**
  - [x] DSL：`CreateNode` / `ConnectPins` / `SetPinDefault` / `Comment`（`BlueprintDslTypes`）
  - [x] DSL System Prompt（`BuildSystemPromptDslJson`）
  - [x] 面板：**生成可执行步骤（DSL）**、步骤预览、批量执行、单步执行
  - [x] 执行器：`BlueprintDslExecutor`（事务、失败停步并提示步号）

---

### Phase 3：自动生成节点

#### Phase 3.A：执行器能力扩展

- [x] **流程控制 / 常用工具**：`Sequence` / `DoOnce` / `Gate` / `Delay` / `GetVariable` / `SetVariable` / `SpawnActorFromClass`（类白名单）等
- [x] **Trace 族**：`LineTraceSingle` / `SphereTraceSingle` + 默认 TraceChannel 等
- [x] **动画**：`PlayAnimMontage` + Montage 默认值
- [x] **图范围**：`EventGraph` / `Function:<Name>`（`targetGraph`）

#### Phase 3.B：节点白名单策略（方案 3）

- [x] **CallFunction 全量开放** + 常用别名映射
- [x] **高风险黑名单**（强制 `requiresConfirmation=true`）
- [x] **Spawn 类白名单**（可配置）
- [x] **ValidateSteps** 增强 + 面板高风险提示

#### Phase 3.C：冒烟与修复

- [x] Trace / Spawn / PlayAnimMontage / targetGraph 冒烟通过
- [x] 典型问题修复：`PostPlacedNewNode`、Exec/Pin 别名、AssetRegistry 等

#### Phase 3.D：常用节点补齐（第一批）

- [x] `Cast` / `IsValid` / `MakeVector` / `BreakVector` / `MakeRotator` / `BreakRotator`
- [x] **Spawn 白名单四层**（Settings：`UBlueprintAIAssistantSettings`）
- [x] Prompt / DSL `targetClass` 与 pin 速查
- [x] **冒烟**：`TestScenarios.md` 第八节 **用例 19–24** 已测

#### Phase 3.E：常用节点第二批（路线图 P0）

> 与 `BlueprintAIAssistant_Roadmap.md` §3.6 一致；**下列「核心 P0」已交付**（用例 25–27）。

- [x] **P0：`ForEachLoop` / `ForEachLoopWithBreak`**（StandardMacros；`MakeArrayInt` 类型锁定等）
- [x] **P0：`SwitchOnInt` / `SwitchOnString`**（`cases` 扩展分支）
- [x] **P0：`Select`**（`cases`、Option 引脚映射、`SetPinDefault` 前 String/Int 定型、语义 pin 名容错）
- [x] **P0：`MakeArray` / `MakeArrayInt`**
- [x] **P0：`GetVariable` / `SetVariable` 解析**：`CreateNode` 同条 JSON 解析 `varName` / `valueFrom` / `defaultValue`；别名 `variableName` / `name`
- [x] **P0：Float → `PrintString`**：`ConnectPins` 失败时对 `InString` 自动插 `Conv_*ToString` 兜底（int/float/bool/name）
- [x] **InFloat ↔ InDouble**：`FindPinLoose` 互认 + 别名表双向映射
- [x] **HitResult → Cast.Object**：`TryConnectHitResultToCastObject`（BreakStruct / BreakHitResult，含 OutHit wildcard）
- [x] **冒烟**：`TestScenarios.md` 第九节 **用例 25–27** 已通过

**仍排队（非 P0，按试用日志优先级排期）**

- [x] `Get Component By Class`（DSL `GetComponentByClass` + CallFunction 别名）
- [x] `Set Timer by Function Name`（`SetTimerByFunctionName` + CallFunction 别名；`defaultValue`=秒数，`cases[0]`=是否循环）
- [ ] `Add Actor Component`（低优先级）
- [ ] 更广义的 `ToString` / 类型转换自动插入（Text 等非常用类型；PrintString 外目标 pin）

**Phase 5 能力（不在 3.E 内，见路线图）**

- [x] `ConnectPins` 前 `CanCreateConnection` 预检 + 两端 pin 类型写入错误信息
- [x] 失败聚合执行（Settings：`bAggregateDslExecutionFailures`）
- [x] JSON Schema 校验（轻量：解析必填字段/动作类型校验）+ **Schema 清洗/纠错**（Settings：`bEnableDslSchemaAutoSanitize`）
- [x] 解析失败自动重问 1 次（Settings：`bAutoRetryOnceOnDslParseFailure`）
- [x] 执行失败自动重问 1 次（批量/单步；Settings：`bAutoRetryOnceOnDslExecFailure`）
- [x] 高风险二次确认弹窗（`requiresConfirmation=true`）
- [x] Pin 别名候选输出（v1）：面板按钮「生成 Pin 别名候选」扫描 usage 日志输出 `pin-alias-suggestions-*.md`（不自动写入）
- [x] Pin 别名 v2（候选导入→勾选写入→热更新）：面板按钮「导入候选 / 写入并启用别名表 / 重新加载别名表」写入 `Saved/BlueprintAIAssistant/pin-aliases.json` 并 `ReloadPinAliasTable()`
- [x] 排版 v3：长线自动插 Knot、数据节点卫星贴靠、Comment 跟随、分簇原生 Straighten（Settings：`bLayoutInsertKnotsForLongWires` / `bLayoutAttachDataNodesAsSatellites` / `bLayoutAutoResizeCommentBoxes`）
- [x] HTTP 失败可定位闭环：提取失败日志增强 + 面板按钮「导出 HTTP 响应（脱敏）」输出到 `Saved/BlueprintAIAssistant/http-dumps/`
- [x] usage 埋点增强：重试统计（parse/exec）+ 失败归因字段（`failure_category` 等）
- [x] `AnyDamage` 事件节点支持：识别 `EventAnyDamage / Event AnyDamage / ReceiveAnyDamage`，缺失时自动创建
- [x] `OnComponentBeginOverlap` 回退策略：组件事件不可绑定时自动退化到 `OnActorBeginOverlap`
- [x] 数学别名补齐（Int）：`Int + Int / - / * / /` 映射可执行函数
- [x] 跨对象变量上下文（P1 最小版）：`Cast` 输出对象连接 `Get/SetVariable.self` 时自动重定向变量节点到目标类
- [x] `InputAction` / `Input Key` 误写为 CallFunction 时自动创建 `InputKey` 事件（按键开门等）
- [x] `GetComponentByClass` 节点与 CallFunction 别名（拾取/组件链）
- [x] `SetTimerByFunctionName` 节点与 CallFunction 别名（延时回调；`defaultValue`=Time，`cases[0]`=bLooping）
- [x] 可选函数软失败：可选节点不存在时自动跳过，不中断整批执行
- [x] 可选节点 Exec 自动桥接：跳过可选节点后，自动重连上游 `then` 到下游 `execute`
- [x] 失败引导卡片同步“可选步骤自动处理”信息（友好文案）
- [x] 手动前置条件显式提醒：步骤区醒目提示 + 执行前确认弹窗

---

### Phase 4：地编试用包（代码已落地 → **待试用验收**）

详见 `BlueprintAIAssistant_Roadmap.md` §3.1。

- [x] **场景快捷按钮**：`BlueprintAISceneTemplates` + 面板按钮区；点击填入 prompt 并可生成 DSL
- [x] **使用日志**：`FBlueprintAIUsageLogger` → `Saved/BlueprintAIAssistant/usage-YYYYMMDD.log`（JSON Lines）
- [x] **打开日志目录**：面板按钮 `FPlatformProcess::ExploreFolder`
- [x] **一键反馈**：打包 prompt + DSL + 错误 → `Saved/.../feedback/feedback-*.md`
- [x] **撤销本次 DSL 改动**：连续 `GEditor->UndoTransaction`（与 Ctrl+Z 一致）
- [x] **地编手册**：`docs/BlueprintAIAssistant_LDGuide.md`

**试用闭环（需人走一遍再勾）**

- [x] 至少 **2～3 名地编**按 `LDGuide` 独立完成 **5 个场景按钮**各 1 次，无阻塞性 UX 问题
- [ ] **一周内**收集 `usage` 日志 + 任意 `feedback-*.md`，并做一次 **15 分钟复盘**（路线图 §3.3 KPI）

---

### Phase 5～8：后续演进

详见 `docs/BlueprintAIAssistant_Roadmap.md`（连线预检、失败聚合、场景向导、多轮对话、上下文/Diff/快照、CI 等）。以下为总览勾选，细节以路线图为准。

- [x] **Phase 5（阶段内关键能力已落地）**：连线预检、可选失败聚合、解析/执行自动重问（最多 1 次）、高风险弹窗、Schema 清洗/纠错（详细仍以路线图条目为准）
- [ ] **Phase 6（按建议顺序执行）**
  - [x] **Phase 6.A：多轮对话 MVP（首版已落地）**：面板「清空多轮历史」+ 向 `UserPrompt` 注入最近 **N 轮**（默认 6）与「上一版 DSL 摘要 / 最近失败摘要」；项目设置 `对话 | Phase 6.A` 可关/调参
- [x] **Phase 6.A.1：分诊/澄清/计划（首版已落地）**：点「生成可执行步骤（DSL）」时，会对模糊/跨蓝图需求先走 **澄清问题** 或 **跨蓝图实施计划**（可逐步点“生成该步 DSL”落到单蓝图执行）
  - [x] **Phase 6.B：基于现有 DSL 的增量修改**（让模型在“已有 steps”上做 diff/patch，而不是每次全量重写）
    - [x] 6.B-1：失败引导卡片入口与结构化失败上下文打通（不再只靠 `ResponseText` 猜测）
    - [x] 6.B-2：`OnRequestDslPatchFixClicked` 优先使用结构化失败构造 patch prompt（最小变更原则）
    - [x] 6.B-3：`ApplyDslPatchOps` 防护增强（target 缺失/越界/stepId 冲突/可读错误）
    - [x] 6.B-4：连续 patch 回归（两轮增量修复不漂移；成功响应展示变更摘要，并记录 `dsl_patch_result` 埋点）
  - [x] **Phase 6.D：埋点 / KPI（首版已落地）**：高级工具新增「生成 KPI 报告」，扫描 `usage-*.log` 汇总请求/解析/执行/重试/失败归因/patch/超时建议
  - [ ] **Phase 6.C：持久化（可选）**（看地编是否强需求：对话/草稿/执行历史跨会话保存）
- [ ] **Phase 7**：选中节点上下文、蓝图 RAG、Diff 预览、快照回滚
- [ ] **Phase 8**：团队配置、版本与 CHANGELOG
  - [x] **Phase 8.A：Headless CI 冒烟（BlueprintAIAssistant）**：`scripts/run-blueprint-ai-smoke.ps1` + `BlueprintAIAssistant.Smoke.*` + `Plugins/BlueprintAIAssistant/Tests/SmokeDsl/*.json`（**14** 个 fixture，含 12 受伤掉血 / 13 float→ToString / 14 按键开门）+ `.github/workflows/blueprint-ai-smoke.yml`（self-hosted runner）

---

### 文档与自测

- [x] `docs/README.md`：项目/插件说明
- [x] `docs/BlueprintAIAssistant_TestScenarios.md`：自测用例（含 25–27、28–32、33–37 等）
- [x] `docs/BlueprintAIAssistant_Checklist.md`：本文件（可勾选进度）
- [x] `docs/BlueprintAIAssistant_Roadmap.md`：路线图与试用计划
- [x] `docs/BlueprintAIAssistant_LDGuide.md`：地编使用手册

---

### 回归时怎么用本清单

1. 改功能后：在对应 Phase 下核对子项，**保持 `[x]` 与主分支可发布状态一致**。  
2. 新增能力：在 **Phase 3.E 排队** 或 **Phase 5** 下**新增一行 `- [ ]`**，合入后再改为 `[x]`。  
3. 发版前：至少跑一遍 `TestScenarios.md` 中与本次改动相关的用例。
