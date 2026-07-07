## Blueprint AI Assistant 自测用例清单

方便你在 DeepSeek / Gemini / OpenAI 兼容 等不同 Provider 下，快速验证插件是否工作正常。

### 使用说明

- 在蓝图编辑器中打开 `Blueprint AI Assistant` 面板。
- 顶部确认当前 Provider / Model / Endpoint 是否符合预期。
- 逐条复制下面的用例到输入框。
- 分别测试：
  - `生成建议` 按钮
  - `生成步骤清单` 按钮
  - `生成可执行步骤（DSL）` 按钮（Phase 2.5）

---

### 一、简单功能类（基础连通 + 基本建议）

#### 用例 1：按键交互开门（基础）

> 我想做一个按键 E 交互开门的蓝图，角色靠近门按下 E 就打开，再按一次关闭，请给我详细步骤。

自测要点：

- `生成建议`：是否返回清晰的文字说明（原理 + 步骤）。
- `生成步骤清单`：是否生成 3–8 条左右的“步骤1、步骤2…”列表。

#### 用例 2：Shift 加速跑

> 在第三人称模板里，想按住 Shift 加速跑，松开恢复正常速度，应该在哪个蓝图里加哪些节点？

#### 用例 3：简单血量系统

> 帮我设计一个简单的血量系统蓝图，有“当前血量”和“最大血量”，受到伤害时掉血，血量小于等于 0 播放死亡动画。

---

### 二、蓝图编辑器操作类（考察对编辑器操作的理解）

#### 用例 4：新增变量并打印

> 在角色蓝图里新增一个名为 Health 的 float 变量，并在 Event BeginPlay 打印它的值，请一步步告诉我怎么做。

#### 用例 5：Timeline 开门动画

> 用 Timeline 实现一个平滑打开的门动画（旋转门），Timeline 节点应该怎么接？请用步骤说明。

#### 用例 6：UI 按键提示 + 开门

> 玩家靠近门时在屏幕中间显示“按 E 开门”的提示，离开范围提示消失，按 E 时触发开门蓝图，请帮我给出整体思路和节点搭建步骤。

---

### 三、调试 / 报错排查类

#### 用例 7：Accessed None 报错

> 蓝图里提示 “Accessed None trying to read property”，一般是什么原因？请结合 Unreal 蓝图给我分析和排查步骤。

#### 用例 8：Overlap 不触发

> 我在蓝图里绑定了 OnComponentBeginOverlap 事件，但是游戏里走过去没有触发，可能有哪些原因？请列出排查清单。

---

### 四、上下文敏感类（在具体蓝图中测试）

> 建议在打开一个实际项目蓝图（比如角色 BP、交互 BP）时使用。

#### 用例 9：当前蓝图结构解释

> 请帮我概括一下当前蓝图的主要功能和结构，只看变量和函数命名做一个高层总结。

#### 用例 10：为当前蓝图增加一个“重置状态”逻辑

> 在当前蓝图里，我想加一个“重置状态”的功能，让角色回到初始位置/初始血量/关闭交互对象，请结合当前变量和函数命名，给出推荐的实现方式。

---

### 五、Provider 切换自测建议

对每个 Provider（DeepSeek / Gemini / OpenAI 兼容 / 以后可能接入的更多模型），都可以用下面最小用例验证：

#### 用例 11：最小连通性 + 中文回答

> 简单介绍一下你是谁，以及你在这个工程中的角色是什么？请用简体中文回答。

#### 用例 12：JSON 步骤清单（严格约束）

> 针对第三人称模板中的角色移动逻辑，给出一份修改“按住 Shift 额外加速”的步骤清单，注意按照系统提示的 JSON 格式输出。

预期：

- `生成建议` 始终返回中文说明。
- `生成步骤清单` 在大多数情况下能成功解析为步骤列表；若失败，面板会显示“步骤清单解析失败 + 原始输出”，你可以把那段原始输出发给助手继续调优。**

---

### 六、Phase 2.5：DSL / 半自动执行（冒烟）

> 目标：验证 “生成 DSL JSON → 面板预览 → 单步/批量执行 → 可撤销” 的最小闭环。

#### 冒烟前置

- 打开任意 Blueprint（例如新建 `BP_Test`），进入它的 EventGraph
- 打开 `Blueprint AI Assistant` 面板

#### 用例 13：生成 DSL（最小链路）

> 生成一个最小可执行链路：按键后 Delay 0.2 秒，然后 PrintString “Hello”

自测要点：

- 点击 `生成可执行步骤（DSL）` 后，下方出现 **DSL 步骤预览**（多行）
- 每行有：勾选框 + 描述 + “执行”按钮

