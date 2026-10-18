param(
    [string]$UwpDeps = 'C:\bsuwp\_deps\uwpdep-src',
    [string]$AngleDir,
    [int]$Jobs = 12,
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = $root
if (-not $AngleDir) {
    $AngleDir = Join-Path $source 'port\xboxone\third_party\angle'
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'configure.py'))) {
    throw 'Warthog source was not found.'
}
if (-not (Test-Path -LiteralPath $UwpDeps)) {
    throw "UWP dependency directory was not found: $UwpDeps"
}

Push-Location $source
$previousHaloVersion = $env:HALO_VERSION
$env:HALO_VERSION = (Get-Content -LiteralPath (Join-Path $source 'VERSION') -Raw).Trim()
try {
    $guestConfigArgs = if ($Configuration -eq 'Debug') { @('--lto=off', '--pgo=off') } else { @('--release', '--pgo=off') }
    & py -3 configure.py @guestConfigArgs
    if ($LASTEXITCODE) { throw 'Guest configuration failed.' }
    & ninja uwp_guest
    if ($LASTEXITCODE) { throw 'Guest build failed.' }
} finally {
    if ($null -eq $previousHaloVersion) {
        Remove-Item Env:HALO_VERSION -ErrorAction SilentlyContinue
    } else {
        $env:HALO_VERSION = $previousHaloVersion
    }
    Pop-Location
}

$cmakeBuildType = if ($Configuration -eq 'Debug') { 'RelWithDebInfo' } else { 'Release' }
$cmakeDebugDef = if ($Configuration -eq 'Debug') { '-DHALO_DEBUG=1' } else { '-DHALO_DEBUG=0' }
$msvc = Join-Path $PSScriptRoot 'with-msvc.ps1'
$angleArg = if (Test-Path -LiteralPath $AngleDir) { "-DANGLE_DIR=$AngleDir" } else { "-DANGLE_DIR=C:\bsuwp\angle" }
& $msvc cmake --fresh -S (Join-Path $source 'port\xboxone') `
    -B (Join-Path $source 'build\xbox-uwp') -G Ninja `
    "-DCMAKE_BUILD_TYPE=$cmakeBuildType" $cmakeDebugDef -DCMAKE_C_COMPILER=clang-cl `
    -DCMAKE_CXX_COMPILER=clang-cl "-DUWP_DEPS=$UwpDeps" $angleArg
if ($LASTEXITCODE) { throw 'Host configuration failed.' }
& $msvc cmake --build (Join-Path $source 'build\xbox-uwp') -j $Jobs
if ($LASTEXITCODE) { throw 'Host build failed.' }
