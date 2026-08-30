# notes/38-39: autonomous gameplay entry - VERIFIED WORKING end-to-end 2026-08-18 (reached real
# gameplay in Whispering Rock, HUD visible, camera in world-space coords, from a cold title
# screen with zero human input).
#
# Route (user-provided + live-refined):
#   title "Press to begin" -> SPACE -> Raz appears on the brain (main menu)
#   -> walk UP x3, LEFT x1 (approach the BLUE "CONTINUE" door - middle card of the three)
#   -> the door does NOT trigger from its edge: take TINY steps (100ms holds) toward it,
#      JUMPING (SPACE) after every step - the jump while on/over the card enters it
#   -> loading screen -> gameplay (save must exist for CONTINUE).
#
# Position drift is real (walk timings are approximate): for robust automation, pair this with
# PSYVR_DUMP_EYES=1 and check the eye dump between phases; the definitive "gameplay reached"
# signal is the BVM cache SET camera coordinates jumping from menu-space (|xyz| < ~1000) to
# world-space (tens of thousands). CONTINUE loads the most recent save - the user has declared
# this machine's saves expendable (2026-08-18).
#
# Assumes: game already running at the title screen, window focusable, user hands-off.
param([int]$StepHoldMs = 450)

$send = Join-Path $PSScriptRoot "send_key.ps1"

& $send -Scan 0x39                                   # SPACE: title -> menu
Start-Sleep -Seconds 4
for ($i = 0; $i -lt 3; $i++) {
    & $send -Scan 0xC8 -Extended -HoldMs $StepHoldMs # UP x3 toward the doors
    Start-Sleep -Milliseconds 400
}
& $send -Scan 0xCB -Extended -HoldMs $StepHoldMs     # LEFT x1 (blue CONTINUE door is mid-left)
Start-Sleep -Milliseconds 400

# door-entry phase: micro-step + jump, repeated - covers positional drift around the card
foreach ($k in @(0xC8, 0x39, 0xCB, 0x39, 0xC8, 0x39, 0xCB, 0x39)) {
    if ($k -eq 0x39) { & $send -Scan 0x39 | Out-Null }
    else { & $send -Scan $k -Extended -HoldMs 100 | Out-Null }
    Start-Sleep -Milliseconds 700
}
"sequence sent - if the door caught, loading begins now (verify via BVM world-space coords in the proxy log)"
