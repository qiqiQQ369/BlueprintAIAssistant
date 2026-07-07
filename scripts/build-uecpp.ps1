[CmdletBinding()]
param(
    [string]$Target = "UECPPEditor",
    [string]$Platform = "Win64",
    [string]$Configuration = "Development",
    [string]$Project = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$EngineRoot = "C:\Program Files\Epic Games\UE_5.6"
$BuildBat = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
if ([string]::IsNullOrWhiteSpace($Project)) {
    $Project = Join-Path $ScriptRoot "..\\UECPP.uproject"
}
$ResolvedProject = [System.IO.Path]::GetFullPath($Project)

if (-not (Test-Path $BuildBat)) {
    throw "未找到 Build.bat：$BuildBat"
}

if (-not (Test-Path $ResolvedProject)) {
    throw "未找到项目文件：$ResolvedProject"
}

Write-Host "Using EngineRoot: $EngineRoot"
Write-Host "Building Target: $Target $Platform $Configuration"
Write-Host "Project: $ResolvedProject"

& $BuildBat $Target $Platform $Configuration $ResolvedProject
$ExitCode = $LASTEXITCODE

if ($ExitCode -ne 0) {
    throw "构建失败，退出码：$ExitCode"
}

Write-Host "Build succeeded."