#### 用例 14：单步执行 + Ctrl+Z

操作：

- 在 DSL 步骤预览中，任选一行点击 “执行”
- 然后在蓝图编辑器中按 `Ctrl+Z`

预期：

- EventGraph 中会新增对应节点（例如 Delay 或 PrintString）
- 面板提示 “第 N 步执行成功/失败原因”
- `Ctrl+Z` 能撤销刚才那一步

#### 用例 15：批量执行（勾选参与）+ 整体撤销

操作：

- 保持默认全勾选（或只勾选你想执行的几步）
- 点击 “执行所选 DSL 步骤”
- 然后按一次 `Ctrl+Z`

预期：

- EventGraph 中出现一条最小链路（例如 Delay → PrintString）
- 若执行失败：面板显示失败原因，并提示“已自动停止在第 N 步”
- `Ctrl+Z` 一次撤销整批执行（事务生效）

---

### 七、Phase 3：高级节点覆盖（冒烟）

> 目标：验证 Trace（Visibility）、SpawnActor（白名单）、PlayAnimMontage（B 线：动画）等高级能力的最小可用闭环。

#### 用例 16：LineTrace（Visibility）+ 打印是否命中

> 生成可执行 DSL：在 EventGraph 的 BeginPlay 中，从 ActorLocation 向前方 1000 单位做一次 LineTrace（TraceChannel 固定 Visibility，IgnoreSelf=true，DebugDraw=None）。把返回的 ReturnValue（是否命中）用 PrintString 打印出来。要求：只输出 DSL JSON。

自测要点：

- DSL 里出现 `CallFunction(LineTrace...)` 节点，且无需你手动填 TraceChannel/IgnoreSelf/DebugDraw
- 执行后能看到一条 BeginPlay -> LineTrace -> PrintString 链路

#### 用例 17：SpawnActor（白名单）+ requiresConfirmation

> 生成可执行 DSL：在 EventGraph 的 BeginPlay 中 Spawn 一个 BP_FireBall（使用 SpawnActorFromClass），并在该步骤标记 requiresConfirmation=true。Spawn 成功后 PrintString “Spawned”。只输出 DSL JSON。

自测要点：

- 若你勾选批量执行，面板会给出 requiresConfirmation 的校验警告（仍可执行）
- 仅允许 BP_Cursor / BP_Door_01 / BP_FireBall 三个类；其它类应被拒绝

#### 用例 18：PlayAnimMontage（动画 B 线）

前置：

- 当前蓝图是 Character（或能拿到 Character 实例）
- 你有一个可用的 AnimMontage 资产路径（例如 `/Game/.../M_MyMontage.M_MyMontage`）

> 生成可执行 DSL：在 BeginPlay 中对 Self Character 调用 PlayAnimMontage（CallFunction=PlayAnimMontage），Montage 引脚用 SetPinDefault 设为上述 AnimMontage 资源路径，并 PrintString “Played”。只输出 DSL JSON。

自测要点：

- `SetPinDefault` 支持把对象 pin 的 DefaultObject 设为 /Game 路径加载出来的 Montage
- PlayAnimMontage 若找不到 Character 上下文，应在 DSL description 中提示需要用户确认对象来源

---

### 八、Phase 3.D：新增常用节点（冒烟）

> 目标：验证 Cast / IsValid / MakeVector / MakeRotator / BreakVector / BreakRotator 的最小可用流。建议先在一个空白的 Actor Blueprint 里做。

#### 用例 19：Cast To BP_Door_01 + IsValid

前置：

- 项目里存在 `BP_Door_01` 蓝图（默认 Spawn 白名单之一，Cast 不需要白名单）。

> 生成可执行 DSL：在 BeginPlay 中，先用 GetPlayerCharacter 拿到一个 Character，再把它 Cast To BP_Door_01（nodeType=Cast, targetClass="BP_Door_01"），Cast 成功分支接 PrintString "Cast OK"，失败分支接 PrintString "Cast Failed"。只输出 DSL JSON，不要 Markdown。

自测要点：

- DSL 里出现 `{nodeType:"Cast", targetClass:"BP_Door_01"}` 形式的 CreateNode。
- **执行线**：`GetPlayerCharacter` 是 Pure 节点，没有 `execute` 输入；`BeginPlay` 的 `then` 必须连到 **Cast 的 `execute`**，不能把执行线连到 GetPlayerCharacter。数据：`GetPlayerCharacter.ReturnValue` → `Cast.Object`。
- 执行后 EventGraph 里出现一个 `Cast To BP_Door_01` 节点，连线正确区分成功 / 失败两路 Exec。

