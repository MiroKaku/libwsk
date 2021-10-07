#include "universal.h"
#include "libwsk.h"

#pragma comment(lib, "Netio.lib")

//////////////////////////////////////////////////////////////////////////
// Private Struct

static constexpr auto WSK_POOL_TAG = ' KSW'; // 'WSK '

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

WSK_CLIENT_DISPATCH WSKClientDispatch = {
    MAKE_WSK_VERSION(1, 0), // This default uses WSK version 1.0
    0,                      // Reserved
    nullptr                 // WskClientEvent callback is not required in WSK version 1.0
};

WSK_REGISTRATION WSKRegistration;
WSK_PROVIDER_NPI WSKNPIProvider;

//////////////////////////////////////////////////////////////////////////
// Private Function

static PLARGE_INTEGER WSKTimeoutToLargeInteger(
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

static NTSTATUS WSKCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context
);

static WSK_CONTEXT_IRP* WSKAllocContextIRP(
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

static VOID WSKFreeContextIRP(
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

static NTSTATUS WSKCompletionRoutine(
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

//////////////////////////////////////////////////////////////////////////
// Public  Function

NTSTATUS WSKStartup(_In_ UINT16 Version, _Out_ WSKDATA* WSKData)
{
    NTSTATUS Status = STATUS_SUCCESS;

    do 
    {
        *WSKData = {};

        WSKClientDispatch.Version = Version;

        WSK_CLIENT_NPI NPIClient{};
        NPIClient.ClientContext = nullptr;
        NPIClient.Dispatch = &WSKClientDispatch;

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

    } while (false);

    return Status;
}

VOID WSKCleanup()
{
    if (WSKNPIProvider.Client)
    {
        WSKNPIProvider = {};
        WskReleaseProviderNPI(&WSKRegistration);
    }

    WskDeregister(&WSKRegistration);
}

NTSTATUS WSKGetAddrInfo(
    _In_opt_ LPCWSTR NodeName,
    _In_opt_ LPCWSTR ServiceName,
    _In_     UINT32 Namespace,
    _In_opt_ GUID* Provider,
    _In_opt_ PADDRINFOEXW Hints,
    _Outptr_ PADDRINFOEXW* Result,
    _In_opt_ UINT32 TimeoutMilliseconds,
    _In_opt_ LPLOOKUPSERVICE_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        *Result = nullptr;

        if (TimeoutMilliseconds && CompletionRoutine)
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

        UNICODE_STRING NodeNameS;
        UNICODE_STRING ServiceNameS;

        RtlInitUnicodeString(&NodeNameS, NodeName);
        RtlInitUnicodeString(&ServiceNameS, ServiceName);

        Status = WSKNPIProvider.Dispatch->WskGetAddressInfo(
            WSKNPIProvider.Client,
            &NodeNameS,
            &ServiceNameS,
            Namespace,
            Provider,
            Hints,
            reinterpret_cast<PADDRINFOEXW*>(&WSKContext->Context), // This Context is query result.
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

VOID WSKFreeAddrInfo(
    _In_ PADDRINFOEXW Data
)
{
    if (Data)
    {
        WSKNPIProvider.Dispatch->WskFreeAddressInfo(
            WSKNPIProvider.Client,
            Data);
    }
}

NTSTATUS WSKAddressToString(
    _In_reads_bytes_(AddressLength) SOCKADDR_INET* Address,
    _In_     UINT32  AddressLength,
    _Out_writes_to_(*AddressStringLength, *AddressStringLength) LPWSTR AddressString,
    _Inout_  UINT32* AddressStringLength
)
{
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    do
    {
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

NTSTATUS WSKStringToAddress(
    _In_ PCWSTR AddressString,
    _Inout_ SOCKADDR_INET* Address,
    _Inout_ UINT32* AddressLength
)
{
    NTSTATUS Status = STATUS_INVALID_PARAMETER;

    do
    {
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
