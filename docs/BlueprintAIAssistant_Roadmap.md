## Blueprint AI Assistant 路线图

面向「让地编 / 初级开发者以最小学习成本，在蓝图里快速搭出常见玩法」的长期规划。

当前节点：Phase 1~5 核心能力已落地（含排版 v1/v2/v3、可靠性闭环），Phase 6.A / 6.A.1（多轮 + 分诊/澄清/计划）已首版交付，Phase 6.B（基于失败上下文的 DSL 增量 patch）已完成首版，Phase 6.D（埋点/KPI）已完成首版：高级工具可一键生成 usage KPI 报告，汇总请求、解析、执行、重试、失败归因、patch 与超时建议。下一步按 **6.C（持久化，可选）** 或继续扩展 KPI 可视化推进。本文件用于记录试用期目标、反馈闭环，以及后续 Phase 的交付节奏。

---

### 0. 背景 & 目标用户画像

#### 目标用户


| 角色            | 典型诉求                                        | 关键痛点                                   |
| ------------- | ------------------------------------------- | -------------------------------------- |
| **地编 / 关卡设计** | 给关卡里某个物件加简单逻辑：开门、触发器、Spawn 特效、播放音效/动画、UI 提示 | 不熟悉蓝图 API 名、不确定哪根 pin 接哪根 pin、改动后易破坏编译 |
| **初级蓝图开发者**   | 给角色/控制器加新功能（血量、加速跑、计时器）                     | 记不住节点名称、难以从零搭起最小可运行链路                  |
| **资深开发者**     | 把重复性蓝图脚手架 10 秒搭完                            | 现有模板/收藏节点操作繁琐                          |


#### 核心假设（试用期要验证）

1. **模型能理解自然语言的玩法需求** → 能输出 60%+ 可直接执行的 DSL。
2. **执行失败也不会损坏已有蓝图** → Undo/Redo + 单步执行 + 事务 已经做到。
3. **地编在遇到失败时能看懂错误信息 / 自行取消并重试** → 需要体验打磨。
4. **相比手工搭建，能平均节省 30%+ 时间** → 需要对比数据。

---

### 1. 现状盘点（Phase 0 基线）

已具备能力（参考 `BlueprintAIAssistant_Checklist.md`）：

- **Q&A 问答**：中文建议、步骤清单（JSON）、Markdown 剥离。
- **Provider 兼容**：DeepSeek / Doubao / Gemini / OpenAI 兼容。
- **DSL 半自动执行**：`CreateNode` / `ConnectPins` / `SetPinDefault` / `Comment` / `GetVariable` / `SetVariable`。
- **节点覆盖**：Branch / Sequence / DoOnce / Gate / Delay / InputKey / CallFunction（全量开放）/ SpawnActorFromClass（类白名单）/ Trace / PlayAnimMontage / Cast / ForEach / Switch / Select 等。
- **目标图范围**：EventGraph + `Function:<Name>`。
- **安全边界**：高风险函数黑名单、`requiresConfirmation`、Spawn 类白名单、事务包裹全部改动。
- **试用闭环**：使用日志、一键反馈、场景快捷按钮、KPI 报告、headless smoke（`scripts/run-blueprint-ai-smoke.ps1` + GitHub Actions）。
- **可靠性**：连线预检、失败聚合、解析/执行自动重试、跨对象变量、可选节点桥接、DSL patch、排版 v1–v3。

仍在推进：

- 试用 KPI 复盘与批量执行率持续拉升（目标 ≥ 60% → 80%）
- Phase 3.E 排队：`Get Component By Class`、`Set Timer` 等（按 usage 优先级）
- Phase 6 场景向导（模板按钮已有，向导 UI 未做）
- Phase 7：选中节点上下文、Diff 预览、快照回滚

---

### 2. 路线图总览

```
Phase 0 (当前) ─► Phase 4 试用包 ─► Phase 5 降失败率 ─► Phase 6 体验升级 ─► Phase 7 进阶能力
                    │                     │                   │                       │
                    └── 1-2 周内发布      └── 试用反馈驱动    └── 迭代稳定后推广      └── 团队级/工程化
```


