#include "universal.h"
#include "libwsk.h"
#include "socket.h"

#pragma comment(lib, "Netio.lib")

//////////////////////////////////////////////////////////////////////////
// Private Struct

using WSK_COMPLETION_ROUTINE = VOID(NTAPI*)(
    _In_ NTSTATUS  Status,
    _In_ ULONG_PTR Bytes,
    _In_ PVOID     Context
    );

struct WSK_CONTEXT_IRP
{
    PIRP    Irp;
    KEVENT  Event;
    PVOID   Context;
    PVOID   CompletionRoutine; // WSK_COMPLETION_ROUTINE
};

//////////////////////////////////////////////////////////////////////////
// Global  Data

static volatile long _Initialized = false;

WSK_CLIENT_DISPATCH WSKClientDispatch = {
    MAKE_WSK_VERSION(1, 0), // This default uses WSK version 1.0
    0,                      // Reserved
    nullptr                 // WskClientEvent callback is not required in WSK version 1.0
};

WSK_REGISTRATION WSKRegistration;
WSK_PROVIDER_NPI WSKNPIProvider;

//////////////////////////////////////////////////////////////////////////
// Private Function

PLARGE_INTEGER WSKAPI WSKTimeoutToLargeInteger(
    _In_ UINT32 Milliseconds,
    _In_ PLARGE_INTEGER Timeout
)
{
    if (Milliseconds == WSK_INFINITE_WAIT)
    {
        return nullptr;
    }

    Timeout->QuadPart = Int32x32To64(Milliseconds, -10000);

    return Timeout;
}

NTSTATUS WSKAPI WSKCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context
);

WSK_CONTEXT_IRP* WSKAPI WSKAllocContextIRP(
    _In_opt_ PVOID CompletionRoutine,
    _In_opt_ PVOID Context
)
{
    auto WSKContext = static_cast<WSK_CONTEXT_IRP*>(
        ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(WSK_CONTEXT_IRP), WSK_POOL_TAG));
    if (WSKContext == nullptr)
    {
        return nullptr;
    }

    WSKContext->CompletionRoutine = CompletionRoutine;
    WSKContext->Context = Context;

    WSKContext->Irp = IoAllocateIrp(1, FALSE);
    if (WSKContext->Irp == nullptr)
    {
        ExFreePoolWithTag(WSKContext, WSK_POOL_TAG);
        return nullptr;
    }

    KeInitializeEvent(&WSKContext->Event, SynchronizationEvent, FALSE);

    IoSetCompletionRoutine(WSKContext->Irp, WSKCompletionRoutine, WSKContext, TRUE, TRUE, TRUE);

    return WSKContext;
}

VOID WSKAPI WSKFreeContextIRP(
    _In_ WSK_CONTEXT_IRP* WSKContext
)
{
    if (WSKContext)
    {
        if (WSKContext->Irp)
        {
            IoFreeIrp(WSKContext->Irp);
        }

        ExFreePoolWithTag(WSKContext, WSK_POOL_TAG);
    }
}

