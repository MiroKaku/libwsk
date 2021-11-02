#include <stdlib.h>
#include <ntddk.h>
#include <wdm.h>
#include <ntstrsafe.h>
#include "src\libwsk.h"

EXTERN_C_START
DRIVER_INITIALIZE   DriverEntry;
DRIVER_UNLOAD       DriverUnload;
EXTERN_C_END

namespace unittest
{
    NTSTATUS StartWSKServer(
        _In_opt_ LPCWSTR NodeName,
        _In_opt_ LPCWSTR ServiceName,
        _In_     ADDRESS_FAMILY AddressFamily,
        _In_     USHORT  SocketType
    );

    VOID CloseWSKServer();

    NTSTATUS StartWSKClient(
        _In_opt_ LPCWSTR NodeName,
        _In_opt_ LPCWSTR ServiceName,
        _In_     ADDRESS_FAMILY AddressFamily,
        _In_     USHORT  SocketType
    );

    VOID CloseWSKClient();
}

NTSTATUS DriverEntry(_In_ DRIVER_OBJECT* DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS Status = STATUS_SUCCESS;

    do 
    {
        DriverObject->DriverUnload = DriverUnload;

        WSKDATA WSKData{};
        Status = WSKStartup(MAKE_WSK_VERSION(1, 0), &WSKData);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        Status = unittest::StartWSKServer(nullptr, L"20211", AF_INET, SOCK_STREAM);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        Status = unittest::StartWSKClient(nullptr, L"20211", AF_INET, SOCK_STREAM);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

    } while (false);

    if (!NT_SUCCESS(Status))
    {
        DriverUnload(DriverObject);
    }

    return Status;
}

VOID DriverUnload(_In_ DRIVER_OBJECT* DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    unittest::CloseWSKClient();
    unittest::CloseWSKServer();

    WSKCleanup();
}

namespace unittest
{
    constexpr ULONG  POOL_TAG = 'TSET'; // TEST
    constexpr size_t DEFAULT_BUFFER_LEN = PAGE_SIZE;

    SIZE_T    SocketCount   = 0u;
    SOCKET*   ServerSockets = nullptr;
    PETHREAD* ServerThreads = nullptr;

    SOCKET    ClientSocket = WSK_INVALID_SOCKET;
    PETHREAD  ClientThread = nullptr;

    NTSTATUS WSKServerThread(
        _In_ SOCKET Socket
    )
    {
        NTSTATUS Status = STATUS_SUCCESS;

        PVOID  Buffer   = nullptr;
        LPWSTR HostName = nullptr;
        LPWSTR PortName = nullptr;
        SOCKET SocketClient = WSK_INVALID_SOCKET;

        do 
        {
            SIZE_T Bytes        = 0u;
            INT    SocketType   = 0;

            Bytes  = sizeof SocketType;
            Status = WSKGetSocketOpt(Socket, SOL_SOCKET, SO_TYPE, &SocketType, &Bytes);
            if (!NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] WSKGetSocketOpt(SO_TYPE) failed: 0x%08X.\n",
                    Status);

                break;
            }