| Phase       | 目标             | 预期周期  | 主要受益对象    |
| ----------- | -------------- | ----- | --------- |
| **Phase 4** | 交付地编试用包 & 数据埋点 | 1-2 周 | 试用地编、产品决策 |
| **Phase 5** | 基于试用数据降失败率     | 2-3 周 | 所有用户      |
| **Phase 6** | 交互/体验升级（模板、多轮） | 2-4 周 | 地编、初级开发   |
| **Phase 7** | 上下文感知 & 进阶能力   | 4-6 周 | 所有开发者     |
| **Phase 8** | 工程化 & 团队推广     | 视情况   | 团队级       |


---

### 3. Phase 4 — 地编试用包（MVP for LD）

> **目的**：把当前能力打包成地编能上手的小范围试用版，并建立反馈/数据闭环。
> **交付期望**：1~~2 周，能给 2~~3 位地编试用至少 1 周。

#### 3.1 交付物


| 交付物          | 说明                                                                                    | 验收                        |
| ------------ | ------------------------------------------------------------------------------------- | ------------------------- |
| **地编使用手册**   | `docs/BlueprintAIAssistant_LDGuide.md`：纯"我想做 XX → 点哪个按钮 → 看到什么"，配截图                   | 地编不看代码/Prompt 能独立走通 5 个用例 |
| **预设模板提示入口** | 面板加一排「场景快捷按钮」：按键交互开门 / 拾取物 / 延时 Spawn / 角色受伤掉血 / UI 提示                                | 点按钮即填入 prompt，5 秒内生成 DSL  |
| **使用日志**     | 本地 `Saved/BlueprintAIAssistant/usage.log`：时间戳、provider、prompt、DSL 成功/失败、用时、失败步骤       | 试用期内可统计出请求次数、成功率、常见失败     |
| **一键反馈按钮**   | 面板新增「反馈当前问题」：把最近一次 prompt + DSL + 错误 + 可选截图路径打包成 `feedback-YYYYMMDD-HHMMSS.md` 放到指定目录 | 地编遇错 30 秒内可提交一份反馈         |
| **快速回滚入口**   | 保留现有 Ctrl+Z；额外在面板加「撤销本次所有改动」按钮（调用事务 Undo N 次）                                         | 批量执行失败后一键回到干净状态           |


#### 3.2 任务拆解

**功能性**

- 面板 UI：新增场景快捷按钮区（SWrapBox 横向按钮组），点击后填入预设 prompt 并自动生成 DSL
- Prompt 模板库：`BlueprintAISceneTemplates.h/.cpp`，内置 7 条常见场景（后续可迁移到 `.ini`）
- 使用日志：`FBlueprintAIUsageLogger` 单例，JSON Lines 写到 `Saved/BlueprintAIAssistant/usage-YYYYMMDD.log`；埋点覆盖 Ask / GuidedSteps / DSL / Ping / Validate / ExecBatch / ExecStep / SceneTemplate
- 打开日志目录按钮：面板底部「打开日志目录」，`FPlatformProcess::ExploreFolder` 一键弹出
- 反馈按钮：最近一次 prompt + DSL 预览 + 面板输出打包为 `<Saved>/BlueprintAIAssistant/feedback/feedback-*.md`，自动弹出目录
- "撤销本次 DSL 改动"按钮：累计事务计数（批量 +1、单步 +1），点击时连续 `GEditor->UndoTransaction`

**文档**

- `BlueprintAIAssistant_LDGuide.md`：使用手册（文字版；后续有需要再补截图）
- 更新 `BlueprintAIAssistant_TestScenarios.md`：新增 LD 试用专属用例（5~8 条，覆盖场景快捷按钮）

**运维**

- 内部反馈群/邮件组：告知地编每天下班前把 `feedback-`* 文件发到群里
- 每周快速复盘会（15 分钟）：过使用日志 + 反馈，决定下周优先改哪几条

#### 3.3 试用期指标（KPI）


| 指标                      | 目标     | 观察方式 |
| ----------------------- | ------ | ---- |
| DSL 生成成功率（JSON 可解析）     | ≥ 90%  | 使用日志 |
| DSL 执行成功率（全部 step 跑完）   | ≥ 60%  | 使用日志 |
| 单个 prompt 平均耗时（从提问到执行完） | < 45 秒 | 使用日志 |
| 地编主观评分（1-5）             | ≥ 3.5  | 周末问卷 |
| 每位地编一周内尝试次数             | ≥ 20 次 | 使用日志 |


