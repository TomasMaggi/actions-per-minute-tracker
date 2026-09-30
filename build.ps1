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

function Resolve-Rc {
    $cmd = Get-Command llvm-rc -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $dirs = @()
    if ($clangxx) { $dirs += (Split-Path -Parent $clangxx) }
    $dirs += (Join-Path $env:ProgramFiles 'LLVM\bin')
    foreach ($dir in $dirs) {
        $candidate = Join-Path $dir 'llvm-rc.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $sdk = Get-Command rc.exe -ErrorAction SilentlyContinue
    if ($sdk) { return $sdk.Source }
    return $null
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
        (Join-Path $PSScriptRoot 'rec.cpp'),
        (Join-Path $PSScriptRoot 'eapm.cpp'),
        (Join-Path $PSScriptRoot 'third_party\puff.cpp'),
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
    (Join-Path $PSScriptRoot 'log.cpp'),
    (Join-Path $PSScriptRoot 'rec.cpp'),
    (Join-Path $PSScriptRoot 'eapm.cpp'),
    (Join-Path $PSScriptRoot 'third_party\puff.cpp')
)

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$exePath = Join-Path $OutputDir 'actions-per-minute-tracker.exe'

# Embed the application icon declared in app.rc. Skipped (with a warning) when
# the .ico is absent so the sources still build without it.
$iconPath = Join-Path $PSScriptRoot 'actions-per-minute-tracker.ico'
$resourceArgs = @()
if (Test-Path -LiteralPath $iconPath) {
    $rc = Resolve-Rc
    if (-not $rc) {
        throw "No resource compiler found. Install LLVM (llvm-rc) or the Windows SDK (rc.exe)."
    }
    $resPath = Join-Path $OutputDir 'app.res'
    Write-Host "Compiling icon resource with $rc..."
    & $rc /fo $resPath (Join-Path $PSScriptRoot 'app.rc')
    if ($LASTEXITCODE -ne 0) { throw "Resource compile failed with exit code $LASTEXITCODE" }
    $resourceArgs += $resPath
}
else {
    Write-Warning "actions-per-minute-tracker.ico not found; building without an icon."
}

$arguments = $commonFlags + @('-o', $exePath)
$arguments += ('-DAPP_VERSION=' + $Version)
$arguments += $sources
$arguments += $resourceArgs
$arguments += @('-luser32', '-lgdi32', '-lxmllite', '-lole32', '-lshell32')
$arguments += @('-Wl,/subsystem:windows', '-Wl,/entry:mainCRTStartup')

Write-Host "Compiling with $clangxx ($Configuration), version $Version..."
& $clangxx @arguments
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

Write-Host "Built: $exePath"
