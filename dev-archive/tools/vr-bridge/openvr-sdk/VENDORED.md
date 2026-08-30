OpenVR SDK vendored (headers + win32 lib/dll only) via sparse shallow clone.
Source: https://github.com/ValveSoftware/openvr
Commit: 0924064316de3effbcd1acf1e309182a2deb1c05 (master HEAD at vendoring time, via `git ls-remote`)
Vendored: 2026-08-17

Only headers/, lib/win32/, bin/win32/ kept (other platforms/architectures stripped to keep this
workspace lightweight -- linux/osx/android libs, win64, docs, samples all removed). Within
bin/win32/, only openvr_api.dll (the runtime DLL actually needed to run anything) was kept --
openvr_api.pdb (11.3MB debug symbols, not needed) and openvr_api.dll.sig were dropped.
License: BSD-3-Clause (see LICENSE, or https://github.com/ValveSoftware/openvr/blob/master/LICENSE).

No build/install step needed to use these -- openvr.h/openvr_capi.h are plain C/C++ headers,
lib/win32/openvr_api.lib is a standard 32-bit import library, bin/win32/openvr_api.dll is the
runtime DLL (copy next to any test .exe that links it, exactly like any other vendored DLL
dependency).
