#pragma once
#include <wsk.h>

using SOCKET = UINT_PTR;

#ifndef WSK_INVALID_SOCKET
#  define WSK_INVALID_SOCKET static_cast<SOCKET>(~0)
#endif

#ifndef WSK_FLAG_INVALID_SOCKET
#    define WSK_FLAG_INVALID_SOCKET 0xffffffff
#endif

#ifndef WSK_FLAG_STREAM_SOCKET
#   define WSK_FLAG_STREAM_SOCKET   0x00000008
#endif

struct WSKOVERLAPPED
{
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            ULONG Offset;
            ULONG OffsetHigh;
        } DUMMYSTRUCTNAME;
        PVOID Pointer;
    } DUMMYUNIONNAME;

    KEVENT Event;
};

using LPWSKOVERLAPPED_COMPLETION_ROUTINE = VOID(WSKAPI*)(
    _In_ NTSTATUS       Status,
    _In_ ULONG_PTR      Bytes,
    _In_ WSKOVERLAPPED* Overlapped
    );

VOID WSKAPI WSKSetLastError(
    _In_ NTSTATUS Status
);

NTSTATUS WSKAPI WSKGetLastError();

struct WSKDATA
{
    UINT16 HighestVersion;
    UINT16 LowestVersion;
};

NTSTATUS WSKAPI WSKStartup(
    _In_  UINT16   Version,
    _Out_ WSKDATA* WSKData
);

VOID WSKAPI WSKCleanup();

VOID WSKAPI WSKCreateEvent(
    _Out_ KEVENT* Event
);

NTSTATUS WSKAPI WSKGetOverlappedResult(
    _In_  SOCKET         Socket,
    _In_  WSKOVERLAPPED* Overlapped,
    _Out_opt_ SIZE_T*    TransferBytes,
    _In_  BOOLEAN        Wait
);

NTSTATUS WSKAPI WSKGetAddrInfo(
    _In_opt_ LPCWSTR        NodeName,
    _In_opt_ LPCWSTR        ServiceName,
    _In_     UINT32         Namespace,
    _In_opt_ GUID*          Provider,
    _In_opt_ PADDRINFOEXW   Hints,
    _Outptr_result_maybenull_ PADDRINFOEXW*  Result,
    _In_opt_ UINT32         TimeoutMilliseconds,
    _In_opt_ WSKOVERLAPPED* Overlapped,
    _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
);

VOID WSKAPI WSKFreeAddrInfo(
    _In_ PADDRINFOEXW Data
);

NTSTATUS WSKAPI WSKGetNameInfo(
    _In_ const SOCKADDR*  Address,
    _In_ ULONG      AddressLength,
    _Out_writes_opt_(NodeNameSize)      LPWSTR  NodeName,
    _In_ ULONG      NodeNameSize,
    _Out_writes_opt_(ServiceNameSize)   LPWSTR  ServiceName,
    _In_ ULONG      ServiceNameSize,
    _In_ ULONG      Flags
);

constexpr auto WSK_MAX_ADDRESS_STRING_LENGTH = 64u;

NTSTATUS WSKAPI WSKAddressToString(
    _In_reads_bytes_(AddressLength) SOCKADDR* SockAddress,
    _In_    UINT32  AddressLength,
    _Out_writes_to_(*AddressStringLength, *AddressStringLength) LPWSTR AddressString,
    _Inout_ UINT32* AddressStringLength
);

NTSTATUS WSKAPI WSKStringToAddress(
    _In_    PCWSTR      AddressString,
    _Inout_ SOCKADDR*   SockAddress,    // must init Address->si_family
    _Inout_ UINT32*     AddressLength
);

NTSTATUS WSKAPI WSKSocket(
    _Out_ SOCKET*           Socket,
    _In_  ADDRESS_FAMILY    AddressFamily,
    _In_  USHORT            SocketType,
    _In_  ULONG             Protocol,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor
);

NTSTATUS WSKAPI WSKCloseSocket(
    _In_ SOCKET Socket
);

NTSTATUS WSKAPI WSKIoctl(
    _In_ SOCKET         Socket,
    _In_ ULONG          ControlCode,
    _In_reads_bytes_opt_(InputSize)     PVOID InputBuffer,
    _In_ SIZE_T         InputSize,
    _Out_writes_bytes_opt_(OutputSize)  PVOID OutputBuffer,
    _In_ SIZE_T         OutputSize,
    _Out_opt_ SIZE_T*   OutputSizeReturned,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
);

NTSTATUS WSKAPI WSKSetSocketOpt(
    _In_ SOCKET         Socket,
    _In_ ULONG          OptionLevel,    // SOL_xxxx
    _In_ ULONG          OptionName,     // SO_xxxx
    _In_reads_bytes_(InputSize)     PVOID InputBuffer,
    _In_ SIZE_T         InputSize
);

NTSTATUS WSKAPI WSKGetSocketOpt(
    _In_ SOCKET         Socket,
    _In_ ULONG          OptionLevel,    // SOL_xxxx
    _In_ ULONG          OptionName,     // SO_xxxx
    _Out_writes_bytes_(*OutputSize) PVOID OutputBuffer,
    _Inout_ SIZE_T*     OutputSize
);

NTSTATUS WSKAPI WSKBind(
    _In_ SOCKET         Socket,
    _In_ PSOCKADDR      LocalAddress,
    _In_ SIZE_T         LocalAddressLength
);

NTSTATUS WSKAPI WSKAccpet(
    _In_  SOCKET        Socket,
    _Out_ SOCKET*       SocketClient,
    _Out_opt_ PSOCKADDR LocalAddress,
    _In_ SIZE_T         LocalAddressLength,
    _Out_opt_ PSOCKADDR RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength
);

NTSTATUS WSKAPI WSKListen(
    _In_ SOCKET         Socket,
    _In_ INT            BackLog
);

NTSTATUS WSKAPI WSKConnect(
    _In_ SOCKET         Socket,
    _In_ PSOCKADDR      RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength
);

NTSTATUS WSKAPI WSKDisconnect(
    _In_ SOCKET         Socket,
    _In_ ULONG          Flags
);

NTSTATUS WSKAPI WSKSend(
    _In_ SOCKET         Socket,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesSent,
    _In_ ULONG          Flags,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
);

NTSTATUS WSKAPI WSKSendTo(
    _In_ SOCKET         Socket,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesSent,
    _Reserved_ ULONG    Flags,
    _In_opt_ PSOCKADDR  RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
);

NTSTATUS WSKAPI WSKReceive(
    _In_ SOCKET         Socket,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesRecvd,
    _In_ ULONG          Flags,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
);

NTSTATUS WSKAPI WSKReceiveFrom(
    _In_ SOCKET         Socket,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesRecvd,
    _Reserved_ ULONG    Flags,
    _Out_opt_ PSOCKADDR RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
);