#### 用例 20：IsValid + Branch

> 生成可执行 DSL：在 BeginPlay 中，用 GetPlayerCharacter 取到角色，再用 IsValid 节点（nodeType=IsValid）判断是否有效，bool 返回值接到 Branch 的 Condition，True 分支 PrintString "Valid"，False 分支 PrintString "Invalid"。只输出 DSL JSON。

自测要点：

- DSL 里 IsValid 是函数版（Pure，无 Exec 线）。
- 需要再显式创建一个 Branch 节点把 bool 结果拆成两路 Exec；这正是我们希望 LLM 学会做的。

#### 用例 21：MakeVector + SetActorLocation

前置：

- 当前蓝图是 Actor / Character（有 SetActorLocation）。

> 生成可执行 DSL：在 BeginPlay 中，用 MakeVector（X=100, Y=200, Z=300）生成一个向量，然后把它连到 SetActorLocation 的 NewLocation 引脚。只输出 DSL JSON。

自测要点：

- DSL 中出现 `{nodeType:"MakeVector"}` 节点，并通过 `SetPinDefault` 或字面量直接设置 X/Y/Z。
- **ConnectPins**：MakeVector 对应 `**UKismetMathLibrary::MakeVector`**，输出为 `**ReturnValue`**（FVector）；连到 `SetActorLocation` 的 `NewLocation`。
- 执行后 EventGraph 里出现 `Make Vector` → `SetActorLocation` 的正确连线。

#### 用例 22：BreakVector + 打印 X/Y/Z

> 生成可执行 DSL：在 BeginPlay 中，用 GetActorLocation 拿到当前位置，用 BreakVector 拆成 X/Y/Z，再用 PrintString 打印 X。只输出 DSL JSON。

自测要点：

- DSL 出现 `{nodeType:"BreakVector"}` 节点。
- **ConnectPins**：Break Vector 的向量输入内部名为 `**Vector`**；`GetActorLocation` 的 `ReturnValue` → Break 的 `**Vector`**（若 DSL 写 `InVec`，执行器会映射为 `Vector`）。
- X 引脚最终能通过某种类型转换（或依赖蓝图的隐式转换）接到 PrintString 的 InString，若 LLM 未自动加 ToString 转换，可以在自测里手动补。

#### 用例 23（可选）：MakeRotator / BreakRotator

> 生成可执行 DSL：在 BeginPlay 中，用 MakeRotator (Pitch=0, Yaw=90, Roll=0) 做出一个旋转，再用 BreakRotator 拆回 Yaw，PrintString 打印 Yaw。只输出 DSL JSON。

自测要点：

- 插件生成的是 `**UKismetMathLibrary::MakeRotator` / `BreakRotator**`（CallFunction）。**MakeRotator** 的 `UFUNCTION` 参数顺序是 `**Roll / Pitch / Yaw`**（与口头「Pitch、Yaw、Roll」顺序不同），`SetPinDefault` 或连线时请用真实引脚名 `**Roll`、`Pitch`、`Yaw`**；BreakRotator 输入为 `**InRot**`，输出 `Roll/Pitch/Yaw`；若 DSL 把旋转输入写成 `**Rotator**`，执行器会映射到 `**InRot**`。
- 确认两种节点能创建、且 **Break 的 `Yaw` → PrintString** 这条数据链能连上（若类型不对，同用例 22 加 `Float`→`String` 转换）。
- smoke：节点生成 + 关键连线命中即可，不必强求一次完整跑通。

#### 用例 24：Spawn 白名单 - 分层配置（L2 路径前缀冒烟）

> 目标：验证 **L1 显式类名** 之外的 `**SpawnPathPrefixes`（L2）** 能放行指定目录下的蓝图，而不必把每个类都写进 L1。

##### 前置：在工程里准备一个测试蓝图

1. 在 Content Browser 中建目录（示例）：`Content/Blueprints/SpawnTests/`（磁盘路径随项目，**资产前缀以 `/Game/Blueprints/SpawnTests/` 为准**）。
2. **新建蓝图**：`Blueprint` → `Actor`，命名为 `**BP_TestSpawnable`**，父类 `Actor` 即可，保存到上述目录。
  - 保存后资产路径应类似：`/Game/Blueprints/SpawnTests/BP_TestSpawnable`。
3. **确认 L1 不包含它**：打开 `Project Settings → Blueprint AI Assistant → DSL | SpawnActor 白名单`，在 `**SpawnClassWhitelist`** 里**不要**添加 `BP_TestSpawnable`（只保留你原来的 L1 条目即可）。这样若不走 L2，Spawn 会被拒绝。

