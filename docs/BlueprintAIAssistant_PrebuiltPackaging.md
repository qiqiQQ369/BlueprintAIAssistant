## Blueprint AI Assistant 预编译包（Prebuilt Root）打包规则

目标：生成一个**解压到项目根目录即可用**的 zip，方便地编直接拷贝/覆盖更新插件。

> 约定：本文档描述的包称为 **prebuilt_root** 包，因为 zip 顶层直接是“项目根目录的相对结构”（含 `Plugins/` 与 `docs/`）。

---

### 1. 包结构（必须保持一致）

zip 顶层必须是下面两个目录（不要多一层根文件夹）：

- `Plugins/BlueprintAIAssistant/`
- `docs/`

解压后的目标位置（项目根目录）示例：

```
<ProjectRoot>/
  Plugins/
    BlueprintAIAssistant/
      BlueprintAIAssistant.uplugin
      Binaries/Win64/...
      Source/...
      Intermediate/...
      Config/...
  docs/
    BlueprintAIAssistant_Roadmap.md
    BlueprintAIAssistant_Checklist.md
    BlueprintAIAssistant_TestScenarios.md
    BlueprintAIAssistant_LDGuide.md
    ...
```

---

### 2. 打包前置条件（强烈建议）

- 先编译一遍 Editor（确保 `Binaries/Win64/*.dll` 与 `UnrealEditor.modules` 是最新的）
- 确认 `Plugins/BlueprintAIAssistant/BlueprintAIAssistant.uplugin` 正常
- 如果你希望包更小/更干净：可以把 `Intermediate/` 排除（但**本项目历史包结构包含 Intermediate**，除非你打算升级规则，否则保持一致）

---

### 3. 打包内容清单（当前规则：与历史包结构一致）

**包含：**

- `Plugins/BlueprintAIAssistant/`**（整个插件目录，含 `Binaries/`、`Source/`、`Config/`、`Intermediate/`、`.uplugin`）
- `docs/`**（所有文档）

**不包含：**

- 项目自身的 `Binaries/`、`DerivedDataCache/`、`Saved/`、`.vs/`、`Intermediate/`（项目级）
- `dist/`（打包输出目录本身）

---

### 4. PowerShell 一键打包命令（推荐）

在项目根目录执行（**建议每次生成新时间戳文件名，避免歧义**）：

```powershell
$ErrorActionPreference = 'Stop'
$proj  = 'D:\Workspace\UEProject\UECPP'
$ts = Get-Date -Format 'yyyyMMdd-HHmmss'
$outZip = "D:\Workspace\UEProject\UECPP\dist\BlueprintAIAssistant_prebuilt_root_$ts.zip"
$stage = 'D:\Workspace\UEProject\UECPP\dist\BlueprintAIAssistant_stage_prebuilt_root'

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'Plugins') | Out-Null

Copy-Item -Force -Recurse (Join-Path $proj 'docs\*') (Join-Path $stage 'docs')
Copy-Item -Force -Recurse (Join-Path $proj 'Plugins\BlueprintAIAssistant') (Join-Path $stage 'Plugins\BlueprintAIAssistant')

if (Test-Path $outZip) { Remove-Item -Force $outZip }
Compress-Archive -Path (Join-Path $stage 'docs'), (Join-Path $stage 'Plugins') -DestinationPath $outZip -CompressionLevel Optimal
```

如果你希望一键复用脚本：参考 `dist/package_prebuilt_root.ps1`（项目内置），默认会按当前时间戳生成新 zip 文件名。

---

### 5. 地编使用说明（解压即用）

1. 关闭 Unreal Editor
2. 把 zip 解压到**项目根目录**（与 `.uproject` 同级）
3. 覆盖提示选择“全部覆盖”
4. 重新打开项目与蓝图编辑器，确认 `Blueprint AI Assistant` 面板可打开