#### 3.4 风险 & 应对

- **风险 A：模型不稳定/额度耗尽** → 预先准备 2 个 provider（DeepSeek + Gemini）互备，面板可切换。
- **风险 B：生成的 DSL 破坏已有蓝图** → 已有事务包裹 + 单步执行；在手册里强调「先单步后批量」。
- **风险 C：地编反馈门槛高** → 提供一键反馈按钮，避免写文字说明。

---

### 3.5 Phase 3.D — 常用节点第一批（已完成，可与 Phase 4 试用并行迭代）

> 把"会被 LD 频繁要求"的节点先补齐，避免地编一开口就超出执行器能力。


| 任务                             | 状态  | 说明                                                                       |
| ------------------------------ | --- | ------------------------------------------------------------------------ |
| `Cast To <Class>`              | 已完成 | `UK2Node_DynamicCast` + `targetClass` 字段（短名/完整路径）                        |
| `IsValid`（函数版）                 | 已完成 | `UKismetSystemLibrary::IsValid`，Pure bool，配合 Branch 使用                   |
| `MakeVector` / `BreakVector`   | 已完成 | `UKismetMathLibrary::MakeVector` / `BreakVector`（避免泛型 Break Struct 编译警告） |
| `MakeRotator` / `BreakRotator` | 已完成 | `UKismetMathLibrary::MakeRotator` / `BreakRotator`                       |
| **Spawn 白名单四层配置**              | 已完成 | `UBlueprintAIAssistantSettings` L1 类名 / L2 路径前缀 / L3 基类 / L4 dev 全放行     |
| Prompt 同步更新                    | 已完成 | 新增 4 类 nodeType 的用法说明与 pin 速查                                            |
| 冒烟用例                           | 已完成 | `TestScenarios.md` 第八节：用例 **19–24**（含 Cast/向量旋转/Spawn L2 等，**已实测通过**）    |


### 3.6 Phase 3.E — 常用节点第二批（**已完成，冒烟用例 25–27 已通过**）


| 交付项                                    | 状态  | 说明                                                                                        |
| -------------------------------------- | --- | ----------------------------------------------------------------------------------------- |
| `ForEachLoop` / `ForEachLoopWithBreak` | 已完成 | StandardMacros 宏节点；`MakeArrayInt` + 类型锁定，避免 wildcard 默认值不显示                               |
| `SwitchOnInt` / `SwitchOnString`       | 已完成 | `cases` 扩展分支；StartIndex 约定                                                                |
| `Select`                               | 已完成 | `cases` 扩展 option；`SetPinDefault` 前自动定型 String+Int；`pinName` 与 `cases` 标签映射（`BPAI_CASES`） |
| `MakeArray` / `MakeArrayInt`           | 已完成 | 字面量数组 + `SetPinDefault`                                                                   |
| `GetVariable` / `SetVariable`          | 已完成 | `CreateNode` 解析 `varName` / `valueFrom` / `defaultValue`；别名 `variableName`/`name`         |
| Float → String                         | 已完成 | `ConnectPins` 目标为 `PrintString.InString` 时自动插 `Conv_Int/Float/Bool/NameToString` 兜底 |
| InFloat ↔ InDouble                     | 已完成 | `FindPinLoose` 互认 A/B；别名表双向映射                                                     |
| HitResult → Cast.Object                | 已完成 | `TryConnectHitResultToCastObject`（BreakStruct 优先 + BreakHitResult 函数兜底）               |
| Prompt + `TestScenarios` 第九节           | 已完成 | 用例 **25–27**（ForEach / SwitchOnInt / Select）                                              |


**仍排队（按试用日志上调优先级）**：`Add Actor Component`、Text 等非常用类型的 `ToString`、LineTrace OutHit by-ref 在 headless 冒烟中的编译级回归（运行时自动 BreakHit 已增强）。`Get Component By Class`、`Set Timer by Function Name` 已落地。

---

### 3.7 下一步规划（Phase 3.E 已闭环，**用例 25–27 冒烟通过**）

> **当前建议**：**优先启动 Phase 4（地编试用包）**，把已有能力交给地编真实使用并收集日志；技术债与 P1 项与试用并行、小步迭代。