##### 配置 L2

1. 仍在同一设置页，找到 `**SpawnPathPrefixes`**。
2. **新增一条**（与第 1 步目录一致，注意末尾建议带 `/`）：
  `/Game/Blueprints/SpawnTests/`  
   （若你把蓝图放在别的目录，把前缀改成该目录的 `**/Game/.../`** 前缀即可。）
3. 保存设置（必要时重启编辑器使配置完全生效；多数情况下立即生效）。

##### 冒烟 DSL（可直接用于「生成可执行步骤（DSL）」）

把下面整段复制到面板输入框（或略改描述，但保留 **SpawnActorFromClass + BP_TestSpawnable + requiresConfirmation**）：

> 生成可执行 DSL JSON（version 2）：在 EventGraph 的 BeginPlay 执行链上，用 `SpawnActorFromClass` 生成 `**BP_TestSpawnable`**，`targetClass` 填 `BP_TestSpawnable`，`requiresConfirmation=true`。Spawn 成功后 `PrintString` 输出 `SpawnTests OK`。只输出 JSON，不要 Markdown。

**手工检查 DSL 要点（若模型生成有误可手改预览里的 JSON）：**

- 有一条 `CreateNode`，`nodeType` 为 `SpawnActorFromClass`，`targetClass`（或兼容字段 `functionName`）为 `**BP_TestSpawnable`**。
- 高风险步骤带 `**requiresConfirmation": true`**（批量执行时面板会警告，仍可继续）。

##### 预期结果

- **前缀已配置且路径匹配**：执行 Spawn 步骤应**成功**，日志/面板无「未命中白名单」类错误。
- **验证 L2 生效**：在设置里**删掉** `SpawnPathPrefixes` 里刚加的那条（或改成不匹配的目录），**不要**把 `BP_TestSpawnable` 加进 L1，**再执行同一段 DSL**，应**失败**，并出现与「未命中任一层白名单 / 无法解析」相关的提示（与当前执行器文案一致即可）。

##### 可选：对照 L1

将 `**BP_TestSpawnable`** 临时加进 `**SpawnClassWhitelist`** 后，即使去掉 L2 前缀，只要类名在白名单里也应能 Spawn（用于区分 L1 与 L2，按需做）。

---

### 九、Phase 3.E（P0）：遍历 / 多分支 / Select / Float 打印（冒烟）

> 目标：验证 P0 节点覆盖：`ForEachLoop`、`SwitchOnInt`、`Select` 以及 float→string 的最小闭环。

#### 用例 25：ForEachLoop 遍历数组并打印

> 生成可执行 DSL：在 BeginPlay 中先创建一个 `MakeArrayInt`（cases=["1","2","3"]），再把它连到 ForEachLoop 的 Array 输入；每次 LoopBody 里把元素转换成字符串并 PrintString 打印出来。只输出 DSL JSON。

自测要点：

- ForEachLoop 是 **宏节点**（StandardMacros），应能创建并出现 `Array` / `LoopBody` / `Completed` 等引脚。
- 该用例避免「新建数组变量 / SetVariable」的不稳定性，直接用 `MakeArrayInt` 做字面量数组。
- 数值打印建议模型显式加 `Conv_*ToString`；float→string 若仍连接失败，执行器会在目标为 PrintString.InString 时自动插入 `Conv_FloatToString` 节点兜底。

#### 用例 26：SwitchOnInt（cases=0/1/2）

> 生成可执行 DSL：在 BeginPlay 中创建一个 int 变量 Mode=1，用 SwitchOnInt（cases=[0,1,2]）对 Mode 分三路：0 打印 Zero，1 打印 One，2 打印 Two，Default 打印 Other。只输出 DSL JSON。

自测要点：

- `cases` 数量决定 Switch 的分支数量，cases[0] 为 StartIndex（连续整数序列约定）。
- `Selection` pin 必须连到 Mode。
- **常见失败**：用 `CreateNode` + `nodeType:SetVariable` 时忘记在 JSON 里写 `varName`，只在 `description` 里写了变量名 → 解析后 `varName` 为空。正确示例：`{"action":"CreateNode","nodeId":"VarSet_1","nodeType":"SetVariable","varName":"Mode","defaultValue":"1",...}`（变量需已在蓝图中存在；`defaultValue` 由执行器写入 Set 节点的值引脚）。

#### 用例 27：Select（3 选 1）

> 生成可执行 DSL：在 BeginPlay 中用 Select 节点准备 3 个字符串选项 A/B/C（cases=[A,B,C]），Index=2 时选 C，然后 PrintString 打印选择结果。只输出 DSL JSON。

