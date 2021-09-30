#pragma once
#include <wsk.h>


struct WSKDATA
{
    UINT16 HighestVersion;
    UINT16 LowestVersion;
};

NTSTATUS WSKStartup(
    _In_  UINT16   Version,
    _Out_ WSKDATA* WSKData
);

VOID WSKCleanup();

using LPLOOKUPSERVICE_COMPLETION_ROUTINE = VOID(NTAPI*)(
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Bytes,
    _In_ PADDRINFOEXW Result
    );

NTSTATUS WSKGetAddrInfo(
    _In_opt_ const wchar_t* NodeName,
    _In_opt_ const wchar_t* ServiceName,
    _In_     UINT32 Namespace,
    _In_opt_ GUID* Provider,
    _In_opt_ PADDRINFOEXW Hints,
    _Outptr_ PADDRINFOEXW* Result,
    _In_opt_ UINT32 TimeoutMilliseconds,
    _In_opt_ LPLOOKUPSERVICE_COMPLETION_ROUTINE CompletionRoutine
);

VOID WSKFreeAddrInfo(
    _In_ PADDRINFOEXW Data
);