| 阶段               | 优先级    | 方向                                       | 说明                                                            |
| ---------------- | ------ | ---------------------------------------- | ------------------------------------------------------------- |
| **Phase 4**      | **P0** | 地编试用包                                    | 场景快捷按钮、使用日志、一键反馈、撤销本次 DSL、LD Guide（见 §3.1）— **与路线图 W1 里程碑对齐** |
| **Phase 5 轻量前置** | **P1** | `ConnectPins` 前 `CanCreateConnection` 预检 | 失败信息带两端 pin 类型，减少黑盒；不阻塞 Phase 4 发版                            |
| **数据驱动**         | **P1** | 试用后扫 `usage` 日志                          | Top 失败 pin 并入 `ResolvePinNameAlias`；Phase 3.E 排队项按命中次数排序      |
| **Phase 3.E 扩展** | **P2** | `Set Timer` / `Get Component by Class` 等 | 在 ForEach/Switch/Select 已稳的前提下再扩                              |
| **Phase 6 预览**   | **P2** | 多轮对话带上一段 DSL                             | 迭代式改图                                                         |


**时间建议**：接下来 **1～2 周以 Phase 4 交付为主**（试用包 + 埋点）；试用满约 **1 周**后，用日志驱动 **Phase 5**（预检、别名、可选失败聚合）与 3.E 扩展项排序。

---

### 4. Phase 5 — 基于试用数据降失败率

> **触发条件**：Phase 4 试用一周后，拿到至少 50 条使用日志和若干反馈。
> **目标**：DSL 执行成功率从 60% → 80%+。

#### 4.1 功能任务


| 任务                 | 说明                                                                                                                                      |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------------- |
| **连线静态预检**         | **已完成**：`ConnectPins` 阶段先 `CanCreateConnection` 预检并把「原因 + 两端 pin 类型」写入错误信息（后续仍允许 float→string 等兜底尝试）                                    |
| **轻量自动排版 v1**      | **已交付**：规则 BFS 左→右排布 + 分支下移 + SnapToGrid(16)；蓝图编辑器已打开时额外调用原生「Straighten Connections」（见 §4.3 子专题 v1）                                     |
| **失败聚合执行**         | **已完成**：设置 `bAggregateDslExecutionFailures` 后尝试跑完所有 steps，末尾汇总失败列表；默认仍为「遇错即停」                                                           |
| **自动扩容 Pin 别名表**   | 解析使用日志，把最常见的"显示名→真实 PinName"自动合并到 `ResolvePinNameAlias`                                                                                 |
| **模型输出 Schema 校验** | **已完成（轻量实现）**：`ParseDslStepsFromJson` 对必填字段/动作类型做校验；解析失败时可自动触发 1 次修复重试（见下一条）                                                            |
| **失败后"自动重问"**      | **已完成**：解析失败（生成阶段）支持 `bAutoRetryOnceOnDslParseFailure`；执行失败支持 `bAutoRetryOnceOnDslExecFailure`（**批量/单步**：先 Undo 再回灌失败摘要+原 steps，重试 1 次） |
| **高风险二次确认 UI**     | **已完成**：批量执行前若检测到 `requiresConfirmation=true`，弹窗 Yes/No 确认；点 No 则取消执行                                                                   |


#### 4.2 验收

- 复跑试用期 Top 10 失败用例：成功率 ≥ 80%
- 新回归套件（`TestScenarios` 新增 10 条）：一次性通过率 ≥ 90%

#### 4.2.1 近期反馈驱动加固（2026-04，已落地）

> 基于 `feedback-*.md` 高频失败样本，对“金币拾取/受伤逻辑”这类 LD 高频场景做了专项收敛。