自测要点：

- Select 应能按 cases 扩展 option pins 到 3 个。
- 若 Index/Option pin 名写错，面板应给出可用 pins（DumpNodePinsForDebug）。
- **常见错误**：把选项**内容**当成 **pinName**（如 `pinName:A` 想表达「Option 0 默认值为 A」）。引擎里选项输入脚名为 `**Option 0`**、`**Option 1`**…；正确写法是 `pinName:Option 0` + `defaultValue:A`，或依赖执行器对 `pinName:A` 与 `cases` 的映射（插件会在创建 Select 时写入 `BPAI_CASES` 元数据并解析）。
- **Wildcard 无字面量**：若 DSL **先 SetPinDefault 再 ConnectPins**，Select 仍为通配符，`TrySetDefaultValue` 可能不报错但图上不显示。执行器在 `SetPinDefault` 时会将 Select **定型为 String（选项/Return）与 Int（Index）** 再写入默认值。

---

### 十、Phase 5.B：自动排版（v2）冒烟

> 目标：验证 v2 真实尺寸度量、同列对齐、Branch/Sequence 按 pin Y 排序、对既有节点做碰撞避让。
>
> 自测前置：
>
> - 打开任意一张**已有若干用户节点**的蓝图（EventGraph 非空为佳），保持蓝图编辑器可见（便于 Slate 测量 + 最后的 Straighten 生效）。
> - 项目设置 → Editor → `Blueprint AI Assistant`：确认 `DSL | 自动排版 / bAutoLayoutAfterExecute = true`、`bLayoutAvoidCollisions = true`（默认）。

#### 用例 28：Branch 两分支 + Sequence

> 生成可执行 DSL：在 BeginPlay 之后放一个 Branch，Condition 任意字面量 true；True 分支打印 "T"，False 分支打印 "F"；在 Branch 之后再接一个 Sequence，Then0 打印 "S0"，Then1 打印 "S1"。只输出 DSL JSON。

自测要点：

- **分支方向**：执行后 Branch 的 True 应在上方、False 在下方；Sequence 的 Then0 / Then1 自上而下；不再出现「反着排」。
- **无叠加**：新生成的节点应与已有用户节点**没有任何矩形重叠**（目测 / 可选：按住 Alt 选中观察）；被压到的既有节点应整体下移，其他既有节点不移动。
- **列宽自适应**：若蓝图里有「宽度特别大的节点」（如带长字面量的 PrintString），该列之后的节点应相应右移，不会叠进该节点右侧。
- **编辑器未打开回退**：临时关闭蓝图编辑器后再重放一遍同一 DSL，应仍能得到规则布局（只是列宽偏保守 = 经验常量 240×120）；不会崩溃、不会打断 DSL 执行结果。
- **开关关闭回退**：把 `bAutoLayoutAfterExecute` 关掉重放，应只有节点创建 / 连线，不做任何位移 & 不调 Straighten。

---

### 十一、Phase 5：执行失败自动重试（单步/批量）冒烟

> 目标：验证 `bAutoRetryOnceOnDslExecFailure` 对「批量执行」与「单步执行」都生效（失败后先 Undo，再自动修复并重试 1 次）。

#### 用例 29：单步执行失败 → 自动修复重试

> 生成可执行 DSL：先创建一个 `PrintString`，然后故意用 `ConnectPins` 把 `PrintString` 的某个**不存在的 pinName** 连接（例如 `toPin:\"InStr\"` 这种拼写错误），让该步骤必然失败。只输出 DSL JSON。

自测要点：

- 在 DSL 预览中对该失败步骤点行末 **「执行」**：
  - 第一次应失败并显示错误；
  - 若开启 `bAutoRetryOnceOnDslExecFailure`，应提示“自动修复并重试一次”，并在重试前 **Undo 回滚**（图上不应留下半执行残留）；
  - 若修复成功，应显示“单步已自动修复并重试执行完成”，并且该行 DSL（若修复只输出 1 步）会被替换为修正版。
- 若把 `bAutoRetryOnceOnDslExecFailure` 关掉：应只失败一次，不会重试。

#### 用例 30：批量执行失败 → 自动修复重试

> 用与用例 29 类似的方式构造一个「必然失败」的 steps（比如第二步 pin 名拼错），勾选后点 **「执行所选 DSL 步骤」**。

自测要点：

- 第一次批量执行失败后，应先 **Undo 回滚**本次批量事务，再自动修复并重试 1 次；
- 若修复后的 DSL 仍含 `requiresConfirmation=true`，应弹窗确认。

---

