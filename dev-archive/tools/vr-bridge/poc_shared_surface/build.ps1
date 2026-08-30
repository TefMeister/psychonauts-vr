# Builds the D3D9Ex -> D3D11 shared-surface interop proof-of-concept (32-bit console EXE,
# matching Psychonauts.exe's architecture, though nothing in this test is architecture-specific).
# Uses the same LLVM-MinGW i686-w64-mingw32 toolchain as tools/proxy-d3d9 (winget:
# MartinStorsjo.LLVM-MinGW.UCRT) - no new install needed, already present on this machine.
#
# Usage: powershell -File build.ps1
# Then just run the resulting .exe directly - no game, no SteamVR, no debugger needed.

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

$src = Join-Path $PSScriptRoot "shared_surface_poc.c"
$out = Join-Path $PSScriptRoot "shared_surface_poc.exe"

& $clang --target=i686-w64-mingw32 -O2 -mconsole `
    -o $out $src `
    -ld3d9 -ld3d11 -ldxgi -luser32 -lgdi32 -lkernel32

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Built: $out"
Get-Item $out | Select-Object Name, Length, LastWriteTime
