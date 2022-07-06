#include "universal.h"
#include "libwsk.h"
#include "socket.h"

#pragma comment(lib, "Netio.lib")

//////////////////////////////////////////////////////////////////////////
// Private Struct

using WSK_COMPLETION_ROUTINE = VOID(WSKAPI*)(
    _In_ NTSTATUS  Status,
    _In_ ULONG_PTR Bytes,
    _In_ PVOID     Context
    );

struct WSK_CONTEXT_IRP
{
    PIRP    Irp;
    KEVENT  Event;
    PVOID   Context;
    union {
        PVOID   CompletionRoutine;  // WSK_COMPLETION_ROUTINE
        PVOID   Pointer;            // Other
    } DUMMYUNIONNAME;

    WSK_BUF InputBuffer;
    WSK_BUF OutputBuffer;
};

#if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)
typedef struct _WSK_STREAM_SOCKET_WIN7 {
    BOOLEAN     Mode;   // 0:unknown, 1:listen, 2:connect
    PWSK_SOCKET Listen;
    PWSK_SOCKET Connect;
} WSK_STREAM_SOCKET_WIN7, * PWSK_STREAM_SOCKET_WIN7;
#endif // if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)

//////////////////////////////////////////////////////////////////////////
// Global  Data

static volatile long _Initialized = false;
static volatile long _lasterror   = STATUS_SUCCESS;

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

NTSTATUS WSKAPI WSKLockBuffer(
    _In_  PVOID    Buffer,
    _In_  SIZE_T   BufferLength,
    _Out_ PWSK_BUF WSKBuffer,
    _In_  BOOLEAN  ReadOnly
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        WSKBuffer->Offset = 0;
        WSKBuffer->Length = BufferLength;

        WSKBuffer->Mdl = IoAllocateMdl(Buffer, static_cast<ULONG>(BufferLength), FALSE, FALSE, nullptr);
        if (WSKBuffer->Mdl == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        __try
        {
            MmProbeAndLockPages(WSKBuffer->Mdl, KernelMode, ReadOnly ? IoReadAccess : IoWriteAccess);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Status = GetExceptionCode();

            IoFreeMdl(WSKBuffer->Mdl), WSKBuffer->Mdl = nullptr;
            break;
        }

    } while (false);

    return Status;
}

//NTSTATUS WSKAPI WSKLockBuffer(
//    _In_  PNET_BUFFER_LIST NetBufferList,
//    _In_  ULONG BufferOffset,
//    _Out_ PWSK_BUF WSKBuffer,
//    _In_  BOOLEAN  ReadOnly
//)
//{
//    NTSTATUS Status = STATUS_SUCCESS;
//
//    do
//    {
//        WSKBuffer->Offset = BufferOffset;
//        WSKBuffer->Length = NetBufferList->FirstNetBuffer->DataLength - BufferOffset;
//
//        WSKBuffer->Mdl = NetBufferList->FirstNetBuffer->CurrentMdl;
//        if (WSKBuffer->Mdl == nullptr)
//        {
//            Status = STATUS_INSUFFICIENT_RESOURCES;
//            break;
//        }
//
//        __try
//        {
//            if ((WSKBuffer->Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA) != MDL_MAPPED_TO_SYSTEM_VA &&
//                (WSKBuffer->Mdl->MdlFlags & MDL_PAGES_LOCKED) != MDL_PAGES_LOCKED &&
//                (WSKBuffer->Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL) != MDL_SOURCE_IS_NONPAGED_POOL)
//            {
//                MmProbeAndLockPages(WSKBuffer->Mdl, KernelMode, ReadOnly ? IoReadAccess : IoWriteAccess);
//            }
//        }
//        __except (EXCEPTION_EXECUTE_HANDLER)
//        {
//            IoFreeMdl(WSKBuffer->Mdl), WSKBuffer->Mdl = nullptr;
//
//            Status = GetExceptionCode();
//            break;
//        }
//
//    } while (false);
//
//    return Status;
//}

VOID WSKAPI WSKUnlockBuffer(
    _In_  PWSK_BUF WSKBuffer
)
{
    if (WSKBuffer)
    {
        if (WSKBuffer->Mdl)
        {
            MmUnlockPages(WSKBuffer->Mdl);
            IoFreeMdl(WSKBuffer->Mdl);
            WSKBuffer->Mdl = nullptr;
        }
    }
}

NTSTATUS WSKCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context
);

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

        WSKUnlockBuffer(&WSKContext->InputBuffer);
        WSKUnlockBuffer(&WSKContext->OutputBuffer);

        ExFreePoolWithTag(WSKContext, WSK_POOL_TAG);
    }
}