### 十二、Phase 5：Schema 清洗/纠错（无需重试）冒烟

> 目标：验证 DSL 输出“轻微不规范”时，仍能被解析通过（不触发自动重试）。

#### 用例 31：steps 字段别名 + action 缺失

> 把下面这段 **故意不规范** 的 JSON 粘进「DSL 解析」路径（可用任意 provider 生成后手动改，或直接把它当作模型输出进行调试）：\n
>
> - 根字段用 `Actions` 代替 `steps`\n
> - step 不写 `action`，只写 `nodeType/nodeId` 让解析器推断为 CreateNode\n
> \n
> 预期：应能正常解析并显示 DSL 预览（不需要触发自动重试）。

示例（仅供手改测试）：\n

```json
{
  \"version\": 2,
  \"Actions\": [
    {\"stepId\":\"S1\",\"nodeId\":\"N1\",\"nodeType\":\"Branch\",\"targetGraph\":\"EventGraph\",\"description\":\"(no action field)\"}
  ]
}
```

#### 用例 32：snake_case action + requires_confirmation 字段

> 预期：`create_node` / `connect_pins` / `set_pin_default` 等 snake_case action 能被归一化识别；`requires_confirmation` 会被识别为 `requiresConfirmation`。

---

### 十三、模板与排版 v3（回归补齐）

> 目标：补齐先前约定的用例 **34–37**（金币拾取 + 排版 v3：Knot / 数据卫星 / Comment 只扩不缩），用于回归验证地编可交付性。

#### 用例 33：Pin 别名 v2（候选导入 → 勾选写入 → 热更新生效）

前置：

- 已产生过 `pin-alias-suggestions-*.md`（来自面板“生成 Pin 别名候选”或 usage 日志分析）。

操作：

1. 打开 `Blueprint AI Assistant` 面板。
2. 点击 **“导入候选”**，确认候选列表出现（带复选框）。
3. 勾选 1～3 条常见别名（如显示名→真实 PinName）。
4. 点击 **“写入并启用别名表”**，再点击 **“重新加载别名表”**（或仅前者，视实现提示）。
5. 构造一个会用到该别名的 DSL（或手动编辑 DSL 的 pinName 为“显示名”），执行 ConnectPins。

预期：

- `Saved/BlueprintAIAssistant/pin-aliases.json` 被写入（包含 version + aliases 数组）。
- 重新执行后 ConnectPins 能命中真实 pin，不再报“pin not found”。

#### 用例 34：金币拾取模板（ComponentOverlap 优先，ActorOverlap 兜底）

> 在一个放置在场景中的金币 Actor（带碰撞组件）里，做拾取逻辑：玩家碰到金币后金币销毁，并给玩家加 1 金币数（若没有金币数变量就先 PrintString 提示）。要求优先使用 `OnComponentBeginOverlap`，找不到合适组件再用 `OnActorBeginOverlap`。只输出可执行 DSL JSON。

自测要点：

- 事件节点优先为 `**OnComponentBeginOverlap`**（若蓝图可绑定到合适 `UPrimitiveComponent`）。
- 若组件不可用（或无法解析），应能退化到 `**OnActorBeginOverlap`**，但仍能执行链路完整。
- 生成的 DSL 不应只用 `PrintString` 代替核心逻辑（销毁/加数至少要有一个真实效果）。

#### 用例 35：长 Exec 线穿节点 → 自动插入 Knot + 分簇 Straighten

前置：

- 打开一张 EventGraph 节点较多的蓝图（中间有一些“宽节点”或散落节点，便于制造“连线穿过节点”的场景）。
- Settings 确认：
  - `bAutoLayoutAfterExecute = true`
  - `bLayoutInsertKnotsForLongWires = true`

> 生成可执行 DSL：在 BeginPlay 后串 6～10 个可执行节点（比如 Delay/PrintString/Sequence/Branch 的组合），要求中间至少出现一次“跨度很大”的执行连接（例如从最左的节点连到最右的节点），并且让这条执行线尽量穿过其他节点区域。只输出 DSL JSON。

验证点：

- 执行完成后，能看到至少 1 个 `Knot`（Reroute）节点被自动插入到长执行线中。
- Straighten 是“按簇执行线组件分簇”应用的：复杂图里不会把不相关节点拉扯到一起。
- 关闭 `bLayoutInsertKnotsForLongWires` 复跑，同一段 DSL 不应再插 Knot（用于对照开关有效）。

#### 用例 36：纯数据节点卫星贴靠（Get→Math→Set）

前置：

- Settings 确认：
  - `bAutoLayoutAfterExecute = true`
  - `bLayoutAttachDataNodesAsSatellites = true`

