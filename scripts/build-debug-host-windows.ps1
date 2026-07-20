param(
    [string] $BuildDir = "",
    [string] $Config = "Release",
    [string] $Generator = "Visual Studio 18 2026",
    [string] $Architecture = "x64"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildScript = Join-Path $ScriptDir "build-windows.ps1"

& $buildScript `
    -BuildDir $BuildDir `
    -Config $Config `
    -Generator $Generator `
    -Architecture $Architecture `
    -Target "debug_host"

$RootDir = Resolve-Path (Join-Path $ScriptDir "..")
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RootDir "build-windows"
}

$debugHostPath = Join-Path $BuildDir "debug_host_artefacts\$Config\Ohmsick Debug Host.exe"
if (Test-Path $debugHostPath) {
    Write-Host "Built debug host: $debugHostPath"
}
