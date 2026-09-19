#pragma once

// unnecessary, fix ReSharper's code analysis.
#pragma warning(suppress: 4117)
#define _KERNEL_MODE 1

#include <Veil.h>

// netiodef.h (WDK 10.0.26100/10.0.28000) unconditionally redeclares the 32-bit
// handle types without POINTER_32, which collides with the Veil definitions
// (C2086: "HANDLE32 * __ptr32 PHANDLE32" redefinition). C2086 cannot be
// suppressed (#pragma warning(disable:2086) is reported as C4616), and swapping
// the include order only moves the error to the Veil side. Rename the netiodef.h
// copies for the duration of the network stack headers instead; no other
// kernel-mode/shared header mentions these two names.
#define HANDLE32  NETIODE_HANDLE32
#define PHANDLE32 NETIODE_PHANDLE32

// System
#include <intrin.h>
#include <wsk.h>

#undef HANDLE32
#undef PHANDLE32

// C & C++
#include <stddef.h>
#include <stdlib.h>

// Global
static const ULONG WSK_POOL_TAG = ' KSW'; // 'WSK '