> 生成可执行 DSL：在 BeginPlay 的执行链上，创建一个 `GetVariable`（或 GetActorLocation 等纯数据节点），再接一个简单数学节点（如 Float + Float / Vector + Vector / Clamp），最后把结果接到一个可执行节点的输入（例如 PrintString 的 InString 或 SetActorLocation 的 NewLocation）。要求让这些纯数据节点在语义上“服务于”某个 Exec 主干节点。只输出 DSL JSON。

验证点：

- 执行后：纯数据节点（无 Exec pin）会被布局为“贴靠在其主要消费者节点附近”（通常在左上侧），不再漂在远处。
- 关闭 `bLayoutAttachDataNodesAsSatellites` 复跑，数据节点不再强制贴靠（对照开关有效）。

#### 用例 37：Comment 只扩不缩（避免误伤用户手调）

前置：

- 图中已有一个你手动拖大的 Comment 框（比其包含节点的包围盒更大）。
- Settings 确认：
  - `bAutoLayoutAfterExecute = true`
  - `bLayoutAutoResizeCommentBoxes = true`

操作：

1. 让 DSL 本次执行在该 Comment 框内新增/移动若干节点（或新增一个 Comment step）。
2. 执行并自动排版。

验证点：

- Comment 框会“跟随并包住”相关节点，但遵循 **只扩不缩**：不会把你原本手动加大的空白缩掉。
- 若 Comment 不在本次 NodeSet（非本次生成/涉及），应尽量保持其现状不被移动/收缩（根据当前实现策略验证即可）。

---

### 十四、Phase 6.A.1：分诊 / 澄清 / 计划（冒烟）

#### 用例 38：模糊需求 → 自动进入澄清问题

> 我想做一个“任务系统”，玩家完成任务后给奖励。先给我生成可执行 DSL。

预期：

- 点击 **「生成可执行步骤（DSL）」** 后，不应直接输出 DSL 预览，而是出现 **“需要补充信息（澄清问题）”** 区块，包含 2～4 个问题输入框。
- 随便填写 1～2 项后点 **「基于补充信息生成 DSL」**，进入 DSL 生成流程并出现 DSL 预览（能否完全正确不做硬性要求，重点是流程可推进）。

#### 用例 39：跨蓝图需求 → 自动进入计划模式 + 可逐步生成单步 DSL

> 我想做金币拾取：捡到金币加到玩家金币数里，同时 HUD 上显示金币数量，金币满 10 个就打开一扇门。请帮我实现。

预期：

- 点击 **「生成可执行步骤（DSL）」** 后，出现 **“跨蓝图实施计划（可逐步落到 DSL）”** 区块，列出 3～8 条计划步。
- 每条计划步旁有 **「生成该步 DSL」** 按钮；点击后进入 DSL 生成，并提示/要求先打开对应蓝图（由计划中的 targetHint/dslPrompt 引导）。
- 该流程不要求“一键跨蓝图全部执行”，以“可控地逐步落地”为验收点。

---

### 十五、近期反馈修复回归（P0/P1 + 可选节点容错）

> 目标：覆盖 2026-04 反馈高频失败点（AnyDamage、Overlap 回退、Int 算术、跨对象变量、可选函数跳过与自动补链、手动前置提醒）。

#### 用例 40：AnyDamage 受伤逻辑（事件别名 + Int/Float 运算）

> 生成可执行 DSL：在 EventGraph 中处理 `Event AnyDamage`（注意保留空格写法），执行 `Health = Health - Damage`，`Health <= 0` 打印 `Dead`，否则打印当前血量。只输出 DSL JSON。

验证点：

- `fromNodeId: Event AnyDamage` 能被识别（不再报 node not found）。
- 若图中无该事件，执行器可自动创建 `ReceiveAnyDamage` 事件节点。
- `Float - Float`、`Conv_*ToString` 不再成为首要阻断项。

#### 用例 41：金币拾取（ComponentOverlap 不可用时自动回退 ActorOverlap）

> 生成可执行 DSL：金币 Actor 上优先 `OnComponentBeginOverlap`，若不可绑定则回退 `OnActorBeginOverlap`；玩家 `CoinCount += 1` 后销毁金币。只输出 DSL JSON。

验证点：

- 当组件事件不可绑定时，不会在该步硬失败，主链可继续通过 `OnActorBeginOverlap` 运行。
- 执行摘要中可看到成功/跳过信息，不出现大规模连锁失败。

#### 用例 42：可选 UpdateCoinUI 缺失（软失败 + Exec 自动重连）

