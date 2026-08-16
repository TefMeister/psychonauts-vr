# Temporarily renames Psychonauts' pre-menu splash videos so they're skipped silently.
# ALWAYS pair with restore-intro-videos.ps1 after the test/debug session, even on error paths.
# Confirmed 2026-08-16: game runs normally (no crash, responsive, active CPU) with these missing.
$dir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\WorkResource\Cutscenes\Prerendered"
$targets = @("INTRO.bik","DFLogo.bik","MajescoLogo.bik","transgaming.bik")
foreach ($f in $targets) {
    if (Test-Path "$dir\$f") {
        Rename-Item "$dir\$f" "$f.silenced" -ErrorAction SilentlyContinue
    }
}
