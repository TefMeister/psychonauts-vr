# Tooling Setup Log

Date: 2026-08-16

## Workspace

Created `C:\Users\Tefa\Documents\PsychonautsVR\` with subfolders:
- `notes/` — this documentation
- `tools/` — reserved for future mod tooling (Astralathe, dxwrapper, custom DLLs, etc.) — currently empty, nothing downloaded into it yet
- `recon/` — raw output from static analysis of the game binary

The actual game install at `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts` was **not** modified — read-only access only, confirmed by directory listing and PE parsing without writes.

## Claude Code plugins installed

Both installs required `git`, which was **not present** on this machine. Installed via
`winget install --id Git.Git -e --source winget --accept-package-agreements --accept-source-agreements --silent`
(Git 2.55.0.3). This is a standard, safe, ubiquitous dev tool — flagged here for transparency
but not treated as a "third-party debugger" style risk requiring a stop-and-ask.

### x64dbg-skills (dariushoule/x64dbg-skills)

Installed per its actual README:
```
claude plugin marketplace add dariushoule/x64dbg-skills
claude plugin install x64dbg-skills
```
Confirmed via `claude plugin list`: `x64dbg-skills@x64dbg-skills`, status enabled.

**This plugin is NOT yet functional** — it requires software that is not installed on this
machine and was intentionally *not* silently installed, per the task's own caution about not
installing a third-party debugger without flagging it:

- **x64dbg** (the actual debugger) + **x64dbg Automate** — not found anywhere under
  `C:\Program Files*`, and no `x64dbg`/`x32dbg` command on PATH.
- **x64dbg MCP server** configured in Claude Code — not set up (depends on x64dbg being
  installed first).
- **Python 3 with `x64dbg_automate[mcp]`** — the `python`/`python3` on PATH are Microsoft
  Store app-execution-alias stubs, not a real interpreter (`python --version` fails with
  "Python was not found; run without arguments to install from the Microsoft Store...").
  No real Python install exists on this machine.

**Action needed from the user (real blocker, not something to silently auto-install):**
install x64dbg (https://x64dbg.com) + x64dbg Automate, install a real Python 3.x, then
`pip install x64dbg_automate[mcp] --upgrade`, then configure the x64dbg MCP server. Flagging
this rather than installing a debugger unattended.

### superpowers (obra/superpowers)

Installed from the official marketplace, already registered on this machine:
```
claude plugin install superpowers@claude-plugins-official
```
Confirmed via `claude plugin list`: `superpowers@claude-plugins-official` v6.3.0, enabled.
This one has no extra prerequisites beyond git (now installed) — it's a skills/methodology
plugin, not a debugger integration.

### Telemetry opt-out

Per superpowers' own docs, the mechanism is the env var `SUPERPOWERS_DISABLE_TELEMETRY` set to
any true value (it also separately honors Claude Code's own `DISABLE_TELEMETRY` and
`CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC`). Set in the **global** user settings file
`C:\Users\Tefa\.claude\settings.json`:
```json
"env": { "SUPERPOWERS_DISABLE_TELEMETRY": "1" }
```
so it applies to every session touching this project, not just this one. Verified the file is
still valid JSON after the edit.

## Net result

Both trusted tools are installed and enabled at the Claude Code plugin level. superpowers is
fully usable now. x64dbg-skills is installed but inert until the user installs x64dbg itself
(+ x64dbg Automate + a real Python) — this is the actual next blocker for live/dynamic analysis
(task milestone below).
