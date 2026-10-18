param(
    [string]$BuildDirectory,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $sourceRoot 'build\xbox-uwp'
}
if (-not $OutputPath) {
    $OutputPath = Join-Path $BuildDirectory 'Warthog-Xbox-x64.appx'
}

$required = @(
    'Warthog.exe',
    'OpenCEUWP.winmd',
    'SDL2.dll',
    'libuwp.dll',
    'z-1.dll',
    'libEGL.dll',
    'libGLESv2.dll',
    'halo_guest.elf'
)
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory $name))) {
        throw "Missing build output: $name"
    }
}

$assets = Join-Path $BuildDirectory 'xbox-assets'
& powershell -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $sourceRoot 'tools\generate_xbox_assets.ps1') -OutputDirectory $assets

$sourceNotices = [ordered]@{
    'LICENSE' = 'LICENSE.txt'
    'README.md' = 'Warthog-README.md'
    'LICENSES\CC0-1.0.txt' = 'CC0-1.0.txt'
    'port\assets\fonts\Newtown-LICENSE.txt' = 'Newtown-LICENSE.txt'
    'port\assets\fonts\Overpass-OFL.txt' = 'Overpass-OFL.txt'
    'port\linux\ui\fonts\OFL.txt' = 'NotoSans-OFL.txt'
    'port\linux\ui\fonts\KENNEY-CC0.txt' = 'Kenney-Input-Prompts-CC0.txt'
    'port\third_party\expat\COPYING' = 'expat-COPYING.txt'
    'port\third_party\extract-xiso\LICENSE.TXT' = 'extract-xiso-LICENSE.txt'
    'port\third_party\kcp\LICENSE' = 'kcp-LICENSE.txt'
    'port\third_party\mbedtls\LICENSE' = 'mbedtls-LICENSE.txt'
    'port\third_party\miniupnpc\LICENSE' = 'miniupnpc-LICENSE.txt'
    'port\third_party\monocypher\LICENCE.md' = 'monocypher-LICENCE.md'
    'port\third_party\qrcodegen\LICENSE' = 'qrcodegen-LICENSE.txt'
    'port\third_party\smaa\LICENSE' = 'smaa-LICENSE.txt'
    'port\third_party\stb\LICENSE' = 'stb-LICENSE.txt'
    'port\third_party\tomlc17\LICENSE' = 'tomlc17-LICENSE.txt'
    'port\xboxone\third_party\imgui\LICENSE.txt' = 'imgui-LICENSE.txt'
}

$makeAppx = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') `
    -Recurse -Filter MakeAppx.exe |
    Where-Object FullName -match '\\x64\\MakeAppx.exe$' |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $makeAppx) {
    throw 'MakeAppx.exe not found.'
}

$stage = Join-Path $BuildDirectory ('.stage-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($name in $required) {
        Copy-Item -LiteralPath (Join-Path $BuildDirectory $name) -Destination $stage
    }

    Copy-Item -LiteralPath (Join-Path $sourceRoot 'port\xboxone\Package.appxmanifest') `
        -Destination (Join-Path $stage 'AppxManifest.xml')
    New-Item -ItemType Directory -Path (Join-Path $stage 'Assets') | Out-Null
    Copy-Item -Path (Join-Path $assets '*.png') -Destination (Join-Path $stage 'Assets')
    $warthogAssets = Join-Path $stage 'Warthog'
    New-Item -ItemType Directory -Path $warthogAssets | Out-Null
    Copy-Item -Path (Join-Path $sourceRoot 'port\assets\*') -Destination $warthogAssets -Recurse -Force
    foreach ($notice in $sourceNotices.Keys) {
        $noticePath = Join-Path $sourceRoot $notice
        if (Test-Path -LiteralPath $noticePath) {
            Copy-Item -LiteralPath $noticePath -Destination (Join-Path $stage $sourceNotices[$notice])
        }
    }

    if (Test-Path -LiteralPath $OutputPath) {
        Remove-Item -LiteralPath $OutputPath
    }
    & $makeAppx pack /d $stage /p $OutputPath /o
    if ($LASTEXITCODE) {
        throw "MakeAppx failed: $LASTEXITCODE"
    }
}
finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}

Write-Output "Created $OutputPath"
