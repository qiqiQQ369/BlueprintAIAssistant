[CmdletBinding()]
param(
    [switch]$BuildFirst,
    [string]$EngineRoot = "",
    [string]$Project = "",
    [string]$Map = "/Engine/Maps/Entry",
    [string]$TestFilter = "BlueprintAIAssistant.Smoke",
    [int]$TimeoutSeconds = 900
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if ([string]::IsNullOrWhiteSpace($EngineRoot)) {
    if ($env:UE_ENGINE_ROOT) { $EngineRoot = $env:UE_ENGINE_ROOT }
    elseif ($env:UE_ROOT) { $EngineRoot = $env:UE_ROOT }
    else { $EngineRoot = "C:\Program Files\Epic Games\UE_5.6" }
}
if ([string]::IsNullOrWhiteSpace($Project)) {
    $Project = Join-Path $ScriptRoot "..\UECPP.uproject"
}
$ResolvedProject = [System.IO.Path]::GetFullPath($Project)

if ($BuildFirst) {
    & (Join-Path $ScriptRoot "build-uecpp.ps1") -Target "UECPPEditor" -Platform "Win64" -Configuration "Development" -Project $ResolvedProject
}

$EditorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path $EditorCmd)) {
    throw "UnrealEditor-Cmd.exe not found: $EditorCmd"
}
if (-not (Test-Path $ResolvedProject)) {
    throw "Project file not found: $ResolvedProject"
}

$OutDir = Join-Path (Split-Path -Parent $ResolvedProject) "Saved\BlueprintAIAssistant\smoke"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$LogPath = Join-Path $OutDir "smoke-$Stamp.log"
$SummaryPath = Join-Path $OutDir "smoke-summary-$Stamp.md"

Write-Host "EngineRoot: $EngineRoot"
Write-Host "Project:    $ResolvedProject"
Write-Host "OutDir:     $OutDir"
Write-Host "Log:        $LogPath"
Write-Host "Filter:     $TestFilter"

# Notes:
# - UE automation exit codes vary between versions/configs, so this script also parses the report/log.
# - TestExit waits until the automation queue is empty before exiting.
$ExecCmds = "Automation RunTests $TestFilter"
$ReportDir = Join-Path $OutDir "Report-$Stamp"

$Args = @(
    "`"$ResolvedProject`"",
    $Map,
    "-unattended",
    "-nop4",
    "-nosplash",
    "-nullrhi",
    "-RenderOffScreen",
    "-NoSound",
    "-stdout",
    "-log=`"$LogPath`"",
    "-ExecCmds=`"$ExecCmds`"",
    "-TestExit=`"Automation Test Queue Empty`"",
    "-ReportOutputPath=`"$ReportDir`""
)

$timedOut = $false
$p = Start-Process -FilePath $EditorCmd -ArgumentList $Args -PassThru
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    try { $p.Refresh() } catch {}
}
if (-not $p.HasExited) {
    Write-Error "Smoke timed out after $TimeoutSeconds seconds. Killing UnrealEditor-Cmd..."
    try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
    $timedOut = $true
}

$logText = ""
if (Test-Path $LogPath) {
    $logText = Get-Content -Raw -Path $LogPath
}

$failed = $false
$hasReport = $false
$failMatches = @()
$passMatches = @()
$reportJsonPath = Join-Path $ReportDir "index.json"
$reportSucceeded = 0
$reportFailed = 0
$reportNotRun = 0
$reportInProcess = 0

if ($logText) {
    $failMatches = Select-String -InputObject $logText -Pattern "Automation Test Failed" -AllMatches
    $passMatches = Select-String -InputObject $logText -Pattern "Automation Test Succeeded|Automation Test Passed" -AllMatches
    if ($failMatches.Count -gt 0) { $failed = $true }
}
if (Test-Path $reportJsonPath) {
    $hasReport = $true
    try {
        $report = Get-Content -Raw -Path $reportJsonPath | ConvertFrom-Json
        $reportSucceeded = [int]$report.succeeded
        $reportFailed = [int]$report.failed
        $reportNotRun = [int]$report.notRun
        $reportInProcess = [int]$report.inProcess
        if ($reportFailed -gt 0 -or $reportNotRun -gt 0 -or $reportInProcess -gt 0) {
            $failed = $true
        }
    } catch {
        $failed = $true
    }
} else {
    $failed = $true
}
$failed = $failed -or $timedOut

$md = @()
$md += "# BlueprintAIAssistant Headless Smoke Summary"
$md += ""
$md += "- time: $(Get-Date -Format o)"
$md += "- project: $ResolvedProject"
$md += "- editorCmd: $EditorCmd"
$md += "- filter: $TestFilter"
$md += "- processExitCode: $($p.ExitCode)"
$md += "- log: $LogPath"
$md += "- reportDir: $ReportDir"
$md += ""

$md += "## Result"
$md += ""
if ($failed) {
    $md += "**FAIL**"
} else {
    $md += "**PASS**"
}
$md += ""
$md += "## Parsed"
$md += ""
$md += "- hasLog: $([bool]$logText)"
$md += "- hasReport: $hasReport"
$md += "- failedMatches: $($failMatches.Count)"
$md += "- passedMatches: $($passMatches.Count)"
$md += "- reportSucceeded: $reportSucceeded"
$md += "- reportFailed: $reportFailed"
$md += "- reportNotRun: $reportNotRun"
$md += "- reportInProcess: $reportInProcess"
$md += ""

if ($failed -and $logText) {
    $md += "## Failures (tail excerpts)"
    $md += ""
    $tail = $logText.Split([Environment]::NewLine) | Select-Object -Last 120
    $md += '```'
    $md += ($tail -join [Environment]::NewLine)
    $md += '```'
}

$md -join [Environment]::NewLine | Set-Content -Encoding UTF8 -Path $SummaryPath
Write-Host "Summary written: $SummaryPath"

if ($failed) {
    Write-Error "Smoke FAILED. See: $SummaryPath"
    exit 1
}

Write-Host "Smoke PASSED."
exit 0