| 项目                           | 状态  | 说明                                                                                                          |
| ---------------------------- | --- | ----------------------------------------------------------------------------------------------------------- |
| `AnyDamage` 事件复用/自动创建        | 已完成 | 识别 `EventAnyDamage / Event AnyDamage / ReceiveAnyDamage` 等写法，`ConnectPins` 找不到时自动创建 `ReceiveAnyDamage` 事件节点 |
| `OnComponentBeginOverlap` 回退 | 已完成 | 组件重叠事件不可绑定时，自动回退 `OnActorBeginOverlap`，避免主链中断                                                               |
| 数学别名扩展（Int 算术）               | 已完成 | `Int + Int / - / * / /` 映射到 `Add/Subtract/Multiply/Divide_IntInt`                                           |
| 跨对象变量读写（P1 最小版）              | 已完成 | `Cast` 输出对象连到 `GetVariable/SetVariable.self` 时，变量节点自动重定向到目标类上下文                                             |
| 可选函数软失败                      | 已完成 | 可选函数（如 `UpdateCoinUI`）不存在时步骤自动跳过，不计入硬失败                                                                     |
| 跳过可选节点后 Exec 自动重连            | 已完成 | 对可选函数节点的上/下游执行线做保守桥接（上游 `then` → 下游 `execute`）                                                              |
| 失败引导增强（可选处理可视化）              | 已完成 | 引导卡片新增“可选步骤已自动处理”展示，地编可见已跳过与自动补链结果                                                                          |
| 手动前置条件显式提醒                   | 已完成 | DSL 预览区前置条件卡片 + 执行前确认弹窗，避免“看见 comment 但执行前无感知”                                                              |


---

### 4.3 Phase 5.B — 蓝图排版与连线专题（子计划）

> **动机**：v1 的规则 BFS 只能让本次涉及节点「大致成行」，实际使用中仍会出现（1）节点叠加既有节点、（2）长连线穿节点、（3）数据节点漂在一边、（4）Comment 框与实际节点错位 等问题；目标是在不改变 DSL 语义的前提下，**把「排出来就能直接交付」的比例从 v1 的 ~50% 提升到 80%+**。
>
> **范围约束**（始终不变）：
>
> - 只移动节点 / 只插 `UK2Node_Knot` 中继节点；**不会新增、删除业务节点，不改 ConnectPins 拓扑**
> - 全程事务包裹；任何一步失败都静默回退到 v1 规则布局
> - 所有开关默认**开**、但可在 `UBlueprintAIAssistantSettings` 里逐项关闭

#### 4.3.1 分期交付


| 版本     | 阶段位置                  | 主要能力                                                                                                                     | 工作量估算 |
| ------ | --------------------- | ------------------------------------------------------------------------------------------------------------------------ | ----- |
| **v1** | Phase 5 前             | **已完成**：规则 BFS + 原生 Straighten（best-effort）                                                                              | —     |
| **v2** | Phase 5 内             | **已完成**：真实尺寸度量、同列对齐、**对既有节点做碰撞避让**；Branch/Sequence 按 pin Y 排序子节点；`bAutoLayoutAfterExecute` / `bLayoutAvoidCollisions` 开关 | 1–2 天 |
| **v3** | Phase 5 后 → Phase 6 前 | 长连线自动插 **Knot 中继**；纯数据节点作为「卫星」贴到其 Exec 主干左上；**Comment 框跟随** 其声明包含的节点                                                     | 3–5 天 |
| **v4** | Phase 7               | 「对整个 EventGraph / 当前函数图一键美化」入口（带快照回滚）；全局 Settings 开关汇总                                                                   | 2–3 天 |


#### 4.3.2 v2 任务拆解（Phase 5 内，先做）**— 已交付**


| 任务                          | 状态  | 说明                                                                                                       |
| --------------------------- | --- | -------------------------------------------------------------------------------------------------------- |
| **真实尺寸度量**                  | 已完成 | 走 `SGraphEditor::GetBoundsForNode`（蓝图编辑器打开时）；兜底 `NodeWidth/NodeHeight`；再兜底经验常量 (240×120)                 |
| **同列 X 对齐**                 | 已完成 | BFS 得到的 (Col, Row) 映射到**按列累加宽 / 按行累加高**的 ColX/RowY 表，避免固定 StepX/Y 造成叠加或过度稀疏                              |
| **碰撞避让**                    | 已完成 | 对本次排版 AABB 与图内既有非本次节点做矩形相交；被盖住的节点整体下移 `(OurBottom - TheirTop) + 32` 并级联（上限 5 次）                          |
| **Branch / Sequence Pin 序** | 已完成 | 构建 `FLayoutExecEdge { To, FromPinIndex }`，BFS 子节点按**源节点上的 exec pin 顺序**排序，True 在上/False 在下、Then0..N 自上而下 |
| **Settings 开关**             | 已完成 | `bAutoLayoutAfterExecute`（总开关，关闭后整个流水线跳过）、`bLayoutAvoidCollisions`                                       |
| **回归用例**                    | 已完成 | `TestScenarios` 第十节 **用例 28**（Branch + Sequence）                                                         |


