[CmdletBinding()]
param(
    # 输出目录（默认：项目 Saved/BlueprintAIAssistant/releases）
    [string]$OutputDir = "",
    # 打包前先用 build-uecpp.ps1 编译一次当前工程（验证插件可编过）
    [switch]$BuildFirst,
    # 额外包含 Tests/SmokeDsl 等测试资源（地编日常不需要，默认不包含）
    [switch]$IncludeTests,
    # 额外包含本机已编译的 Binaries（仅在与接收方引擎版本完全一致时使用）
    [switch]$IncludeBinaries,
    # 项目 .uproject 路径（默认仓库根目录 UECPP.uproject）
    [string]$Project = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptRoot ".."))
$PluginRoot = Join-Path $RepoRoot "Plugins\BlueprintAIAssistant"

if ([string]::IsNullOrWhiteSpace($Project)) {
    $Project = Join-Path $RepoRoot "UECPP.uproject"
}
$ResolvedProject = [System.IO.Path]::GetFullPath($Project)

if (-not (Test-Path $PluginRoot)) {
    throw "Plugin folder not found: $PluginRoot"
}

if ($BuildFirst) {
    & (Join-Path $ScriptRoot "build-uecpp.ps1") -Target "UECPPEditor" -Platform "Win64" -Configuration "Development" -Project $ResolvedProject
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $RepoRoot "Saved\BlueprintAIAssistant\releases"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$ZipName = "BlueprintAIAssistant-UE56-source-$Stamp.zip"
if ($IncludeBinaries) {
    $ZipName = "BlueprintAIAssistant-UE56-with-binaries-$Stamp.zip"
}
$ZipPath = Join-Path $OutputDir $ZipName

$Staging = Join-Path $env:TEMP "BlueprintAIAssistant-package-$Stamp"
if (Test-Path $Staging) {
    Remove-Item -Recurse -Force $Staging
}
$StagePlugin = Join-Path $Staging "BlueprintAIAssistant"
New-Item -ItemType Directory -Force -Path $StagePlugin | Out-Null

# 与 .gitignore 思路一致：地编分发通常只要 Source + uplugin + 说明，避免把本机中间文件打进去
$ExcludeDirs = @(
    "Binaries",
    "Intermediate",
    ".vs"
)
if (-not $IncludeTests) {
    $ExcludeDirs += "Tests"
}

# robocopy: 复制 Source、Config（若存在）、uplugin、DISTRIBUTION.md 等；排除目录
$RoboArgs = @(
    $PluginRoot,
    $StagePlugin,
    "/E",
    "/NFL", "/NDL", "/NJH", "/NJS", "/NC", "/NS"
)
foreach ($d in $ExcludeDirs) {
    $RoboArgs += "/XD"
    $RoboArgs += $d
}

& robocopy @RoboArgs
$RoboExit = $LASTEXITCODE
# robocopy: 0-7 均视为成功（有文件复制时常为 1）
if ($RoboExit -ge 8) {
    throw "robocopy failed with exit code: $RoboExit"
}

# 明确补拷根目录单文件（robocopy 已覆盖；双保险）
Copy-Item -Force (Join-Path $PluginRoot "BlueprintAIAssistant.uplugin") -Destination $StagePlugin

if ($IncludeBinaries) {
    $BinFrom = Join-Path $PluginRoot "Binaries"
    if (Test-Path $BinFrom) {
        Copy-Item -Recurse -Force $BinFrom -Destination (Join-Path $StagePlugin "Binaries")
    } else {
        Write-Warning "-IncludeBinaries set but folder missing: $BinFrom (build the plugin locally first)"
    }
}

# 删除误拷的临时/日志（若存在）
Get-ChildItem -Path $StagePlugin -Recurse -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq "__pycache__" -or $_.Name -eq ".git" } |
    ForEach-Object { Remove-Item -Recurse -Force $_.FullName -ErrorAction SilentlyContinue }

if (-not (Test-Path (Join-Path $StagePlugin "DISTRIBUTION.md"))) {
    Write-Warning "DISTRIBUTION.md missing under plugin folder."
}

if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}
Compress-Archive -Path $StagePlugin -DestinationPath $ZipPath -CompressionLevel Optimal

Remove-Item -Recurse -Force $Staging

$sizeMb = [math]::Round((Get-Item $ZipPath).Length / 1MB, 2)
$ModeMsg = if ($IncludeBinaries) { "source + Binaries" } else { "source only (recipients build locally)" }
$TestsMsg = if ($IncludeTests) { "included" } else { "excluded (use -IncludeTests to add)" }
Write-Host ""
Write-Host "=== Blueprint AI Assistant package done ==="
Write-Host "Output: $ZipPath"
Write-Host "Size MB: $sizeMb"
Write-Host "Mode: $ModeMsg"
Write-Host "Tests: $TestsMsg"
Write-Host "Read: BlueprintAIAssistant/DISTRIBUTION.md inside the zip"
Write-Host ""
