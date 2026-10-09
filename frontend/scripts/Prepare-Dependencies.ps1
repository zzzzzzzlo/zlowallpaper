param(
    [Parameter(Mandatory = $true)][string]$MpvPackage,
    [Parameter(Mandatory = $true)][string]$FfmpegExecutable,
    [Parameter(Mandatory = $true)][string]$IcontraRuntime,
    [Parameter(Mandatory = $true)][string]$TranslucentRuntime,
    [Parameter(Mandatory = $true)][string]$AsarCli,
    [string]$QtImageFormatsPackage
)
$ErrorActionPreference = 'Stop'
$frontend = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $frontend 'deps'
# Inputs are extracted, trusted x64 runtime packages, not arbitrary latest downloads.
# See docs/RUNTIME-NOTICES.md for this delivery's provenance and fingerprints.
$checks = @(
    (Join-Path $MpvPackage 'include/mpv/client.h'),
    (Join-Path $MpvPackage 'libmpv.dll.a'),
    (Join-Path $MpvPackage 'libmpv-2.dll'),
    $FfmpegExecutable,
    (Join-Path $IcontraRuntime 'Icontra.exe'),
    (Join-Path $TranslucentRuntime 'TranslucentTB.exe'),
    $AsarCli
)
foreach ($item in $checks) { if (!(Test-Path -LiteralPath $item -PathType Leaf)) { throw "Missing dependency: $item" } }
New-Item -ItemType Directory -Force -Path "$deps/mpv/package", "$deps/ffmpeg/package", "$deps/icontra", "$deps/translucenttb" | Out-Null
Copy-Item -LiteralPath (Join-Path $MpvPackage 'include') -Destination "$deps/mpv/package" -Recurse -Force
Copy-Item -LiteralPath (Join-Path $MpvPackage 'libmpv.dll.a'), (Join-Path $MpvPackage 'libmpv-2.dll') -Destination "$deps/mpv/package" -Force
Copy-Item -LiteralPath $FfmpegExecutable -Destination "$deps/ffmpeg/package/ffmpeg.exe" -Force
Get-ChildItem -LiteralPath $IcontraRuntime | Copy-Item -Destination "$deps/icontra" -Recurse -Force
Get-ChildItem -LiteralPath $TranslucentRuntime | Copy-Item -Destination "$deps/translucenttb" -Recurse -Force
if ($QtImageFormatsPackage) {
    New-Item -ItemType Directory -Force -Path "$deps/qt-imageformats" | Out-Null
    Get-ChildItem -LiteralPath $QtImageFormatsPackage -Force | Copy-Item -Destination "$deps/qt-imageformats" -Recurse -Force
}
# Repack source from this repository, including the independent Dock profile patch.
& node $AsarCli pack "$frontend/vendor/icontra" "$deps/icontra/resources/app.asar"
if ($LASTEXITCODE -ne 0) { throw 'Dock asar packaging failed' }
Write-Host "Dependencies prepared in $deps. Continue with scripts/Build.ps1."