#### 4.3.3 v3 任务拆解（Phase 5 后期 / Phase 6 前）


| 任务                  | 说明                                                                                                                                       |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| **Knot 中继自动插入**     | 触发条件：单条连线 `dx > 2*ColStepX` **或** 起止 pin Y 相差 > 1 行高；用 `FEdGraphSchemaAction_K2AddNode` 生成 `UK2Node_Knot`，插在「中点附近的空列」，避免穿越节点；Undo 事务整体生效 |
| **数据节点卫星布局**        | 无 Exec pin 的数据节点（Get/Set Variable、Math、Make/Break）识别出其「唯一 / 主要消费者」Exec 节点，贴到消费者左上方（列差 = -1，行差 = 0）；多消费者则取最左一个                            |
| **Comment 框跟随**     | `UEdGraphNode_Comment::NodesUnderComment` 非空时，排版后重算其 AABB（含 40px padding）并写回 `NodePosX/Y/NodeWidth/NodeHeight`，保持框始终包裹声明节点               |
| **分簇原生 Straighten** | v1 里一次性选中所有节点做 Straighten 在复杂图里效果反而不稳；改为按「BFS 一层」为单位分簇、逐簇调用 `SGraphEditor::OnStraightenConnections`                                      |
| **Settings 开关**     | `bInsertKnotsForLongWires` / `bAttachDataNodesAsSatellites` / `bAutoResizeCommentBoxes`                                                  |
| **回归用例**            | `TestScenarios` 新增 **用例 29–30**：含 Comment 框的 Spawn + Delay 链路、Get Variable → Math → Set Variable 数据链                                     |


#### 4.3.4 v4（Phase 7 内）

- 蓝图编辑器工具栏 / 面板新增「**AI 排版整张图**」按钮：对当前 `FocusedGraph` 全量节点跑一次 v3 排版，并在执行前做一次 `DuplicateObject` 快照，失败可一键还原
- `UBlueprintAIAssistantSettings` 汇总所有排版开关，并支持按项目保存；README / LDGuide 同步说明
- CI 冒烟（Phase 8）里增加一条：对一张「已知良好」蓝图跑一次整图排版，比对执行成功率是否不回退

#### 4.3.5 验收指标


| 指标                                      | 目标    | 测量方式                     |
| --------------------------------------- | ----- | ------------------------ |
| v2 交付后，本次节点**与既有节点 AABB 重叠率**           | = 0   | 回归用例 28 + 试用日志抽查 20 张图   |
| v3 交付后，「**连线穿过节点** 次数 / 总连线数」           | ≤ 3%  | 用例 29–30 + 手测            |
| 地编对排版主观评分（1–5，问卷中新增一项「**排出来是否不需要再手调**」） | ≥ 4.0 | Phase 5.B 发版后的下一次周反馈     |
| 回归：排版相关用例通过率                            | 100%  | `TestScenarios` 用例 28–30 |


#### 4.3.6 风险 & 应对

- **风险 A：Slate 几何缓存在编辑器未打开时取不到** → 已有经验常量兜底；Settings 里默认关闭「强依赖真实尺寸」的优化项
- **风险 B：Knot 插入污染 Undo 栈** → 与 DSL 执行共用同一个 `FScopedTransaction`；用户按一次 Ctrl+Z 即可回到"执行前状态"
- **风险 C：Comment 框重算误伤用户手动拖大的框** → 仅在 Comment 是本次 DSL `Comment` step 产生时重算；已存在的 Comment 用「只扩不缩」策略

---

### 5. Phase 6 — 交互/体验升级

#### 5.1 场景向导

把高频玩法做成"**场景向导**"：分类 → 勾选配置 → 生成 DSL。

示例：**按键交互开门**

- 选 Key（E / F）
- 选门类型（单扇/双扇/滑动）
- 是否需要音效/动画
- 点击「生成」→ LLM + 预设模板补齐 → DSL 预览

覆盖场景（优先级从高到低）：

