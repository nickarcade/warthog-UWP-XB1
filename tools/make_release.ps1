param(
    [string]$OutputDirectory,
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'build\xbox-test-build' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$package = Join-Path $OutputDirectory 'Warthog-UWP-x64.appx'
& (Join-Path $PSScriptRoot 'package_xbox.ps1') -OutputPath $package
if ($LASTEXITCODE) { throw 'Package creation failed.' }
& (Join-Path $PSScriptRoot 'sign_xbox.ps1') -PackagePath $package `
    -CertificatePath (Join-Path $OutputDirectory 'Warthog-UWP.cer')
if ($LASTEXITCODE) { throw 'Package signing failed.' }

$pdb = Join-Path $root 'build\xbox-uwp\Warthog.pdb'
if (Test-Path -LiteralPath $pdb) {
    Copy-Item -LiteralPath $pdb -Destination (Join-Path $OutputDirectory 'Warthog-UWP.pdb') -Force
}

$vclibsFlavor = if ($Configuration -eq 'Debug') { 'Debug' } else { 'Retail' }
$dependencySource = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\$vclibsFlavor\x64"
if (-not (Test-Path -LiteralPath $dependencySource)) {
    $dependencySource = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64'
}
if (Test-Path -LiteralPath $dependencySource) {
    $dependencyOutput = Join-Path $OutputDirectory 'Dependencies\x64'
    New-Item -ItemType Directory -Path $dependencyOutput -Force | Out-Null
    Copy-Item -Path (Join-Path $dependencySource '*.appx') -Destination $dependencyOutput -Force
}

Write-Output "Release directory ready: $OutputDirectory"
