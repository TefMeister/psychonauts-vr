#pragma once
/* hr_common - the few helpers d3d9_proxy.c shares with the readback and headset files. */
#include <windows.h>

/* one timestamped line into d3d9_proxy_log.txt beside the exe (safe from any thread: one WriteFile per line) */
void hr_log(const char *fmt, ...);

/* full path of `name` in the exe's folder; out[0] = 0 if it does not fit */
void hr_beside_exe(const WCHAR *name, WCHAR *out, DWORD room);