NTSTATUS WSKAPI WSKCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    auto WSKContext = static_cast<WSK_CONTEXT_IRP*>(Context);

    if (WSKContext->CompletionRoutine)
    {
        auto Routine = static_cast<WSK_COMPLETION_ROUTINE>(WSKContext->CompletionRoutine);

        __try
        {
            Routine(Irp->IoStatus.Status, Irp->IoStatus.Information, WSKContext->Context);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            __nop();
        }

        WSKFreeContextIRP(WSKContext);
    }
    else
    {
        KeSetEvent(&WSKContext->Event, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS WSKAPI WSKSocketUnsafe(
    _Out_ PWSK_SOCKET*      Socket,
    _In_  ADDRESS_FAMILY    AddressFamily,
    _In_  USHORT            SocketType,
    _In_  ULONG             Protocol,
    _In_opt_ ULONG          Flags,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        *Socket = nullptr;

        if (!InterlockedCompareExchange(&_Initialized, true, true))
        {
            Status = STATUS_NDIS_ADAPTER_NOT_READY;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKNPIProvider.Dispatch->WskSocket(
            WSKNPIProvider.Client,
            AddressFamily,
            SocketType,
            Protocol,
            Flags,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            SecurityDescriptor,
            WSKContext->Irp);

        if (Status == STATUS_PENDING)
        {
            LARGE_INTEGER Timeout{};

            Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                FALSE, WSKTimeoutToLargeInteger(WSK_INFINITE_WAIT, &Timeout));
            if (Status == STATUS_SUCCESS)
            {
                Status = WSKContext->Irp->IoStatus.Status;
            }
        }

        if (NT_SUCCESS(Status))
        {
            *Socket = static_cast<PWSK_SOCKET>(reinterpret_cast<void*>(
                WSKContext->Irp->IoStatus.Information));
        }

        WSKFreeContextIRP(WSKContext);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKCloseSocketUnsafe(
    _In_ PWSK_SOCKET Socket
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        if (!InterlockedCompareExchange(&_Initialized, true, true))
        {
            Status = STATUS_NDIS_ADAPTER_NOT_READY;
            break;
        }

        if (Socket == nullptr)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        auto Dispatch = static_cast<const WSK_PROVIDER_BASIC_DISPATCH*>(Socket->Dispatch);

        Status = Dispatch->WskCloseSocket(
            Socket,
            WSKContext->Irp);

        if (Status == STATUS_PENDING)
        {
            LARGE_INTEGER Timeout{};

            Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                FALSE, WSKTimeoutToLargeInteger(WSK_INFINITE_WAIT, &Timeout));
            if (Status == STATUS_SUCCESS)
            {
                Status = WSKContext->Irp->IoStatus.Status;
            }
        }

        WSKFreeContextIRP(WSKContext);

    } while (false);

    return Status;
}

//////////////////////////////////////////////////////////////////////////
// Public  Function

NTSTATUS WSKAPI WSKStartup(_In_ UINT16 Version, _Out_ WSKDATA* WSKData)
{
    NTSTATUS Status = STATUS_SUCCESS;

    do 
    {
        *WSKData = {};

        if (InterlockedCompareExchange(&_Initialized, true, true))
        {
            WSK_PROVIDER_CHARACTERISTICS Caps;
            Status = WskQueryProviderCharacteristics(&WSKRegistration, &Caps);
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            WSKData->HighestVersion = Caps.HighestVersion;
            WSKData->LowestVersion  = Caps.LowestVersion;

            break;
        }

        WSKSocketsAVLTableInitialize();

        WSK_CLIENT_NPI NPIClient{};
        NPIClient.ClientContext = nullptr;
        NPIClient.Dispatch = &WSKClientDispatch;

        WSKClientDispatch.Version = Version;

        Status = WskRegister(&NPIClient, &WSKRegistration);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        WSK_PROVIDER_CHARACTERISTICS Caps;
        Status = WskQueryProviderCharacteristics(&WSKRegistration, &Caps);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        WSKData->HighestVersion = Caps.HighestVersion;
        WSKData->LowestVersion  = Caps.LowestVersion;

        Status = WskCaptureProviderNPI(&WSKRegistration, WSK_INFINITE_WAIT, &WSKNPIProvider);
        if (!NT_SUCCESS(Status))
        {
            WskDeregister(&WSKRegistration);
            break;
        }

        InterlockedCompareExchange(&_Initialized, true, false);

    } while (false);

    return Status;
}

VOID WSKAPI WSKCleanup()
{
    if (InterlockedCompareExchange(&_Initialized, false, true))
    {
        WSKSocketsAVLTableCleanup();

        WskReleaseProviderNPI(&WSKRegistration);
        WskDeregister(&WSKRegistration);

        WSKNPIProvider = {};
    }
}

NTSTATUS WSKAPI WSKGetAddrInfo(
    _In_opt_ LPCWSTR        NodeName,
    _In_opt_ LPCWSTR        ServiceName,
    _In_     UINT32         Namespace,
    _In_opt_ GUID*          Provider,
    _In_opt_ PADDRINFOEXW   Hints,
    _Outptr_ PADDRINFOEXW*  Result,
    _In_opt_ UINT32         TimeoutMilliseconds,
    _In_opt_ LPLOOKUPSERVICE_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        *Result = nullptr;

        if (!InterlockedCompareExchange(&_Initialized, true, true))
        {
            Status = STATUS_NDIS_ADAPTER_NOT_READY;
            break;
        }

        if (TimeoutMilliseconds != WSK_NO_WAIT && CompletionRoutine != nullptr)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        WSKContext = WSKAllocContextIRP(CompletionRoutine, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        UNICODE_STRING NodeNameS{};
        UNICODE_STRING ServiceNameS{};

        RtlInitUnicodeString(&NodeNameS, NodeName);
        RtlInitUnicodeString(&ServiceNameS, ServiceName);

        Status = WSKNPIProvider.Dispatch->WskGetAddressInfo(
            WSKNPIProvider.Client,
            &NodeNameS,
            &ServiceNameS,
            Namespace,
            Provider,
            Hints,
            reinterpret_cast<PADDRINFOEXW*>(&WSKContext->Context), // The Context is query result.
            nullptr,
            nullptr,
            WSKContext->Irp);

        if (CompletionRoutine == nullptr)
        {
            if (Status == STATUS_PENDING)
            {
                LARGE_INTEGER Timeout{};

                Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                    FALSE, WSKTimeoutToLargeInteger(TimeoutMilliseconds, &Timeout));
                if (Status == STATUS_SUCCESS)
                {
                    Status = WSKContext->Irp->IoStatus.Status;
                }
            }

            *Result = static_cast<PADDRINFOEXW>(WSKContext->Context);

            WSKFreeContextIRP(WSKContext);
        }

    } while (false);

    return Status;
}