1. 按键交互（开门 / 拾取 / 进入载具）
2. Trigger 触发器（进入区域 → 事件）
3. Spawn 特效/掉落物
4. 延时 / 序列（Delay、Sequence）
5. 简单血量/伤害系统
6. UI 屏幕文字提示

#### 5.2 多轮对话

- 同一蓝图上下文里延续追问（如"再把 Spawn 换成 BP_Cursor"）
- 保留对话历史，LLM 复用最近 N 轮上下文

#### 5.3 面板 UX

- 左侧：历史对话列表
- 右侧：当前对话 + DSL 预览
- 底部：快捷按钮区 / 模板按钮
- 支持 Markdown 渲染（用第三方 Slate 组件，可选）

#### 5.4 模糊需求到可执行蓝图的演进计划（Phase 6.A.1）

> 目标：把“用户一句模糊话”稳定地引导到“可执行、可验证、可回滚”的单蓝图 DSL；不盲目追求一键全自动跨蓝图。

##### A) 分诊优先，而不是直接执行

点击 **「生成可执行步骤（DSL）」** 后，先进行本地分诊：

- **DirectDsl**：需求清晰、单蓝图可落地 -> 直接生成 DSL
- **NeedClarify**：关键信息缺失（触发事件/对象来源/变量名等）-> 进入澄清问题
- **NeedPlan**：明显跨蓝图/系统联动 -> 先输出计划，再逐步落到单步 DSL

该策略已在面板中落地，避免“看似能执行、实际错图/错对象”的假成功。

##### B) 澄清模式（Clarify）

- 输出结构：`{"mode":"clarify","questions":[...]}`，问题数控制在 2~4 个
- UI：展示问题输入框 + **「基于补充信息生成 DSL」** 按钮
- 用户补充后，自动拼接回原始需求并重新走 DSL 生成

设计原则：只问“缺什么就补什么”，不重复追问“你想做什么”。

##### C) 计划模式（Plan）

- 输出结构：`{"mode":"plan","summary":"...","items":[{"stepId","title","targetHint","dslPrompt"}]}`
- UI：每条计划项旁提供 **「生成该步 DSL」** 按钮
- 执行策略：不做“一键跨蓝图全自动”，而是按步骤让用户先打开目标蓝图，再生成并执行该步 DSL

这保证了可控性、可回滚性，也更贴合当前执行器“单蓝图事务执行”的边界。

##### D) 产品边界（明确写给用户）

- AI 擅长：单蓝图脚手架、节点连线、常见玩法链路
- 人工更高效：跨蓝图职责划分、资产归属确认、项目命名约定
- 插件原则：**先把需求结构化，再执行局部确定步骤**

这就是“模糊需求 -> 澄清/计划 -> 单步执行”主路径的意义。

##### E) 验收与追踪

- 对应冒烟用例：`TestScenarios` 用例 38（澄清）与 39（计划）
- 对应勾选项：`Checklist` Phase 6.A.1（已落地）
- 对应用户手册：`LDGuide` §1.5（地编可感知流程）

##### F) 后续延伸顺序（与 Checklist 保持一致）

1. **Phase 6.B（首版已落地）**：基于现有 DSL 的增量修改（diff/patch，而非每次全量重写）；已接入结构化失败上下文、patch 应用防护、变更摘要与 `dsl_patch_result` 埋点。
2. **Phase 6.D（首版已落地）**：埋点 / KPI；高级工具可生成本地 markdown 报告，覆盖请求成功率、DSL 解析/执行成功率、重试、失败归因、patch 成功率与超时建议。
3. **Phase 6.C**：持久化（按地编强需求决定是否优先）

---

### 6. Phase 7 — 上下文感知 & 进阶能力


