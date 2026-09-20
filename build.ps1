#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')]
    [string]$Configuration = 'Release',
    [string]$OutputDir,
    [string]$Version,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'

if (-not $OutputDir) {
    $OutputDir = Join-Path $PSScriptRoot 'Release\win64'
}

if (-not $Version) {
    $versionFile = Join-Path $PSScriptRoot 'VERSION'
    if (Test-Path -LiteralPath $versionFile) {
        $Version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    }
}
if (-not $Version) {
    $Version = '0.0.0'
}

function Resolve-Clangxx {
    $cmd = Get-Command clang++ -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $fallback = Join-Path $env:ProgramFiles 'LLVM\bin\clang++.exe'
    if (Test-Path -LiteralPath $fallback) { return $fallback }
    throw "clang++ not found. Install LLVM or add it to PATH."
}

$clangxx = Resolve-Clangxx

$commonFlags = @('-std=c++17', '-DUNICODE', '-D_UNICODE')
if ($Configuration -eq 'Release') {
    $commonFlags += @('-O2', '-DNDEBUG')
} else {
    $commonFlags += @('-O0', '-g', '-D_DEBUG')
}

if ($Test) {
    $testDir = Join-Path $PSScriptRoot 'Release\tests'
    if (-not (Test-Path -LiteralPath $testDir)) {
        New-Item -ItemType Directory -Path $testDir | Out-Null
    }

    $testExe = Join-Path $testDir 'counter_tests.exe'
    $testSources = @(
        (Join-Path $PSScriptRoot 'counter.cpp'),
        (Join-Path $PSScriptRoot 'tests\counter_tests.cpp')
    )

    Write-Host "Building tests with $clangxx ($Configuration)..."
    & $clangxx @commonFlags -o $testExe @testSources
    if ($LASTEXITCODE -ne 0) { throw "Test build failed with exit code $LASTEXITCODE" }

    Write-Host "Running tests..."
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE" }
}

$sources = @(
    (Join-Path $PSScriptRoot 'main.cpp'),
    (Join-Path $PSScriptRoot 'counter.cpp'),
    (Join-Path $PSScriptRoot 'settings.cpp'),
    (Join-Path $PSScriptRoot 'session.cpp'),
    (Join-Path $PSScriptRoot 'log.cpp')
)

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$exePath = Join-Path $OutputDir 'actions-per-minute-tracker.exe'

$arguments = $commonFlags + @('-o', $exePath)
$arguments += ('-DAPP_VERSION=' + $Version)
$arguments += $sources
$arguments += @('-luser32', '-lgdi32', '-lxmllite', '-lole32', '-lshell32')
$arguments += @('-Wl,/subsystem:windows', '-Wl,/entry:mainCRTStartup')

Write-Host "Compiling with $clangxx ($Configuration), version $Version..."
& $clangxx @arguments
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

Write-Host "Built: $exePath"
