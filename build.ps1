#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')]
    [string]$Configuration = 'Release',
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'

if (-not $OutputDir) {
    $OutputDir = Join-Path $PSScriptRoot 'Release\win64'
}

function Resolve-Clangxx {
    $cmd = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $fallback = Join-Path $env:ProgramFiles 'LLVM\bin\clang++.exe'
    if (Test-Path -LiteralPath $fallback) { return $fallback }
    throw "clang++ not found. Install LLVM or add it to PATH."
}

$clangxx = Resolve-Clangxx
$sources = @(
    (Join-Path $PSScriptRoot 'main.cpp'),
    (Join-Path $PSScriptRoot 'counter.cpp')
)

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$exePath = Join-Path $OutputDir 'actions-per-minute-tracker.exe'

$arguments = @('-std=c++17', '-DUNICODE', '-D_UNICODE', '-o', $exePath)
if ($Configuration -eq 'Release') {
    $arguments += @('-O2', '-DNDEBUG')
} else {
    $arguments += @('-O0', '-g', '-D_DEBUG')
}
$arguments += $sources
$arguments += @('-luser32', '-lgdi32')
$arguments += @('-Wl,/subsystem:windows', '-Wl,/entry:mainCRTStartup')

Write-Host "Compiling with $clangxx ($Configuration)..."
& $clangxx @arguments
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

Write-Host "Built: $exePath"