| 能力                       | 价值                                                                                                                                                                                   |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **读取当前选中节点**             | "把这两个节点之间加一个 Delay 0.2" 之类精准指令                                                                                                                                                       |
| **基于当前蓝图的 RAG**          | 提取蓝图的变量/函数/事件/组件，注入 system prompt 做检索增强                                                                                                                                              |
| **执行前 Diff 预览**          | 展示"将新增/修改哪些节点"（树状/图形式），用户确认后再执行                                                                                                                                                      |
| **蓝图快照与回滚**              | 执行前保存一次蓝图包，批量失败可一键回滚到执行前                                                                                                                                                             |
| **更多节点能力**               | Phase 3.D：`Cast` / `IsValid` / `Make/BreakVector` / `Make/BreakRotator`；Phase 3.E：`ForEachLoop`、`SwitchOnInt`/`SwitchOnString`、`Select`、`MakeArrayInt` 等已落地；Enum Switch / 更多节点可按日志扩展 |
| **整图一键 AI 排版**           | §4.3 v4：蓝图工具栏新增按钮，对当前 `FocusedGraph` 全量跑 v3 排版，执行前自动快照、失败一键还原；配合 `UBlueprintAIAssistantSettings` 全局开关                                                                                |
| **Function / Macro 图新建** | `CreateFunctionGraph` / `CreateMacroGraph` 能力扩 DSL                                                                                                                                   |


---

### 7. Phase 8 — 工程化 & 团队推广


| 任务        | 说明                                                               |
| --------- | ---------------------------------------------------------------- |
| **CI 冒烟** | 自动化 headless 跑 `BlueprintAIAssistant.Smoke.*`（fixture 位于 `Plugins/BlueprintAIAssistant/Tests/SmokeDsl/*.json`），由 `scripts/run-blueprint-ai-smoke.ps1` 一键运行；**GitHub Actions**：`.github/workflows/blueprint-ai-smoke.yml`（需 self-hosted `windows`+`ue5` runner，变量 `UE_ENGINE_ROOT`） |
| **团队配置**  | `DefaultBlueprintAIAssistant.ini` 支持团队级 Provider、白名单、Prompt 模板共享 |
| **错误上报**  | 把失败 DSL 匿名上报到内部分析服务（可选、需同意）                                      |
| **多语言**   | 如果要推广到海外团队，支持英文 Prompt                                           |
| **版本管理**  | 插件按语义化版本发布，带 CHANGELOG                                           |


---

### 8. 时间规划（参考）


| 周次    | 任务                                                                 | 里程碑           |
| ----- | ------------------------------------------------------------------ | ------------- |
| W1    | Phase 4：场景快捷按钮 + 使用日志 + 反馈按钮 + LD Guide                            | 地编试用包发版 v0.4  |
| W2    | Phase 4：试用期陪跑（每日答疑、收日志）                                            | 首周数据盘点        |
| W3    | Phase 5：连线预检 + 失败聚合 + 别名扩容 + 自动重问；**排版 v2（§4.3）：尺寸度量 + 碰撞避让 ✅已交付** | v0.5 成功率 80%+ |
| W4    | Phase 5：回归 + 二次确认 UI；**排版 v3（§4.3）：Knot 中继 + 数据卫星 + Comment 跟随**   | v0.5 Release  |
| W5-W6 | Phase 6：场景向导 Top 6 + 多轮对话                                          | v0.6          |
| W7-W8 | Phase 6：UX 重构、历史面板                                                 | v0.6 Release  |
| W9+   | Phase 7：上下文感知 / Diff 预览 / 快照回滚 / **排版 v4 整图一键美化**                  | v0.7          |


---

### 9. 决策点 / 退出条件

每个 Phase 结束后进行一次 Go/No-Go 评估：

- **Go**：指标达标 + 试用者愿意继续用 → 进入下一 Phase
- **No-Go**：
  - 如果**技术能力达标但用户不买账** → 暂停新功能，打磨 UX / 文档 / 模板，下一轮只做体验优化
  - 如果**技术能力不达标**（成功率长期 < 50%） → 考虑切换能力更强的模型 / 改 DSL Schema / 或缩窄适用场景

---

### 10. 相关文档索引

- `docs/README.md` —— 项目说明
- `docs/BlueprintAIAssistant_Checklist.md` —— 分阶段 Checklist（回归测试用）
- `docs/BlueprintAIAssistant_TestScenarios.md` —— 自测用例清单
- `docs/BlueprintAIAssistant_LDGuide.md` —— 地编使用手册（Phase 4 已补齐并随功能同步更新）
- `docs/BlueprintAIAssistant_PrebuiltPackaging.md` —— 预编译包打包规则（prebuilt_root）
- `docs/BlueprintAIAssistant_Roadmap.md` —— 当前文件（路线图）

