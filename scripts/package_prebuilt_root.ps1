[CmdletBinding()]
param(
    [string]$ProjectRoot = "",
    [string]$OutZip = "",
    [switch]$BuildFirst,
    [switch]$IncludePdb
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir ".."))
}
$ProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot)

$PluginSrc = Join-Path $ProjectRoot "Plugins\BlueprintAIAssistant"
$DocsSrc = Join-Path $ProjectRoot "docs"
$ScriptsSrc = Join-Path $ProjectRoot "scripts"
$WorkflowSrc = Join-Path $ProjectRoot ".github\workflows\blueprint-ai-smoke.yml"
$UProject = Join-Path $ProjectRoot "UECPP.uproject"

if (-not (Test-Path $PluginSrc)) {
    throw "Plugin not found: $PluginSrc"
}

if ($BuildFirst) {
    $BuildScript = Join-Path $ScriptsSrc "build-uecpp.ps1"
    if (-not (Test-Path $BuildScript)) {
        throw "Build script not found: $BuildScript"
    }
    & $BuildScript -Target "UECPPEditor" -Platform "Win64" -Configuration "Development" -Project $UProject
}

$DllEditor = Join-Path $PluginSrc "Binaries\Win64\UnrealEditor-BlueprintAIAssistantEditor.dll"
$DllRuntime = Join-Path $PluginSrc "Binaries\Win64\UnrealEditor-BlueprintAIAssistantRuntime.dll"
if (-not (Test-Path $DllEditor) -or -not (Test-Path $DllRuntime)) {
    throw "Prebuilt DLL missing. Run with -BuildFirst or compile once. Expected: $DllEditor"
}

$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutZip)) {
    $OutZip = Join-Path $ProjectRoot ("dist\BlueprintAIAssistant_prebuilt_root_$Stamp.zip")
}
$OutZip = [System.IO.Path]::GetFullPath($OutZip)
$OutDir = Split-Path -Parent $OutZip
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$Stage = Join-Path $OutDir "BlueprintAIAssistant_stage_prebuilt_root_$Stamp"
if (Test-Path $Stage) {
    Remove-Item -Recurse -Force $Stage
}
New-Item -ItemType Directory -Force -Path $Stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "Plugins") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "docs") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "scripts") | Out-Null

$StagePlugin = Join-Path $Stage "Plugins\BlueprintAIAssistant"
$ExcludePluginDirs = @("Intermediate", ".vs", ".git")
$RoboArgs = @(
    $PluginSrc,
    $StagePlugin,
    "/E", "/NFL", "/NDL", "/NJH", "/NJS", "/NC", "/NS"
)
foreach ($d in $ExcludePluginDirs) {
    $RoboArgs += "/XD"
    $RoboArgs += $d
}
& robocopy @RoboArgs | Out-Null
if ($LASTEXITCODE -ge 8) {
    throw "robocopy plugin failed: $LASTEXITCODE"
}

if (-not $IncludePdb) {
    Get-ChildItem -Path $StagePlugin -Recurse -Filter "*.pdb" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item -Force $_.FullName }
}

$DocPatterns = @("BlueprintAIAssistant_*.md", "README.md")
foreach ($pat in $DocPatterns) {
    Get-ChildItem -Path $DocsSrc -Filter $pat -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item -Force $_.FullName -Destination (Join-Path $Stage "docs") }
}

$ScriptNames = @(
    "build-uecpp.ps1",
    "run-blueprint-ai-smoke.ps1",
    "package-blueprint-ai-assistant.ps1"
)
foreach ($name in $ScriptNames) {
    $src = Join-Path $ScriptsSrc $name
    if (Test-Path $src) {
        Copy-Item -Force $src -Destination (Join-Path $Stage "scripts")
    }
}
Copy-Item -Force $PSCommandPath -Destination (Join-Path $Stage "scripts\package_prebuilt_root.ps1")

if (Test-Path $WorkflowSrc) {
    $wfDir = Join-Path $Stage ".github\workflows"
    New-Item -ItemType Directory -Force -Path $wfDir | Out-Null
    Copy-Item -Force $WorkflowSrc -Destination (Join-Path $wfDir "blueprint-ai-smoke.yml")
}

$VersionName = "0.1.0"
$EngineVer = "5.6"
try {
    $uplugin = Get-Content (Join-Path $PluginSrc "BlueprintAIAssistant.uplugin") -Raw | ConvertFrom-Json
    if ($uplugin.VersionName) { $VersionName = $uplugin.VersionName }
    if ($uplugin.EngineVersion) { $EngineVer = $uplugin.EngineVersion }
} catch { }

$InstallLines = @(
    "# Blueprint AI Assistant - INSTALL",
    "",
    "Version: $VersionName | Engine: UE $EngineVer | Platform: Win64 Editor",
    "Packaged: $Stamp",
    "",
    "## Level designers",
    "1. Close Unreal Editor.",
    "2. Unzip to project root (same folder as .uproject). Overwrite all.",
    "3. Open project, enable plugin, restart editor.",
    "4. Project Settings - Plugins - Blueprint AI Assistant: API key.",
    "5. Window - Blueprint AI Assistant.",
    "",
    "See Plugins/BlueprintAIAssistant/DISTRIBUTION.md",
    "See docs/BlueprintAIAssistant_LDGuide.md",
    "",
    "## Programmers",
    "- Source: Plugins/BlueprintAIAssistant/Source/",
    "- Tests: Plugins/BlueprintAIAssistant/Tests/SmokeDsl/",
    "- Build: scripts/build-uecpp.ps1",
    "- Smoke: scripts/run-blueprint-ai-smoke.ps1 -BuildFirst"
)
Set-Content -Path (Join-Path $Stage "INSTALL.md") -Value ($InstallLines -join "`n") -Encoding UTF8

if (Test-Path $OutZip) {
    Remove-Item -Force $OutZip
}

$ZipItems = @(
    (Join-Path $Stage "INSTALL.md"),
    (Join-Path $Stage "Plugins"),
    (Join-Path $Stage "docs"),
    (Join-Path $Stage "scripts")
)
if (Test-Path (Join-Path $Stage ".github")) {
    $ZipItems += (Join-Path $Stage ".github")
}
Compress-Archive -Path $ZipItems -DestinationPath $OutZip -CompressionLevel Optimal

$sizeMb = [math]::Round((Get-Item $OutZip).Length / 1MB, 2)
Remove-Item -Recurse -Force $Stage

Write-Host ""
Write-Host "=== Blueprint AI Assistant prebuilt_root package ==="
Write-Host "Output:   $OutZip"
Write-Host "Size MB:  $sizeMb"
Write-Host "Version:  $VersionName (UE $EngineVer)"
Write-Host "DLLs:     included"
Write-Host "PDB:      $(if ($IncludePdb) { 'included' } else { 'excluded' })"
Write-Host ""
Write-Host "Unzip to project root (same level as .uproject), then read INSTALL.md"
Write-Host ""

[PSCustomObject]@{
    ZipPath = $OutZip
    SizeMB  = $sizeMb
    Version = $VersionName
    Engine  = $EngineVer
    BuiltAt = $Stamp
}