> 在金币拾取 DSL 中加入可选步骤：调用玩家函数 `UpdateCoinUI`；描述里明确“若函数不存在可跳过”。只输出 DSL JSON。

验证点：

- `UpdateCoinUI` 不存在时，该节点被标记为“可选跳过”，不计入硬失败。
- 与该可选节点相关的执行连接可自动桥接（上游 `then` 直连下游 `execute`）。
- 失败引导卡片显示“**可选步骤已自动处理**”及对应摘要信息。

#### 用例 43：手动前置条件显式提醒（变量/函数未建）

> 生成包含前置提示的 DSL：例如“若玩家无 `CoinCount` 变量请先手动创建 int CoinCount=0”。执行前直接点批量执行。

验证点：

- DSL 预览区顶部出现“手动前置条件”醒目提醒卡片。
- 执行前出现确认弹窗，明确建议先完成前置步骤。
- 取消执行后不产生蓝图改动。

---

### 十六、Phase 6.B（增量 patch）回归

> 目标：验证“基于失败上下文做最小 patch”的稳定性，避免全量重写导致步骤漂移。

#### 用例 44：结构化失败上下文进入 patch prompt

操作：

1. 先准备一组会触发 `ConnectPins` 失败的 DSL（例如 `Cast.Object -> Print.execute`）。
2. 触发失败后，点击 **「请求增量修复（Patch）」**。

验证点：

- patch 请求不是只带自然语言摘要，而是包含结构化失败信息（类别、stepIndex、node/pin、from/to）。
- 若存在首个失败对应原始 step，prompt 中包含该 step 的严格 JSON 片段，便于模型做“就地修复”。

#### 用例 45：patch 应用防护（错误可读）

操作：

1. 人工构造/模拟 patch 响应，分别覆盖：
  - 未知 `op`
  - `remove/replace` 缺失或找不到 `stepId`
  - `insertAfter` 缺失或找不到 `afterStepId`
  - 新增 stepId 与现有冲突

验证点：

- 面板明确报出“哪类 patch 违规 + 具体字段问题”，而不是泛化失败。
- 违规 patch 不应部分污染当前 DSL（保持原 steps 可继续编辑/执行）。
- usage 日志记录 `dsl_patch_result`，包含 `ok=false`、`opCount/appliedCount` 与错误摘要。

#### 用例 46：两轮连续 patch 不漂移

操作：

1. 第 1 轮 patch：修复首个连接失败。
2. 不重新全量生成 DSL，继续制造/触发另一个小失败，再做第 2 轮 patch。

验证点：

- 第 2 轮仍在当前 steps 基础上做局部修复，不出现大范围重排或无关步骤丢失。
- StepId 保持稳定且唯一；最终步骤可执行率较第 1 轮提升。
- patch 成功提示中展示“本次变更（applied/op）”与 remove/replace/insert/append 摘要，便于人工复查。

---

### 十七、Phase 6.D（usage KPI 报告）回归

> 目标：验证试用日志可以被一键聚合为可复盘指标，而不是只依赖人工翻日志。

#### 用例 47：生成本地 KPI 报告

操作：

1. 先完成若干次 `生成可执行步骤（DSL）`、执行、失败修复或 patch 操作，确保 `Saved/BlueprintAIAssistant/usage-*.log` 中已有数据。
2. 打开面板的 **高级工具**，点击 **「生成 KPI 报告」**。

验证点：

- 在日志目录生成 `usage-kpi-report-*.md`。
- 报告包含：模型请求成功率、DSL 解析成功率、DSL 批量/单步执行成功率、自动重试、DSL Patch、超时建议。
- 报告包含分布统计：请求类别、Provider、失败归因 Top、Retry 阶段、超时原因。
- 若没有 usage 日志，面板给出明确提示，不产生空报告。

---

### 十八、Phase 8（Headless CI 冒烟）回归

> 目标：把“手动点面板烟测”变成“命令行一键跑 + 可作为 CI gate 的退出码”。\n

#### 用例 48：Headless 运行 BlueprintAIAssistant.Smoke

操作：

1. 在命令行执行：
  `powershell -ExecutionPolicy Bypass -File .\\scripts\\run-blueprint-ai-smoke.ps1 -BuildFirst`

验证点：

- 生成 `Saved/BlueprintAIAssistant/smoke/smoke-*.log` 与 `smoke-summary-*.md`。
- smoke 通过时脚本退出码为 0；失败时退出码非 0（可用于 CI gate）。
- smoke 扫描 fixture：`Plugins/BlueprintAIAssistant/Tests/SmokeDsl/*.json`，至少覆盖 parse/validate/execute 三层用例。