VOID WSKAPI WSKFreeAddrInfo(
    _In_ PADDRINFOEXW Data
)
{
    if (!InterlockedCompareExchange(&_Initialized, true, true))
    {
        return;
    }

    if (Data)
    {
        WSKNPIProvider.Dispatch->WskFreeAddressInfo(
            WSKNPIProvider.Client,
            Data);
    }
}

NTSTATUS WSKAPI WSKGetNameInfo(
    _In_ SOCKADDR*  Address,
    _In_ ULONG      AddressLength,
    _Out_writes_opt_(NodeNameSize)      LPWSTR  NodeName,
    _In_ ULONG      NodeNameSize,
    _Out_writes_opt_(ServiceNameSize)   LPWSTR  ServiceName,
    _In_ ULONG      ServiceNameSize,
    _In_ ULONG      Flags
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        if (!InterlockedCompareExchange(&_Initialized, true, true))
        {
            Status = STATUS_NDIS_ADAPTER_NOT_READY;
            break;
        }

        if (NodeName == nullptr && ServiceName == nullptr)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        UNICODE_STRING NodeNameS{};
        UNICODE_STRING ServiceNameS{};

        RtlInitEmptyUnicodeString(&NodeNameS, NodeName, static_cast<USHORT>(NodeNameSize));
        RtlInitEmptyUnicodeString(&ServiceNameS, ServiceName, static_cast<USHORT>(ServiceNameSize));

        Status = WSKNPIProvider.Dispatch->WskGetNameInfo(
            WSKNPIProvider.Client,
            Address,
            AddressLength,
            NodeName ? &NodeNameS : nullptr,
            ServiceName ? &ServiceNameS : nullptr,
            Flags,
            nullptr,
            nullptr,
            WSKContext->Irp);

        if (Status == STATUS_PENDING)
        {
            LARGE_INTEGER Timeout{};

            Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                FALSE, WSKTimeoutToLargeInteger(WSK_INFINITE_WAIT, &Timeout));
            if (Status == STATUS_SUCCESS)
            {
                Status = WSKContext->Irp->IoStatus.Status;
            }
        }

        WSKFreeContextIRP(WSKContext);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKAddressToString(
    _In_reads_bytes_(AddressLength) SOCKADDR* SockAddress,
    _In_     UINT32  AddressLength,
    _Out_writes_to_(*AddressStringLength, *AddressStringLength) LPWSTR AddressString,
    _Inout_  UINT32* AddressStringLength
)
{
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    do
    {
        auto Address = reinterpret_cast<SOCKADDR_INET*>(SockAddress);

        if (Address == nullptr || AddressLength < sizeof ADDRESS_FAMILY)
        {
            break;
        }

        if (Address->si_family == AF_INET)
        {
            if (AddressLength < sizeof Address->Ipv4)
            {
                break;
            }

            Status = RtlIpv4AddressToStringEx(&Address->Ipv4.sin_addr, Address->Ipv4.sin_port,
                AddressString, reinterpret_cast<ULONG*>(AddressStringLength));

            break;
        }

        if (Address->si_family == AF_INET6)
        {
            if (AddressLength < sizeof Address->Ipv6)
            {
                break;
            }

            Status = RtlIpv6AddressToStringEx(&Address->Ipv6.sin6_addr, Address->Ipv6.sin6_scope_id,
                Address->Ipv6.sin6_port, AddressString, reinterpret_cast<ULONG*>(AddressStringLength));

            break;
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKStringToAddress(
    _In_    PCWSTR      AddressString,
    _Inout_ SOCKADDR*   SockAddress,
    _Inout_ UINT32*     AddressLength
)
{
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    do
    {
        auto Address = reinterpret_cast<SOCKADDR_INET*>(SockAddress);

        if (Address == nullptr || AddressLength == nullptr || *AddressLength < sizeof ADDRESS_FAMILY)
        {
            break;
        }

        if (Address->si_family == AF_INET)
        {
            if (*AddressLength < sizeof Address->Ipv4)
            {
                break;
            }

            Status = RtlIpv4StringToAddressEx(AddressString, TRUE,
                &Address->Ipv4.sin_addr, &Address->Ipv4.sin_port);
            if (!NT_SUCCESS(Status))
            {
                *AddressLength = 0u;
                break;
            }

            *AddressLength = sizeof Address->Ipv4;
            break;
        }

        if (Address->si_family == AF_INET6)
        {
            if (*AddressLength < sizeof Address->Ipv6)
            {
                break;
            }

            Status = RtlIpv6StringToAddressEx(AddressString, &Address->Ipv6.sin6_addr,
                &Address->Ipv6.sin6_scope_id, &Address->Ipv6.sin6_port);
            if (!NT_SUCCESS(Status))
            {
                *AddressLength = 0u;
                break;
            }

            *AddressLength = sizeof Address->Ipv6;
            break;
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKSocket(
    _Out_ SOCKET*           Socket,
    _In_  ADDRESS_FAMILY    AddressFamily,
    _In_  USHORT            SocketType,
    _In_  ULONG             Protocol,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    do 
    {
        *Socket = WSK_INVALID_SOCKET;

        if (!InterlockedCompareExchange(&_Initialized, true, true))
        {
            Status = STATUS_NDIS_ADAPTER_NOT_READY;
            break;
        }

        ULONG WSKSocketType = WSK_FLAG_BASIC_SOCKET;

        switch (SocketType)
        {
        case SOCK_STREAM:
            WSKSocketType = WSK_FLAG_STREAM_SOCKET;
            break;
        case SOCK_DGRAM:
            WSKSocketType = WSK_FLAG_DATAGRAM_SOCKET;
            break;
        case SOCK_RAW:
            WSKSocketType = WSK_FLAG_DATAGRAM_SOCKET;
            break;
        }

        PWSK_SOCKET Socket_ = nullptr;

        Status = WSKSocketUnsafe(&Socket_, AddressFamily, SocketType, Protocol, WSKSocketType, SecurityDescriptor);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        if (!WSKSocketsAVLTableInsert(Socket, Socket_, static_cast<USHORT>(WSKSocketType)))
        {
            WSKCloseSocketUnsafe(Socket_);
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKCloseSocket(
    _In_ SOCKET Socket
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if (!InterlockedCompareExchange(&_Initialized, true, true))
        {
            Status = STATUS_NDIS_ADAPTER_NOT_READY;
            break;
        }

        if (Socket == WSK_INVALID_SOCKET)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        PWSK_SOCKET Socket_ = nullptr;
        USHORT SocketType   = static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET);

        if (!WSKSocketsAVLTableFind(Socket, &Socket_, &SocketType))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKCloseSocketUnsafe(Socket_);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        WSKSocketsAVLTableDelete(Socket);

    } while (false);

    return Status;
}
