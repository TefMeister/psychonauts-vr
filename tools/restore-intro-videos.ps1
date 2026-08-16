# Restores the pre-menu splash videos renamed by silence-intro-videos.ps1.
# Always call this after every debug/test session, even if the session failed or crashed.
$dir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\WorkResource\Cutscenes\Prerendered"
$targets = @("INTRO.bik","DFLogo.bik","MajescoLogo.bik","transgaming.bik")
foreach ($f in $targets) {
    if (Test-Path "$dir\$f.silenced") {
        Rename-Item "$dir\$f.silenced" "$f" -ErrorAction SilentlyContinue
    }
}