            Buffer = ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                DEFAULT_BUFFER_LEN, POOL_TAG);
            if (Buffer == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] ExAllocatePoolWithTag(Buffer) failed.\n");

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            HostName = (LPWSTR)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                NI_MAXHOST * sizeof WCHAR, POOL_TAG);
            PortName = (LPWSTR)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                NI_MAXSERV * sizeof WCHAR, POOL_TAG);

            if (HostName == nullptr || PortName == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] ExAllocatePoolWithTag(Name) failed.\n");

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            SOCKADDR_STORAGE FromAddress{};

            do
            {
                // TCP
                if (SocketType == SOCK_STREAM)
                {
                    if (SocketClient == WSK_INVALID_SOCKET)
                    {
                        Status = WSKAccpet(Socket, &SocketClient, nullptr, 0u, (SOCKADDR*)&FromAddress, sizeof FromAddress);
                        if (!NT_SUCCESS(Status))
                        {
                            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                                "[WSK] [Server] WSKAccpet failed: 0x%08X.\n",
                                Status);

                            break;
                        }

                        Status = WSKGetNameInfo((SOCKADDR*)&FromAddress, sizeof FromAddress,
                            HostName, NI_MAXHOST, PortName, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
                        if (!NT_SUCCESS(Status))
                        {
                            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                                "[WSK] [Server] WSKGetNameInfo failed: 0x%08X.\n",
                                Status);

                            break;
                        }

                        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                            "[WSK] [Server] Accepted connection from host %ls and port %ls.\n",
                            HostName, PortName);
                    }
                    else
                    {
                        Status = WSKReceive(SocketClient, Buffer, DEFAULT_BUFFER_LEN, &Bytes, 0);
                        if (!NT_SUCCESS(Status))
                        {
                            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                                "[WSK] [Server] WSKReceive failed: 0x%08X.\n",
                                Status);

                            break;
                        }

                        if (Bytes == 0u)
                        {
                            WSKCloseSocket(SocketClient);
                            SocketClient = WSK_INVALID_SOCKET;
                        }
                        else
                        {
                            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                                "[WSK] [Server] Read %Id bytes.\n",
                                Bytes);

                            Status = WSKSend(SocketClient, Buffer, Bytes, &Bytes, 0);
                            if (!NT_SUCCESS(Status))
                            {
                                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                                    "[WSK] [Server] WSKSend failed: 0x%08X.\n",
                                    Status);

                                break;
                            }

                            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                                "[WSK] [Server] Wrote %Id bytes.\n",
                                Bytes);
                        }
                    }
                }

                // UDP
                if (SocketType == SOCK_DGRAM)
                {
                    Status = WSKReceiveFrom(Socket, Buffer, DEFAULT_BUFFER_LEN, &Bytes, 0,
                        (SOCKADDR*)&FromAddress, sizeof FromAddress);
                    if (!NT_SUCCESS(Status))
                    {
                        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                            "[WSK] [Server] WSKReceiveFrom failed: 0x%08X.\n",
                            Status);

                        break;
                    }

                    Status = WSKGetNameInfo((SOCKADDR*)&FromAddress, sizeof FromAddress,
                        HostName, NI_MAXHOST, PortName, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
                    if (!NT_SUCCESS(Status))
                    {
                        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                            "[WSK] [Server] WSKGetNameInfo failed: 0x%08X.\n",
                            Status);

                        break;
                    }

                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, 
                        "[WSK] [Server] Read %Id bytes from host %ls and port %ls.\n",
                        Bytes, HostName, PortName);

                    Status = WSKSendTo(Socket, Buffer, Bytes, &Bytes, 0,
                        (SOCKADDR*)&FromAddress, sizeof FromAddress);
                    if (!NT_SUCCESS(Status))
                    {
                        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                            "[WSK] [Server] WSKSendTo failed: 0x%08X.\n",
                            Status);

                        break;
                    }

                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, 
                        "[WSK] [Server] Sent %Id bytes to host %ls and port %ls.\n",
                        Bytes, HostName, PortName);
                }

            } while (true);

        } while (false);

        if (SocketClient != WSK_INVALID_SOCKET)
        {
            WSKCloseSocket(SocketClient);
        }

        if (HostName)
        {
            ExFreePoolWithTag(HostName, POOL_TAG);
        }

        if (PortName)
        {
            ExFreePoolWithTag(PortName, POOL_TAG);
        }

        if (Buffer)
        {
            ExFreePoolWithTag(Buffer, POOL_TAG);
        }

        return Status;
    }

    NTSTATUS StartWSKServer(
        _In_opt_ LPCWSTR NodeName,
        _In_opt_ LPCWSTR ServiceName,
        _In_     ADDRESS_FAMILY AddressFamily,
        _In_     USHORT  SocketType
    )
    {
        NTSTATUS Status = STATUS_SUCCESS;

        LPWSTR HostName = nullptr;
        LPWSTR PortName = nullptr;
        PADDRINFOEXW AddrInfo = nullptr;

        do 
        {
            HostName = (LPWSTR)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                NI_MAXHOST * sizeof WCHAR, POOL_TAG);
            PortName = (LPWSTR)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                NI_MAXSERV * sizeof WCHAR, POOL_TAG);

            if (HostName == nullptr || PortName == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] ExAllocatePoolWithTag(Name) failed.\n");

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ADDRINFOEXW Hints{};
            Hints.ai_family   = AddressFamily;
            Hints.ai_socktype = SocketType;
            Hints.ai_protocol = ((SocketType == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP);
            Hints.ai_flags    = ((NodeName == NULL) ? AI_PASSIVE : 0);

            Status = WSKGetAddrInfo(NodeName, ServiceName, NS_ALL, nullptr,
                &Hints, &AddrInfo, WSK_INFINITE_WAIT, nullptr);
            if (!NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, 
                    "[WSK] [Server] WSKGetAddrInfo failed: 0x%08X.\n",
                    Status);
                break;
            }

            // Make sure we got at least one address back
            if (AddrInfo == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] Unable to resolve node %ls.\n",
                    NodeName);

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            // Count how many addresses were returned
            for (auto Addr = AddrInfo; Addr; Addr = Addr->ai_next)
            {
                ++SocketCount;
            }

            ServerSockets = (SOCKET*)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                SocketCount * sizeof SOCKET, POOL_TAG);
            if (ServerSockets == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] ExAllocatePoolWithTag(Sockets) failed.\n");

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            for (auto i = 0u; i < SocketCount; ++i)
            {
                ServerSockets[i] = WSK_INVALID_SOCKET;
            }

            size_t Index = 0u;
            for (auto Addr = AddrInfo; Addr; Addr = Addr->ai_next)
            {
                Status = WSKSocket(&ServerSockets[Index], static_cast<ADDRESS_FAMILY>(Addr->ai_family),
                    static_cast<USHORT>(Addr->ai_socktype), Addr->ai_protocol, nullptr);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[WSK] [Server] WSKSocket failed: 0x%08X.\n",
                        Status);

                    break;
                }

                Status = WSKBind(ServerSockets[Index], Addr->ai_addr, Addr->ai_addrlen);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[WSK] [Server] WSKBind failed: 0x%08X.\n",
                        Status);

                    break;
                }

                if (Addr->ai_socktype == SOCK_STREAM)
                {
                    Status = WSKListen(ServerSockets[Index], 128);
                    if (!NT_SUCCESS(Status))
                    {
                        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                            "[WSK] [Server] WSKListen failed: 0x%08X.\n",
                            Status);

                        break;
                    }
                }

                Status = WSKGetNameInfo(Addr->ai_addr, static_cast<ULONG>(Addr->ai_addrlen),
                    HostName, NI_MAXHOST, PortName, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[WSK] [Server] WSKGetNameInfo failed: 0x%08X.\n",
                        Status);

                    break;
                }

                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] Socket 0x%IX bound to address %ls and port %ls.\n",
                    ServerSockets[Index], HostName, PortName);

                ++Index;
            }
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            ServerThreads = (PETHREAD*)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                SocketCount * sizeof PETHREAD, POOL_TAG);
            if (ServerThreads == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Server] ExAllocatePoolWithTag(Threads) failed.\n");

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            for (auto i = 0u; i < SocketCount; ++i)
            {
                HANDLE ThreadHandle = nullptr;

                Status = PsCreateSystemThread(&ThreadHandle, SYNCHRONIZE,
                    nullptr, nullptr, nullptr,
                    [](PVOID Context) { PsTerminateSystemThread(WSKServerThread(reinterpret_cast<SOCKET>(Context))); },
                    reinterpret_cast<PVOID>(ServerSockets[i]));
                if (!NT_SUCCESS(Status))
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[WSK] [Server] PsCreateSystemThread(%d) failed: 0x%08X.\n",
                        i, Status);

                    break;
                }

                Status = ObReferenceObjectByHandleWithTag(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode,
                    POOL_TAG, reinterpret_cast<PVOID*>(&ServerThreads[i]), nullptr);

                ZwClose(ThreadHandle);

                if (!NT_SUCCESS(Status))
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[WSK] [Server] ObReferenceObjectByHandleWithTag(%d) failed: 0x%08X.\n",
                        i, Status);

                    break;
                }
            }

        } while (false);

        if (HostName)
        {
            ExFreePoolWithTag(HostName, POOL_TAG);
        }

        if (PortName)
        {
            ExFreePoolWithTag(PortName, POOL_TAG);
        }

        if (AddrInfo)
        {
            WSKFreeAddrInfo(AddrInfo);
        }

        if (!NT_SUCCESS(Status))
        {
            CloseWSKServer();
        }

        return Status;
    }

    VOID CloseWSKServer()
    {
        if (ServerSockets)
        {
            for (auto i = 0u; i < SocketCount; ++i)
            {
                if (ServerSockets[i] != WSK_INVALID_SOCKET)
                {
                    WSKCloseSocket(ServerSockets[i]);
                }
            }

            ExFreePoolWithTag(ServerSockets, POOL_TAG);

            ServerSockets = nullptr;
        }

        if (ServerThreads)
        {
            KeWaitForMultipleObjects(static_cast<ULONG>(SocketCount), reinterpret_cast<PVOID*>(ServerThreads),
                WaitAll, Executive, KernelMode, FALSE, nullptr, nullptr);

            for (auto i = 0u; i < SocketCount; ++i)
            {
                if (ServerThreads[i])
                {
                    ObDereferenceObjectWithTag(ServerThreads[i], POOL_TAG);
                }
            }

            ExFreePoolWithTag(ServerThreads, POOL_TAG);

            ServerThreads = nullptr;
        }
    }

    NTSTATUS WSKClientThread(
        _In_ SOCKET Socket
    )
    {
        NTSTATUS Status = STATUS_SUCCESS;

        PVOID  Buffer    = nullptr;
        SIZE_T LoopCount = 0u;

        Buffer = ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
            DEFAULT_BUFFER_LEN, POOL_TAG);
        if (Buffer == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            return Status;
        }

        do
        {
            SIZE_T Bytes = 0u;

            RtlStringCbPrintfA(static_cast<LPSTR>(Buffer), DEFAULT_BUFFER_LEN,
                "This is a small test message [number %Id].",
                LoopCount++);

            Status = WSKSend(Socket, Buffer, DEFAULT_BUFFER_LEN, &Bytes, 0);
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[WSK] [Client] Wrote %Id bytes.\n",
                Bytes);

            Status = WSKReceive(Socket, Buffer, DEFAULT_BUFFER_LEN, &Bytes, 0);
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            if (Bytes == 0)
            {
                break;
            }

            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[WSK] [Client] Read %Id bytes, data [%s] from server.\n",
                Bytes, static_cast<LPCSTR>(Buffer));

        } while (true);

        if (Buffer)
        {
            ExFreePoolWithTag(Buffer, POOL_TAG);
        }

        return Status;
    }

    NTSTATUS StartWSKClient(
        _In_opt_ LPCWSTR NodeName,
        _In_opt_ LPCWSTR ServiceName,
        _In_     ADDRESS_FAMILY AddressFamily,
        _In_     USHORT  SocketType
    )
    {
        NTSTATUS Status = STATUS_SUCCESS;

        LPWSTR HostName = nullptr;
        LPWSTR PortName = nullptr;
        PADDRINFOEXW AddrInfo = nullptr;

        do
        {
            HostName = (LPWSTR)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                NI_MAXHOST * sizeof WCHAR, POOL_TAG);
            PortName = (LPWSTR)ExAllocatePoolWithTag(static_cast<POOL_TYPE>(static_cast<int>(PagedPool | POOL_ZERO_ALLOCATION)),
                NI_MAXSERV * sizeof WCHAR, POOL_TAG);

            if (HostName == nullptr || PortName == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Client] ExAllocatePoolWithTag(Name) failed.\n");

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ADDRINFOEXW Hints{};
            Hints.ai_family   = AddressFamily;
            Hints.ai_socktype = SocketType;
            Hints.ai_protocol = ((SocketType == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP);

            Status = WSKGetAddrInfo(NodeName, ServiceName, NS_ALL, nullptr,
                &Hints, &AddrInfo, WSK_INFINITE_WAIT, nullptr);
            if (!NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Client] WSKGetAddrInfo failed: 0x%08X.\n",
                    Status);

                break;
            }

            // Make sure we got at least one address back
            if (AddrInfo == nullptr)
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Client] Server (%ls) name could not be resolved!\n",
                    NodeName);

                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            for (auto Addr = AddrInfo; Addr; Addr = Addr->ai_next)
            {
                Status = WSKSocket(&ClientSocket, static_cast<ADDRESS_FAMILY>(Addr->ai_family),
                    static_cast<USHORT>(Addr->ai_socktype), Addr->ai_protocol, nullptr);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[WSK] [Client] WSKSocket failed: 0x%08X.\n",
                        Status);

                    break;
                }

                Status = WSKGetNameInfo(Addr->ai_addr, static_cast<ULONG>(Addr->ai_addrlen),
                    HostName, NI_MAXHOST, PortName, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "[WSK] [Client] WSKGetNameInfo failed: 0x%08X.\n",
                        Status);

                    break;
                }

                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Client] Client attempting connection to: %ls port: %ls.\n",
                    HostName, PortName);

                Status = WSKConnect(ClientSocket, Addr->ai_addr, Addr->ai_addrlen);
                if (NT_SUCCESS(Status))
                {
                    break;
                }

                WSKCloseSocket(ClientSocket);
                ClientSocket = WSK_INVALID_SOCKET;
            }

            if (!NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Client] Unable to establish connection... 0x%08X.\n",
                    Status);

                break;
            }

            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "[WSK] [Client] Connection established...\n");

            HANDLE ThreadHandle = nullptr;

            Status = PsCreateSystemThread(&ThreadHandle, SYNCHRONIZE,
                nullptr, nullptr, nullptr,
                [](PVOID Context) { PsTerminateSystemThread(WSKClientThread(reinterpret_cast<SOCKET>(Context))); },
                reinterpret_cast<PVOID>(ClientSocket));
            if (!NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Client] PsCreateSystemThread failed: 0x%08X.\n",
                    Status);

                break;
            }

            Status = ObReferenceObjectByHandleWithTag(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode,
                POOL_TAG, reinterpret_cast<PVOID*>(&ClientThread), nullptr);

            ZwClose(ThreadHandle);

            if (!NT_SUCCESS(Status))
            {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "[WSK] [Client] ObReferenceObjectByHandleWithTag failed: 0x%08X.\n",
                    Status);

                break;
            }

        } while (false);

        if (HostName)
        {
            ExFreePoolWithTag(HostName, POOL_TAG);
        }

        if (PortName)
        {
            ExFreePoolWithTag(PortName, POOL_TAG);
        }

        if (AddrInfo)
        {
            WSKFreeAddrInfo(AddrInfo);
        }

        if (!NT_SUCCESS(Status))
        {
            CloseWSKClient();
        }

        return Status;
    }

    VOID CloseWSKClient()
    {
        if (ClientSocket != WSK_INVALID_SOCKET)
        {
            WSKCloseSocket(ClientSocket);

            ClientSocket = WSK_INVALID_SOCKET;
        }

        if (ClientThread)
        {
            KeWaitForSingleObject(ClientThread, Executive, KernelMode, FALSE, nullptr);
            ObDereferenceObjectWithTag(ClientThread, POOL_TAG);

            ClientThread = nullptr;
        }
    }
}