WSK_CONTEXT_IRP* WSKAPI WSKAllocContextIRP(
    _In_opt_ PVOID CompletionRoutine,
    _In_opt_ PVOID Context,
    _In_opt_ BOOLEAN OnlyReadInputBuffer = true,
    _In_reads_bytes_opt_(InputSize)     PVOID InputBuffer  = nullptr,
    _In_ SIZE_T         InputSize  = 0,
    _Out_writes_bytes_opt_(OutputSize)  PVOID OutputBuffer = nullptr,
    _In_ SIZE_T         OutputSize = 0
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext = nullptr;

    do 
    {
        WSKContext = static_cast<WSK_CONTEXT_IRP*>(ExAllocatePoolZero(NonPagedPool,
            sizeof(WSK_CONTEXT_IRP), WSK_POOL_TAG));
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        WSKContext->CompletionRoutine = CompletionRoutine;
        WSKContext->Context = Context;

        if (InputBuffer)
        {
            Status = WSKLockBuffer(InputBuffer, InputSize, &WSKContext->InputBuffer, OnlyReadInputBuffer);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        if (OutputBuffer)
        {
            Status = WSKLockBuffer(OutputBuffer, OutputSize, &WSKContext->OutputBuffer, false);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        if (Context == nullptr)
        {
            WSKCreateEvent(&WSKContext->Event);
        }

        WSKContext->Irp = IoAllocateIrp(1, FALSE);
        if (WSKContext->Irp == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        IoSetCompletionRoutine(WSKContext->Irp, WSKCompletionRoutine, WSKContext, TRUE, TRUE, TRUE);

    } while (false);

    if (!NT_SUCCESS(Status))
    {
        if (WSKContext)
        {
            WSKFreeContextIRP(WSKContext);
            WSKContext = nullptr;
        }
    }

    return WSKContext;
}

NTSTATUS WSKCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    auto WSKContext = static_cast<WSK_CONTEXT_IRP*>(Context);
    if (WSKContext == nullptr)
    {
        __debugbreak();
        return STATUS_INVALID_ADDRESS;
    }

    auto Overlapped = static_cast<WSKOVERLAPPED*>(WSKContext->Context);
    if (Overlapped)
    {
        Overlapped->Internal     = Irp->IoStatus.Status;
        Overlapped->InternalHigh = Irp->IoStatus.Information;

        auto Routine = static_cast<WSK_COMPLETION_ROUTINE>(WSKContext->CompletionRoutine);
        if (Routine)
        {
            __try
            {
                Routine(Irp->IoStatus.Status, Irp->IoStatus.Information, WSKContext->Context);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                __nop();
            }
        }

        KeSetEvent(&Overlapped->Event, IO_NO_INCREMENT, FALSE);
        WSKFreeContextIRP(WSKContext);
    }
    else
    {
        KeSetEvent(&WSKContext->Event, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static WSKOVERLAPPED WSKEmptyOverlapped;

VOID NTAPI WSKEmptyAsync(
    _In_ NTSTATUS  Status,
    _In_ ULONG_PTR Bytes,
    _In_ PVOID     Context
)
{
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Bytes);

    auto Overlapped = static_cast<WSKOVERLAPPED*>(Context);
    if (Overlapped != nullptr)
    {
        __debugbreak();
    }
    else
    {
        KeResetEvent(&Overlapped->Event);
    }
}

NTSTATUS WSKAPI WSKCloseSocketUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType
);

static NTSTATUS WSKAPI WSKSocketUnsafeDownlevel(
    _Out_ PWSK_SOCKET* Socket,
    _In_  ADDRESS_FAMILY    AddressFamily,
    _In_  USHORT            SocketType,
    _In_  ULONG             Protocol,
    _In_  ULONG             Flags,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        if (Protocol == 0)
        {
            switch (SocketType)
            {
            case SOCK_STREAM:
                Protocol = IPPROTO_TCP;
                break;

            case SOCK_DGRAM:
                Protocol = IPPROTO_UDP;
                break;

            case SOCK_RAW:
                Protocol = IPPROTO_RAW;
                break;
            }
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

NTSTATUS WSKAPI WSKSocketUnsafe(
    _Out_ PWSK_SOCKET*      Socket,
    _In_  ADDRESS_FAMILY    AddressFamily,
    _In_  USHORT            SocketType,
    _In_  ULONG             Protocol,
    _In_  ULONG             Flags,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    *Socket = nullptr;

    if (!InterlockedCompareExchange(&_Initialized, true, true))
    {
        Status = STATUS_NDIS_ADAPTER_NOT_READY;
        return Status;
    }

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    Status = WSKSocketUnsafeDownlevel(Socket, AddressFamily, SocketType, Protocol, Flags, SecurityDescriptor);
#else
    WSK_SOCKET* Stream  = nullptr;
    WSK_SOCKET* Listen  = nullptr;
    WSK_SOCKET* Connect = nullptr;

    do
    {
        if (Flags != WSK_FLAG_STREAM_SOCKET)
        {
            Status = WSKSocketUnsafeDownlevel(Socket, AddressFamily, SocketType, Protocol, Flags, SecurityDescriptor);
            break;
        }

        Stream = static_cast<PWSK_SOCKET>(ExAllocatePoolZero(NonPagedPool,
            sizeof(WSK_STREAM_SOCKET_WIN7), WSK_POOL_TAG));
        if (Stream == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKSocketUnsafeDownlevel(&Listen, AddressFamily, SocketType, Protocol, WSK_FLAG_LISTEN_SOCKET, SecurityDescriptor);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        Status = WSKSocketUnsafeDownlevel(&Connect, AddressFamily, SocketType, Protocol, WSK_FLAG_CONNECTION_SOCKET, SecurityDescriptor);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        auto StreamImpl = reinterpret_cast<WSK_STREAM_SOCKET_WIN7*>(Stream);
        StreamImpl->Mode    = 0;
        StreamImpl->Listen  = Listen;
        StreamImpl->Connect = Connect;

        *Socket = Stream;

    } while (false);

    if (!NT_SUCCESS(Status))
    {
        if (Listen)
        {
            WSKCloseSocketUnsafe(Listen, WSK_FLAG_LISTEN_SOCKET);
        }

        if (Connect)
        {
            WSKCloseSocketUnsafe(Connect, WSK_FLAG_CONNECTION_SOCKET);
        }

        if (Stream)
        {
            ExFreePoolWithTag(Stream, WSK_POOL_TAG);
        }
    }
#endif

    return Status;
}

static NTSTATUS WSKAPI WSKCloseSocketUnsafeDownlevel(
    _In_ PWSK_SOCKET Socket
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        auto Dispatch = static_cast<const WSK_PROVIDER_BASIC_DISPATCH*>(Socket->Dispatch);

        Status = Dispatch->WskCloseSocket(Socket, WSKContext->Irp);
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

NTSTATUS WSKAPI WSKCloseSocketUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType
)
{
    UNREFERENCED_PARAMETER(WskSocketType);

    NTSTATUS Status = STATUS_SUCCESS;

    if (!InterlockedCompareExchange(&_Initialized, true, true))
    {
        Status = STATUS_NDIS_ADAPTER_NOT_READY;
        return Status;
    }

    if (Socket == nullptr)
    {
        Status = STATUS_INVALID_PARAMETER;
        return Status;
    }

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    Status = WSKCloseSocketUnsafeDownlevel(Socket);
#else
    do
    {
        if (WskSocketType != WSK_FLAG_STREAM_SOCKET)
        {
            Status = WSKCloseSocketUnsafeDownlevel(Socket);
            break;
        }

        Status = WSKCloseSocketUnsafeDownlevel(reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Listen);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        Status = WSKCloseSocketUnsafeDownlevel(reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Connect);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        ExFreePoolWithTag(Socket, WSK_POOL_TAG);

    } while (false);
#endif

    return Status;
}

static NTSTATUS WSKAPI WSKControlSocketUnsafeDownlevel(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_ WSK_CONTROL_SOCKET_TYPE RequestType,
    _In_ ULONG          ControlCode,
    _In_ ULONG          OptionLevel,
    _In_reads_bytes_opt_(InputSize)     PVOID InputBuffer,
    _In_ SIZE_T         InputSize,
    _Out_writes_bytes_opt_(OutputSize)  PVOID OutputBuffer,
    _In_ SIZE_T         OutputSize,
    _Out_opt_ SIZE_T*   OutputSizeReturned,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        if (RequestType == WskGetOption && OptionLevel == SOL_SOCKET && ControlCode == SO_TYPE)
        {
            if (OutputSize != sizeof(int) || OutputBuffer == nullptr)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (OutputSizeReturned)
            {
                *OutputSizeReturned = OutputSize;
            }

            if (WskSocketType == WSK_FLAG_DATAGRAM_SOCKET)
            {
                *static_cast<int*>(OutputBuffer) = SOCK_DGRAM;
            }
            else
            {
                *static_cast<int*>(OutputBuffer) = SOCK_STREAM;
            }

            Status = STATUS_SUCCESS;
            break;
        }

        if (RequestType == WskIoctl)
        {
            if (ControlCode == SIO_WSK_SET_REMOTE_ADDRESS ||
                ControlCode == SIO_WSK_SET_SENDTO_ADDRESS)
            {
                if (WskSocketType != WSK_FLAG_DATAGRAM_SOCKET)
                {
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
                }

                auto RemoteAddress = static_cast<PSOCKADDR>(InputBuffer);
                if (RemoteAddress == nullptr || InputSize < sizeof SOCKADDR)
                {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }

                NTSTATUS WSKAPI WSKBindUnsafe(
                    _In_ PWSK_SOCKET Socket,
                    _In_ ULONG       WskSocketType,
                    _In_ PSOCKADDR   LocalAddress,
                    _In_ SIZE_T      LocalAddressLength
                );

                SOCKADDR_STORAGE LocalAddress{};
                LocalAddress.ss_family = RemoteAddress->sa_family;

                Status = WSKBindUnsafe(Socket, WskSocketType, reinterpret_cast<PSOCKADDR>(&LocalAddress), sizeof LocalAddress);
                if (!NT_SUCCESS(Status))
                {
                    break;
                }

                // Not break;
            }
        }

        WSKContext = WSKAllocContextIRP(CompletionRoutine, Overlapped);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        auto Dispatch = static_cast<const WSK_PROVIDER_BASIC_DISPATCH*>(Socket->Dispatch);

        Status = Dispatch->WskControlSocket(
            Socket,
            RequestType,
            ControlCode,
            OptionLevel,
            InputSize,
            InputBuffer,
            OutputSize,
            OutputBuffer,
            OutputSizeReturned,
            WSKContext->Irp);

        if (Overlapped == nullptr)
        {
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
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKControlSocketUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_ WSK_CONTROL_SOCKET_TYPE RequestType,
    _In_ ULONG          ControlCode,
    _In_ ULONG          OptionLevel,
    _In_reads_bytes_opt_(InputSize)     PVOID InputBuffer,
    _In_ SIZE_T         InputSize,
    _Out_writes_bytes_opt_(OutputSize)  PVOID OutputBuffer,
    _In_ SIZE_T         OutputSize,
    _Out_opt_ SIZE_T* OutputSizeReturned,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!InterlockedCompareExchange(&_Initialized, true, true))
    {
        Status = STATUS_NDIS_ADAPTER_NOT_READY;
        return Status;
    }

    if (Socket == nullptr)
    {
        Status = STATUS_INVALID_PARAMETER;
        return Status;
    }

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    Status = WSKControlSocketUnsafeDownlevel(Socket, WskSocketType, RequestType, ControlCode, OptionLevel,
        InputBuffer, InputSize, OutputBuffer, OutputSize, OutputSizeReturned, Overlapped, CompletionRoutine);
#else
    do
    {
        if (WskSocketType != WSK_FLAG_STREAM_SOCKET)
        {
            Status = WSKControlSocketUnsafeDownlevel(Socket, WskSocketType, RequestType, ControlCode, OptionLevel,
                InputBuffer, InputSize, OutputBuffer, OutputSize, OutputSizeReturned, Overlapped, CompletionRoutine);
            break;
        }

        if (reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode == 2)
        {
            Socket        = reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Connect;
            WskSocketType = WSK_FLAG_CONNECTION_SOCKET;
        }
        else
        {
            Socket        = reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Listen;
            WskSocketType = WSK_FLAG_LISTEN_SOCKET;
        }

        Status = WSKControlSocketUnsafeDownlevel(Socket, WskSocketType, RequestType, ControlCode, OptionLevel,
            InputBuffer, InputSize, OutputBuffer, OutputSize, OutputSizeReturned, Overlapped, CompletionRoutine);

    } while (false);
#endif

    return Status;
}

static NTSTATUS WSKAPI WSKBindUnsafeDownlevel(
    _In_ PWSK_SOCKET Socket,
    _In_ ULONG       WskSocketType,
    _In_ PSOCKADDR   LocalAddress,
    _In_ SIZE_T      LocalAddressLength
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        UNREFERENCED_PARAMETER(LocalAddressLength);

        PFN_WSK_BIND WSKBindRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_LISTEN_SOCKET:
            WSKBindRoutine = static_cast<const WSK_PROVIDER_LISTEN_DISPATCH*>(Socket->Dispatch)->WskBind;
            break;
        case WSK_FLAG_DATAGRAM_SOCKET:
            WSKBindRoutine = static_cast<const WSK_PROVIDER_DATAGRAM_DISPATCH*>(Socket->Dispatch)->WskBind;
            break;
        case WSK_FLAG_CONNECTION_SOCKET:
            WSKBindRoutine = static_cast<const WSK_PROVIDER_CONNECTION_DISPATCH*>(Socket->Dispatch)->WskBind;
            break;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        case WSK_FLAG_STREAM_SOCKET:
            WSKBindRoutine = static_cast<const WSK_PROVIDER_STREAM_DISPATCH*>(Socket->Dispatch)->WskBind;
            break;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        }

        if (WSKBindRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKBindRoutine(
            Socket,
            LocalAddress,
            0,
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

NTSTATUS WSKAPI WSKBindUnsafe(
    _In_ PWSK_SOCKET Socket,
    _In_ ULONG       WskSocketType,
    _In_ PSOCKADDR   LocalAddress,
    _In_ SIZE_T      LocalAddressLength
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!InterlockedCompareExchange(&_Initialized, true, true))
    {
        Status = STATUS_NDIS_ADAPTER_NOT_READY;
        return Status;
    }

    if (Socket == nullptr || LocalAddress == nullptr || (LocalAddressLength < sizeof SOCKADDR))
    {
        Status = STATUS_INVALID_PARAMETER;
        return Status;
    }

    if ((LocalAddress->sa_family == AF_INET  && LocalAddressLength < sizeof SOCKADDR_IN) ||
        (LocalAddress->sa_family == AF_INET6 && LocalAddressLength < sizeof SOCKADDR_IN6))
    {
        Status = STATUS_INVALID_PARAMETER;
        return Status;
    }

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    Status = WSKBindUnsafeDownlevel(Socket, WskSocketType, LocalAddress, LocalAddressLength);
#else
    if (WskSocketType == WSK_FLAG_STREAM_SOCKET)
    {
        Socket        = reinterpret_cast<PWSK_STREAM_SOCKET_WIN7>(Socket)->Listen;
        WskSocketType = WSK_FLAG_LISTEN_SOCKET;
    }

    Status = WSKBindUnsafeDownlevel(Socket, WskSocketType, LocalAddress, LocalAddressLength);
#endif

    return Status;
}

NTSTATUS WSKAPI WSKAcceptUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _Out_ PWSK_SOCKET*  SocketClient,
    _Out_opt_ PSOCKADDR LocalAddress,
    _In_ SIZE_T         LocalAddressLength,
    _Out_opt_ PSOCKADDR RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        *SocketClient = nullptr;

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

        if ((LocalAddress  && (LocalAddressLength  < sizeof SOCKADDR)) ||
            (RemoteAddress && (RemoteAddressLength < sizeof SOCKADDR)))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

#if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)
        if (WskSocketType == WSK_FLAG_STREAM_SOCKET)
        {
            if (reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode != 1)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Socket        = reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Listen;
            WskSocketType = WSK_FLAG_LISTEN_SOCKET;
        }
#endif // #if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)

        PFN_WSK_ACCEPT WSKAcceptRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_LISTEN_SOCKET:
            WSKAcceptRoutine = static_cast<const WSK_PROVIDER_LISTEN_DISPATCH*>(Socket->Dispatch)->WskAccept;
            break;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        case WSK_FLAG_STREAM_SOCKET:
            WSKAcceptRoutine = static_cast<const WSK_PROVIDER_STREAM_DISPATCH*>(Socket->Dispatch)->WskAccept;
            break;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        }

        if (WSKAcceptRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKAcceptRoutine(
            Socket,
            0,
            nullptr,
            nullptr,
            LocalAddress,
            RemoteAddress,
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

        if (Status == STATUS_SUCCESS)
        {
            *SocketClient = reinterpret_cast<PWSK_SOCKET>(WSKContext->Irp->IoStatus.Information);
        }

        WSKFreeContextIRP(WSKContext);

    } while (false);

    return Status;
}

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
static NTSTATUS WSKAPI WSKListenUnsafeDownlevel(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        PFN_WSK_LISTEN WSKListenRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_STREAM_SOCKET:
            WSKListenRoutine = static_cast<const WSK_PROVIDER_STREAM_DISPATCH*>(Socket->Dispatch)->WskListen;
            break;
        }

        if (WSKListenRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKListenRoutine(
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
#endif

NTSTATUS WSKAPI WSKListenUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (!InterlockedCompareExchange(&_Initialized, true, true))
    {
        Status = STATUS_NDIS_ADAPTER_NOT_READY;
        return Status;
}

    if (Socket == nullptr)
    {
        Status = STATUS_INVALID_PARAMETER;
        return Status;
    }

#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    Status = WSKListenUnsafeDownlevel(Socket, WskSocketType);
#else
    UNREFERENCED_PARAMETER(Socket);
    UNREFERENCED_PARAMETER(WskSocketType);

    if (WskSocketType == WSK_FLAG_STREAM_SOCKET)
    {
        if (reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode != 0)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            return Status;
        }
        else
        {
            reinterpret_cast<WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode = 1;
        }
    }
#endif

    return Status;
}

NTSTATUS WSKAPI WSKConnectUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_ PSOCKADDR      RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength
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

        if (Socket == nullptr || RemoteAddress == nullptr || (RemoteAddressLength < sizeof SOCKADDR))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if ((RemoteAddress->sa_family == AF_INET  && RemoteAddressLength < sizeof SOCKADDR_IN) ||
            (RemoteAddress->sa_family == AF_INET6 && RemoteAddressLength < sizeof SOCKADDR_IN6))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

#if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)
        if (WskSocketType == WSK_FLAG_STREAM_SOCKET)
        {
            if (reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode != 0)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }
            else
            {
                reinterpret_cast<WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode = 2;
            }

            Socket        = reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Connect;
            WskSocketType = WSK_FLAG_CONNECTION_SOCKET;
        }
#endif // #if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)

        SOCKADDR_STORAGE LocalAddress{};
        LocalAddress.ss_family = RemoteAddress->sa_family;

        Status = WSKBindUnsafe(Socket, WskSocketType, reinterpret_cast<PSOCKADDR>(&LocalAddress), sizeof LocalAddress);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        PFN_WSK_CONNECT WSKConnectRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_CONNECTION_SOCKET:
            WSKConnectRoutine = static_cast<const WSK_PROVIDER_CONNECTION_DISPATCH*>(Socket->Dispatch)->WskConnect;
            break;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        case WSK_FLAG_STREAM_SOCKET:
            WSKConnectRoutine = static_cast<const WSK_PROVIDER_STREAM_DISPATCH*>(Socket->Dispatch)->WskConnect;
            break;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        }

        if (WSKConnectRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKConnectRoutine(
            Socket,
            RemoteAddress,
            0,
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

NTSTATUS WSKAPI WSKDisconnectUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_opt_ PWSK_BUF   Buffer,
    _In_ ULONG          Flags
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

#if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)
        if (WskSocketType == WSK_FLAG_STREAM_SOCKET)
        {
            if (reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode != 2)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Socket        = reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Connect;
            WskSocketType = WSK_FLAG_CONNECTION_SOCKET;
        }
#endif // #if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)

        PFN_WSK_DISCONNECT WSKDisconnectRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_CONNECTION_SOCKET:
            WSKDisconnectRoutine = static_cast<const WSK_PROVIDER_CONNECTION_DISPATCH*>(Socket->Dispatch)->WskDisconnect;
            break;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        case WSK_FLAG_STREAM_SOCKET:
            WSKDisconnectRoutine = static_cast<const WSK_PROVIDER_STREAM_DISPATCH*>(Socket->Dispatch)->WskDisconnect;
            break;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        }

        if (WSKDisconnectRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(nullptr, nullptr);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKDisconnectRoutine(
            Socket,
            Buffer,
            Flags,
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

NTSTATUS WSKAPI WSKSendUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesSent,
    _In_ ULONG          Flags,
    _In_opt_ ULONG     TimeoutMilliseconds,
    _In_opt_ WSKOVERLAPPED* Overlapped,
    _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        if (NumberOfBytesSent)
        {
            *NumberOfBytesSent = 0u;
        }

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

#if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)
        if (WskSocketType == WSK_FLAG_STREAM_SOCKET)
        {
            if (reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode != 2)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Socket        = reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Connect;
            WskSocketType = WSK_FLAG_CONNECTION_SOCKET;
        }
#endif // #if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)

        PFN_WSK_SEND WSKSendRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_CONNECTION_SOCKET:
            WSKSendRoutine = static_cast<const WSK_PROVIDER_CONNECTION_DISPATCH*>(Socket->Dispatch)->WskSend;
            break;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        case WSK_FLAG_STREAM_SOCKET:
            WSKSendRoutine = static_cast<const WSK_PROVIDER_STREAM_DISPATCH*>(Socket->Dispatch)->WskSend;
            break;
#endif // if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        }

        if (WSKSendRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(CompletionRoutine, Overlapped, true, Buffer, BufferLength);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKSendRoutine(
            Socket,
            &WSKContext->InputBuffer,
            Flags,
            WSKContext->Irp);

        if (Overlapped == nullptr)
        {
            if (Status == STATUS_PENDING)
            {
                LARGE_INTEGER Timeout{};

                Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                    FALSE, WSKTimeoutToLargeInteger(TimeoutMilliseconds, &Timeout));

                if (Status == STATUS_TIMEOUT)
                {
                    IoCancelIrp(WSKContext->Irp);
                    KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode, FALSE, nullptr);
                }

                if (Status == STATUS_SUCCESS)
                {
                    Status = WSKContext->Irp->IoStatus.Status;
                }
            }

            if (NumberOfBytesSent)
            {
                *NumberOfBytesSent = WSKContext->Irp->IoStatus.Information;
            }

            WSKFreeContextIRP(WSKContext);
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKSendToUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesSent,
    _Reserved_ ULONG    Flags,
    _In_opt_ PSOCKADDR  RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength,
    _In_opt_ ULONG      /*TimeoutMilliseconds*/,
    _In_opt_ WSKOVERLAPPED* Overlapped,
    _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        if (NumberOfBytesSent)
        {
            *NumberOfBytesSent = 0u;
        }

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

        if (RemoteAddress)
        {
            if (RemoteAddressLength < sizeof SOCKADDR)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if ((RemoteAddress->sa_family == AF_INET  && RemoteAddressLength < sizeof SOCKADDR_IN) ||
                (RemoteAddress->sa_family == AF_INET6 && RemoteAddressLength < sizeof SOCKADDR_IN6))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        PFN_WSK_SEND_TO WSKSendToRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_DATAGRAM_SOCKET:
            WSKSendToRoutine = static_cast<const WSK_PROVIDER_DATAGRAM_DISPATCH*>(Socket->Dispatch)->WskSendTo;
            break;
        }

        if (WSKSendToRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        if (Overlapped == nullptr)
        {
            Overlapped = &WSKEmptyOverlapped;
        }

        WSKContext = WSKAllocContextIRP(CompletionRoutine, Overlapped, true, Buffer, BufferLength);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKSendToRoutine(
            Socket,
            &WSKContext->InputBuffer,
            Flags,
            RemoteAddress,
            0,
            nullptr,
            WSKContext->Irp);

        if (NumberOfBytesSent)
        {
            if (Status == STATUS_SUCCESS)
            {
                *NumberOfBytesSent = WSKContext->Irp->IoStatus.Information;
            }
            else
            {
                *NumberOfBytesSent = BufferLength;
            }
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKReceiveUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesRecvd,
    _In_ ULONG          Flags,
    _In_opt_ ULONG      TimeoutMilliseconds,
    _In_opt_ WSKOVERLAPPED* Overlapped,
    _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        if (NumberOfBytesRecvd)
        {
            *NumberOfBytesRecvd = 0;
        }

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

#if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)
        if (WskSocketType == WSK_FLAG_STREAM_SOCKET)
        {
            if (reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Mode != 2)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Socket        = reinterpret_cast<const WSK_STREAM_SOCKET_WIN7*>(Socket)->Connect;
            WskSocketType = WSK_FLAG_CONNECTION_SOCKET;
        }
#endif // #if !(NTDDI_VERSION >= NTDDI_WIN10_RS2)

        PFN_WSK_RECEIVE WSKReceiveRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_CONNECTION_SOCKET:
            WSKReceiveRoutine = static_cast<const WSK_PROVIDER_CONNECTION_DISPATCH*>(Socket->Dispatch)->WskReceive;
            break;
#if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        case WSK_FLAG_STREAM_SOCKET:
            WSKReceiveRoutine = static_cast<const WSK_PROVIDER_STREAM_DISPATCH*>(Socket->Dispatch)->WskReceive;
            break;
#endif // #if (NTDDI_VERSION >= NTDDI_WIN10_RS2)
        }

        if (WSKReceiveRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(CompletionRoutine, Overlapped, true, nullptr, 0, Buffer, BufferLength);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Status = WSKReceiveRoutine(
            Socket,
            &WSKContext->OutputBuffer,
            Flags,
            WSKContext->Irp);

        if (Overlapped == nullptr)
        {
            if (Status == STATUS_PENDING)
            {
                LARGE_INTEGER Timeout{};

                Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                    FALSE, WSKTimeoutToLargeInteger(TimeoutMilliseconds, &Timeout));

                if (Status == STATUS_TIMEOUT)
                {
                    IoCancelIrp(WSKContext->Irp);
                    KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode, FALSE, nullptr);
                }

                if (Status == STATUS_SUCCESS)
                {
                    Status = WSKContext->Irp->IoStatus.Status;
                }
            }

            if (NumberOfBytesRecvd)
            {
                *NumberOfBytesRecvd = WSKContext->Irp->IoStatus.Information;
            }

            WSKFreeContextIRP(WSKContext);
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKReceiveFromUnsafe(
    _In_ PWSK_SOCKET    Socket,
    _In_ ULONG          WskSocketType,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesRecvd,
    _Reserved_ ULONG    Flags,
    _Out_opt_ PSOCKADDR RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength,
    _In_opt_ ULONG      TimeoutMilliseconds,
    _In_opt_ WSKOVERLAPPED* Overlapped,
    _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WSK_CONTEXT_IRP* WSKContext{};

    do
    {
        if (NumberOfBytesRecvd)
        {
            *NumberOfBytesRecvd = 0;
        }

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

        if (RemoteAddress)
        {
            if (RemoteAddressLength < sizeof SOCKADDR)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
        }

        PFN_WSK_RECEIVE_FROM WSKReceiveFromRoutine = nullptr;

        switch (WskSocketType)
        {
        case WSK_FLAG_DATAGRAM_SOCKET:
            WSKReceiveFromRoutine = static_cast<const WSK_PROVIDER_DATAGRAM_DISPATCH*>(Socket->Dispatch)->WskReceiveFrom;
            break;
        }

        if (WSKReceiveFromRoutine == nullptr)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }

        WSKContext = WSKAllocContextIRP(CompletionRoutine, Overlapped, true, nullptr, 0, Buffer, BufferLength);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        ULONG ControlFlags  = 0;
        ULONG ControlLength = 0;

        Status = WSKReceiveFromRoutine(
            Socket,
            &WSKContext->OutputBuffer,
            Flags,
            RemoteAddress,
            &ControlLength,
            nullptr,
            &ControlFlags,
            WSKContext->Irp);

        if (Overlapped == nullptr)
        {
            if (Status == STATUS_PENDING)
            {
                LARGE_INTEGER Timeout{};

                Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                    FALSE, WSKTimeoutToLargeInteger(TimeoutMilliseconds, &Timeout));

                if (Status == STATUS_TIMEOUT)
                {
                    IoCancelIrp(WSKContext->Irp);
                    KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode, FALSE, nullptr);
                }

                if (Status == STATUS_SUCCESS)
                {
                    Status = WSKContext->Irp->IoStatus.Status;
                }
            }

            if (NumberOfBytesRecvd)
            {
                *NumberOfBytesRecvd = WSKContext->Irp->IoStatus.Information;
            }

            WSKFreeContextIRP(WSKContext);
        }

    } while (false);

    return Status;
}

//////////////////////////////////////////////////////////////////////////
// Public  Function

VOID WSKAPI WSKSetLastError(
    _In_ NTSTATUS Status
)
{
    _lasterror = Status;
}

NTSTATUS WSKAPI WSKGetLastError()
{
    return _lasterror;
}

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

        ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

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

        WSKCreateEvent(&WSKEmptyOverlapped.Event);

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

VOID WSKAPI WSKCreateEvent(_Out_ KEVENT* Event)
{
    KeInitializeEvent(Event, NotificationEvent, FALSE);
}

NTSTATUS WSKAPI WSKGetOverlappedResult(
    _In_  SOCKET         Socket,
    _In_  WSKOVERLAPPED* Overlapped,
    _Out_opt_ SIZE_T*    TransferBytes,
    _In_  BOOLEAN        Wait
)
{
    UNREFERENCED_PARAMETER(Socket);

    NTSTATUS Status = STATUS_SUCCESS;

    do 
    {
        if (TransferBytes)
        {
            *TransferBytes = 0u;
        }

        if (Overlapped->Internal == STATUS_PENDING)
        {
            if (!Wait)
            {
                Status = STATUS_TIMEOUT;
                break;
            }

            Status = KeWaitForSingleObject(&Overlapped->Event, Executive, KernelMode, FALSE, nullptr);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        if (TransferBytes)
        {
            /* Return bytes transferred */
            *TransferBytes = Overlapped->InternalHigh;
        }

        /* Check for failure during I/O */
        if (!NT_SUCCESS(Overlapped->Internal))
        {
            /* Set the error and fail */
            Status = static_cast<NTSTATUS>(Overlapped->Internal);
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKGetAddrInfo(
    _In_opt_ LPCWSTR        NodeName,
    _In_opt_ LPCWSTR        ServiceName,
    _In_     UINT32         Namespace,
    _In_opt_ GUID*          Provider,
    _In_opt_ PADDRINFOEXW   Hints,
    _Outptr_result_maybenull_ PADDRINFOEXW* Result,
    _In_opt_ UINT32         TimeoutMilliseconds,
    _In_opt_ WSKOVERLAPPED* Overlapped,
    _In_opt_ LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
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

        WSKContext = WSKAllocContextIRP(CompletionRoutine, Overlapped);
        if (WSKContext == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        // The Context is query result. if block mode.
        auto QueryResult = reinterpret_cast<PADDRINFOEXW*>(&WSKContext->Pointer);
        if (Overlapped != nullptr)
        {
            QueryResult = reinterpret_cast<PADDRINFOEXW*>(&Overlapped->Pointer);
        }

        UNICODE_STRING NodeNameS{};
        UNICODE_STRING ServiceNameS{};

        RtlInitUnicodeString(&NodeNameS, NodeName);
        RtlInitUnicodeString(&ServiceNameS, ServiceName);

        Status = WSKNPIProvider.Dispatch->WskGetAddressInfo(
            WSKNPIProvider.Client,
            NodeName    ? &NodeNameS    : nullptr,
            ServiceName ? &ServiceNameS : nullptr,
            Namespace,
            Provider,
            Hints,
            QueryResult,
            nullptr,
            nullptr,
            WSKContext->Irp);

        if (Overlapped == nullptr)
        {
            if (Status == STATUS_PENDING)
            {
                LARGE_INTEGER Timeout{};

                Status = KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode,
                    FALSE, WSKTimeoutToLargeInteger(TimeoutMilliseconds, &Timeout));

                if (Status == STATUS_TIMEOUT)
                {
                    IoCancelIrp(WSKContext->Irp);
                    KeWaitForSingleObject(&WSKContext->Event, Executive, KernelMode, FALSE, nullptr);
                }

                if (Status == STATUS_SUCCESS)
                {
                    Status = WSKContext->Irp->IoStatus.Status;
                }
            }

            *Result = static_cast<PADDRINFOEXW>(WSKContext->Pointer);

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
    _In_ const SOCKADDR* Address,
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

        if (NodeName)
        {
            RtlInitEmptyUnicodeString(&NodeNameS, NodeName, static_cast<USHORT>(NodeNameSize));
        }
        if (ServiceName)
        {
            RtlInitEmptyUnicodeString(&ServiceNameS, ServiceName, static_cast<USHORT>(ServiceNameSize));
        }

        Status = WSKNPIProvider.Dispatch->WskGetNameInfo(
            WSKNPIProvider.Client,
            const_cast<PSOCKADDR>(Address),
            AddressLength,
            NodeName    ? &NodeNameS    : nullptr,
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
            WSKCloseSocketUnsafe(Socket_, WSKSocketType);
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKCloseSocketUnsafe(SocketObject.Socket, SocketObject.SocketType);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        WSKSocketsAVLTableDelete(Socket);

    } while (false);

    return Status;
}

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
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    do
    {
        if (OutputSizeReturned)
        {
            *OutputSizeReturned = 0u;
        }

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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKControlSocketUnsafe(SocketObject.Socket, SocketObject.SocketType, WskIoctl, ControlCode, 0,
            InputBuffer, InputSize, OutputBuffer, OutputSize, OutputSizeReturned, Overlapped, CompletionRoutine);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKSetSocketOpt(
    _In_ SOCKET         Socket,
    _In_ ULONG          OptionLevel,
    _In_ ULONG          OptionName,
    _In_reads_bytes_(InputSize) PVOID InputBuffer,
    _In_ SIZE_T         InputSize
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        if (OptionLevel == SOL_SOCKET && (OptionName == SO_SNDTIMEO || OptionName == SO_RCVTIMEO))
        {
            if (InputSize != sizeof(ULONG) || InputBuffer == nullptr)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            ULONG* Timeout = nullptr;

            if (OptionName == SO_SNDTIMEO)
            {
                Timeout = &SocketObject.SendTimeout;
            }
            if (OptionName == SO_RCVTIMEO)
            {
                Timeout = &SocketObject.RecvTimeout;
            }

            *Timeout = *static_cast<ULONG*>(InputBuffer);

            if (!WSKSocketsAVLTableUpdate(Socket, &SocketObject))
            {
                Status = STATUS_UNSUCCESSFUL;
            }

            break;
        }

        Status = WSKControlSocketUnsafe(SocketObject.Socket, SocketObject.SocketType, WskSetOption,
            OptionName, OptionLevel, InputBuffer, InputSize, nullptr, 0, nullptr, nullptr, nullptr);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKGetSocketOpt(
    _In_ SOCKET         Socket,
    _In_ ULONG          OptionLevel,
    _In_ ULONG          OptionName,
    _Out_writes_bytes_(*OutputSize) PVOID OutputBuffer,
    _Inout_ SIZE_T* OutputSize
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        if (OptionLevel == SOL_SOCKET && (OptionName == SO_SNDTIMEO || OptionName == SO_RCVTIMEO))
        {
            if (*OutputSize != sizeof(ULONG) || OutputBuffer == nullptr)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (OptionName == SO_SNDTIMEO)
            {
                *static_cast<ULONG*>(OutputBuffer) = SocketObject.SendTimeout;
            }
            if (OptionName == SO_RCVTIMEO)
            {
                *static_cast<ULONG*>(OutputBuffer) = SocketObject.RecvTimeout;
            }

            *OutputSize = sizeof ULONG;
            break;
        }

        Status = WSKControlSocketUnsafe(SocketObject.Socket, SocketObject.SocketType, WskGetOption,
            OptionName, OptionLevel, nullptr, 0, OutputBuffer, *OutputSize, OutputSize, nullptr, nullptr);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKBind(
    _In_ SOCKET         Socket,
    _In_ PSOCKADDR      LocalAddress,
    _In_ SIZE_T         LocalAddressLength
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKBindUnsafe(SocketObject.Socket, SocketObject.SocketType, LocalAddress, LocalAddressLength);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKAccpet(
    _In_  SOCKET        Socket,
    _Out_ SOCKET*       SocketClient,
    _Out_opt_ PSOCKADDR LocalAddress,
    _In_ SIZE_T         LocalAddressLength,
    _Out_opt_ PSOCKADDR RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        PWSK_SOCKET SocketClient_ = nullptr;

        Status = WSKAcceptUnsafe(SocketObject.Socket, SocketObject.SocketType, &SocketClient_,
            LocalAddress, LocalAddressLength, RemoteAddress, RemoteAddressLength);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        if (!WSKSocketsAVLTableInsert(SocketClient, SocketClient_, static_cast<USHORT>(WSK_FLAG_CONNECTION_SOCKET)))
        {
            WSKCloseSocketUnsafe(SocketClient_, WSK_FLAG_CONNECTION_SOCKET);
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKListen(
    _In_ SOCKET         Socket,
    _In_ INT            BackLog
)
{
    UNREFERENCED_PARAMETER(BackLog);

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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKListenUnsafe(SocketObject.Socket, SocketObject.SocketType);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKConnect(
    _In_ SOCKET         Socket,
    _In_ PSOCKADDR      RemoteAddress,
    _In_ SIZE_T         RemoteAddressLength
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKConnectUnsafe(SocketObject.Socket, SocketObject.SocketType, RemoteAddress, RemoteAddressLength);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKDisconnect(
    _In_ SOCKET         Socket,
    _In_ ULONG          Flags
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKDisconnectUnsafe(SocketObject.Socket, SocketObject.SocketType, nullptr, Flags);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKSend(
    _In_ SOCKET Socket,
    _In_ PVOID  Buffer,
    _In_ SIZE_T BufferLength,
    _Out_opt_ SIZE_T* NumberOfBytesSent,
    _In_ ULONG  Flags,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKSendUnsafe(SocketObject.Socket, SocketObject.SocketType, Buffer, BufferLength,
            NumberOfBytesSent, Flags, SocketObject.SendTimeout, Overlapped, CompletionRoutine);

    } while (false);

    return Status;
}

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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKSendToUnsafe(SocketObject.Socket, SocketObject.SocketType, Buffer, BufferLength,
            NumberOfBytesSent, Flags, RemoteAddress, RemoteAddressLength, SocketObject.SendTimeout,
            Overlapped, CompletionRoutine);

    } while (false);

    return Status;
}

NTSTATUS WSKAPI WSKReceive(
    _In_ SOCKET         Socket,
    _In_ PVOID          Buffer,
    _In_ SIZE_T         BufferLength,
    _Out_opt_ SIZE_T*   NumberOfBytesRecvd,
    _In_ ULONG          Flags,
    _In_opt_  WSKOVERLAPPED* Overlapped,
    _In_opt_  LPWSKOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine
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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKReceiveUnsafe(SocketObject.Socket, SocketObject.SocketType, Buffer, BufferLength,
            NumberOfBytesRecvd, Flags, SocketObject.RecvTimeout, Overlapped, CompletionRoutine);

    } while (false);

    return Status;
}

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

        SOCKET_OBJECT SocketObject{};

        if (!WSKSocketsAVLTableFind(Socket, &SocketObject))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (SocketObject.SocketType == static_cast<USHORT>(WSK_FLAG_INVALID_SOCKET))
        {
            Status = STATUS_NOT_SUPPORTED;
            break;
        }

        Status = WSKReceiveFromUnsafe(SocketObject.Socket, SocketObject.SocketType, Buffer, BufferLength,
            NumberOfBytesRecvd, Flags, RemoteAddress, RemoteAddressLength, SocketObject.RecvTimeout,
            Overlapped, CompletionRoutine);

    } while (false);

    return Status;
}
