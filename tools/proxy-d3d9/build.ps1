# Builds the minimal logging proxy d3d9.dll (32-bit, matching Psychonauts.exe).
# Uses LLVM-MinGW's i686-w64-mingw32 clang target (winget: MartinStorsjo.LLVM-MinGW.UCRT).
#
# Usage: powershell -File build.ps1

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

$src = Join-Path $PSScriptRoot "proxy_d3d9.c"
$def = Join-Path $PSScriptRoot "proxy_d3d9.def"
$out = Join-Path $PSScriptRoot "d3d9.dll"

# notes/28: VR submission path (additive, off by default via PSYVR_ENABLE_SUBMIT) needs d3d11/dxgi
# (linked normally - no name collision with this DLL) and the vendored OpenVR import lib. Real
# system d3d9.dll's Direct3DCreate9Ex is resolved dynamically via GetProcAddress at runtime (see
# VRBridge_Init), NOT linked here, to avoid any risk of the loader resolving a "-ld3d9" import back
# to this DLL itself when loaded into the game process as d3d9.dll.
$sdkRoot = Join-Path $PSScriptRoot "..\vr-bridge\openvr-sdk"
$importLib = Join-Path $sdkRoot "lib\win32\openvr_api.lib"
$runtimeDll = Join-Path $sdkRoot "bin\win32\openvr_api.dll"
if (-not (Test-Path $importLib)) {
    throw "OpenVR import lib not found at $importLib - is tools/vr-bridge/openvr-sdk vendored?"
}

& $clang --target=i686-w64-mingw32 -shared -O2 -municode `
    -o $out $src $def $importLib `
    -ld3d11 -ldxgi -lgdi32 -luser32 -lkernel32

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Copy-Item -Force $runtimeDll (Join-Path $PSScriptRoot "openvr_api.dll")

Write-Host "Built: $out"
Write-Host "Copied openvr_api.dll alongside it (required at runtime if PSYVR_ENABLE_SUBMIT=1 -"
Write-Host "both files must be deployed together into the game directory for the VR path to work)."
Get-Item $out | Select-Object Name, Length, LastWriteTime
