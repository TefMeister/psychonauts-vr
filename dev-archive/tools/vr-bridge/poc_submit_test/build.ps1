# Builds the end-to-end VR bridge POC (D3D9Ex shared surface -> D3D11 -> IVRCompositor::Submit),
# 32-bit console EXE matching Psychonauts.exe's architecture. Same LLVM-MinGW i686-w64-mingw32
# toolchain as tools/proxy-d3d9, tools/vr-bridge/poc_shared_surface, tools/vr-bridge/poc_openvr_init
# - no new install needed. Links the vendored OpenVR import lib and copies openvr_api.dll alongside
# the built .exe (required at runtime).
#
# Usage: powershell -File build.ps1
# Requires SteamVR installed and the null driver enabled (see notes/25 section 5 / notes/27) to
# get past VRInitError_Init_InstallationNotFound.

$ErrorActionPreference = "Stop"

$candidates = @(
    "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-ucrt-x86_64\bin\i686-w64-mingw32-clang.exe",
    "$env:LOCALAPPDATA\Programs\llvm-mingw\bin\i686-w64-mingw32-clang.exe",
    "C:\Program Files\llvm-mingw\bin\i686-w64-mingw32-clang.exe"
)

$clang = $null
foreach ($c in $candidates) {
    if (Test-Path $c) { $clang = $c; break }
}
if (-not $clang) {
    $cmd = Get-Command i686-w64-mingw32-clang.exe -ErrorAction SilentlyContinue
    if ($cmd) { $clang = $cmd.Source }
}
if (-not $clang) {
    throw "i686-w64-mingw32-clang.exe not found. Install LLVM-MinGW (winget install MartinStorsjo.LLVM-MinGW.UCRT) or adjust this script's search paths."
}

Write-Host "Using compiler: $clang"

$sdkRoot = Join-Path $PSScriptRoot "..\openvr-sdk"
$src = Join-Path $PSScriptRoot "submit_test_poc.c"
$out = Join-Path $PSScriptRoot "submit_test_poc.exe"
$importLib = Join-Path $sdkRoot "lib\win32\openvr_api.lib"
$runtimeDll = Join-Path $sdkRoot "bin\win32\openvr_api.dll"

if (-not (Test-Path $importLib)) {
    throw "OpenVR import lib not found at $importLib - is the openvr-sdk vendored copy present?"
}

& $clang --target=i686-w64-mingw32 -O2 -mconsole `
    -o $out $src $importLib `
    -ld3d9 -ld3d11 -ldxgi -luser32 -lgdi32 -lkernel32

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Copy-Item -Force $runtimeDll (Join-Path $PSScriptRoot "openvr_api.dll")

Write-Host "Built: $out"
Write-Host "Copied runtime DLL: openvr_api.dll"
Get-Item $out | Select-Object Name, Length, LastWriteTime
