param(
    [string] $BuildDir = "",
    [string] $Config = "Release",
    [string] $Generator = "Visual Studio 18 2026",
    [string] $Architecture = "x64",
    [string] $Target = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path (Join-Path $ScriptDir "..")

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $RootDir "build-windows"
}

$configureArgs = @(
    "-S", $RootDir,
    "-B", $BuildDir,
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
)

if (-not [string]::IsNullOrWhiteSpace($Generator)) {
    $configureArgs += @("-G", $Generator)
}

if (-not [string]::IsNullOrWhiteSpace($Architecture)) {
    $configureArgs += @("-A", $Architecture)
}

cmake @configureArgs

$buildArgs = @("--build", $BuildDir, "--config", $Config)
if (-not [string]::IsNullOrWhiteSpace($Target)) {
    $buildArgs += @("--target", $Target)
}

cmake @buildArgs

$vst3Path = Join-Path $BuildDir "vst3arduinothing_vst3_artefacts\$Config\VST3\VST3 Arduino Thing.vst3"
if (Test-Path $vst3Path) {
    Write-Host "Built VST3: $vst3Path"
}
