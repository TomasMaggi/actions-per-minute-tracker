#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Version,
    [string]$BinDir,
    [string]$OutputDir,
    [string]$ProductCode
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $versionFile = Join-Path $root 'VERSION'
    if (Test-Path -LiteralPath $versionFile) {
        $Version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    }
}
if (-not $Version) { $Version = '0.0.0' }

# MSI ProductVersion must be numeric major.minor.patch; drop any +metadata.
$msiVersion = ($Version -split '\+')[0]

if (-not $BinDir) { $BinDir = Join-Path $root 'Release\win64' }
if (-not $OutputDir) { $OutputDir = $BinDir }
if (-not $ProductCode) { $ProductCode = [guid]::NewGuid().ToString().ToUpper() }

$exe = Join-Path $BinDir 'actions-per-minute-tracker.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Executable not found at $exe. Build the app first (.\build.ps1)."
}

function Resolve-Wix {
    $cmd = Get-Command wix -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $fallback = Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe'
    if (Test-Path -LiteralPath $fallback) { return $fallback }
    throw "wix not found. Install it with: dotnet tool install --global wix --version 5.*"
}

$wix = Resolve-Wix
& $wix extension add -g WixToolset.UI.wixext/5.0.2
if ($LASTEXITCODE -ne 0) { throw "Failed to add the WiX UI extension" }

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$msiPath = Join-Path $OutputDir ("apm-tracker-{0}.msi" -f $Version)

$arguments = @(
    'build',
    (Join-Path $PSScriptRoot 'apm-tracker.wxs'),
    '-arch', 'x64',
    '-ext', 'WixToolset.UI.wixext',
    '-o', $msiPath,
    '-d', "Version=$msiVersion",
    '-d', "ProductCode=$ProductCode",
    '-d', "BinDir=$BinDir",
    '-d', "SourceDir=$root",
    '-d', "LicenseRtf=$(Join-Path $PSScriptRoot 'license.rtf')"
)

Write-Host "Building MSI with $wix (version $msiVersion)..."
& $wix @arguments
if ($LASTEXITCODE -ne 0) { throw "wix build failed with exit code $LASTEXITCODE" }

Write-Host "Built: $msiPath"
