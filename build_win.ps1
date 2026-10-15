# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  build_win.ps1  -  Windows (MSVC x64) build + optional install into OBS.
#
#  Usage (from a "x64 Native Tools" or any PowerShell with VS 2022 installed):
#    .\build_win.ps1 -QtDir "C:\Qt\6.6.3\msvc2019_64" `
#                    -ObsDir "C:\Program Files\obs-studio" `
#                    [-Standard] [-Install] [-RunTests]
#
#  -Standard : build the Standard edition (MODEL600_PRO_EDITION=OFF).
#  -Install  : copy the built DLL into the OBS 64bit plugin folder.
#  -RunTests : build and run the headless Layer-1/2 self-tests.
# =============================================================================
param(
    [string]$QtDir   = $env:Qt6_DIR,
    [string]$ObsDir  = "C:\Program Files\obs-studio",
    [string]$ObsSdk  = "",            # Optional: path to an OBS SDK (headers+libs).
    [switch]$Standard,
    [switch]$Install,
    [switch]$RunTests,
    [string]$Config  = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"

Write-Host "================================================================"
Write-Host " MODEL 600 // BROADCAST SUITE  -  Windows build (MSVC x64)"
Write-Host "================================================================"

if (-not $QtDir -or -not (Test-Path $QtDir)) {
    throw "Qt6 directory not found. Pass -QtDir, e.g. C:\Qt\6.6.3\msvc2019_64"
}

$pro = if ($Standard) { "OFF" } else { "ON" }
$prefixPaths = @($QtDir)
if ($ObsSdk)        { $prefixPaths += $ObsSdk }
if (Test-Path $ObsDir) { $prefixPaths += $ObsDir }
$prefix = ($prefixPaths -join ";")

$tests = if ($RunTests) { "ON" } else { "OFF" }

Write-Host "Configuring (Pro=$pro, Config=$Config)..."
cmake -S $root -B $build -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=$prefix" `
    "-DMODEL600_PRO_EDITION=$pro" `
    "-DMODEL600_BUILD_TESTS=$tests"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

Write-Host "Building..."
cmake --build $build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

if ($RunTests) {
    Write-Host "Running tests..."
    ctest --test-dir $build -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
}

$dll = Join-Path $build "$Config\model600.dll"
if (-not (Test-Path $dll)) { $dll = Join-Path $build "model600.dll" }
if (-not (Test-Path $dll)) { throw "Built DLL not found." }
Write-Host "Built: $dll"

if ($Install) {
    $dest = Join-Path $ObsDir "obs-plugins\64bit"
    if (-not (Test-Path $dest)) { throw "OBS plugin folder not found: $dest" }
    Write-Host "Installing to $dest ..."
    Copy-Item $dll $dest -Force
    Write-Host "Installed. Restart OBS and add the MODEL 600 filters."
}

Write-Host "DONE."
