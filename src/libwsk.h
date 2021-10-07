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
    _In_opt_ LPCWSTR NodeName,
    _In_opt_ LPCWSTR ServiceName,
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

NTSTATUS WSKAddressToString(
    _In_reads_bytes_(AddressLength) SOCKADDR_INET* Address,
    _In_    UINT32  AddressLength,
    _Out_writes_to_(*AddressStringLength, *AddressStringLength) LPWSTR AddressString,
    _Inout_ UINT32* AddressStringLength
);

NTSTATUS WSKStringToAddress(
    _In_ PCWSTR AddressString,
    _Inout_ SOCKADDR_INET* Address, // must init Address->si_family
    _Inout_ UINT32* AddressLength
);
