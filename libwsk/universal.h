#pragma once

// unnecessary, fix ReSharper's code analysis.
#pragma warning(suppress: 4117)
#define _KERNEL_MODE 1

#include <Veil/Veil.h>

// System
#include <intrin.h>
#include <wsk.h>

// C & C++
#include <stddef.h>
#include <stdlib.h>

// Global
static const ULONG WSK_POOL_TAG = ' KSW'; // 'WSK '
