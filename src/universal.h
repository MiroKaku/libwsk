#pragma once

#undef  NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN10_CO

// C & C++
#include <stddef.h>
#include <stdlib.h>

// System
#include <intrin.h>
#include <ntddk.h>
#include <wdm.h>
#include <wsk.h>

// Global
static constexpr auto WSK_POOL_TAG = ' KSW'; // 'WSK '
