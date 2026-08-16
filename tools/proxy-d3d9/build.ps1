# Builds the minimal logging proxy d3d9.dll (32-bit, matching Psychonauts.exe).
# Uses LLVM-MinGW's i686-w64-mingw32 clang target (winget: MartinStorsjo.LLVM-MinGW.UCRT).
#
# Usage: powershell -File build.ps1

$ErrorActionPreference = "Stop"

$candidates = @(
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

& $clang --target=i686-w64-mingw32 -shared -O2 -municode `
    -o $out $src $def `
    -lgdi32 -luser32 -lkernel32

if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

Write-Host "Built: $out"
& file $out 2>$null
