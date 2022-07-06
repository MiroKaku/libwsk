#include "universal.h"
#include "berkeley.h"
#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif

SOCKET WSKAPI socket(
    _In_ int af,
    _In_ int type,
    _In_ int protocol
)
{
    SOCKET   Socket = WSK_INVALID_SOCKET;
    NTSTATUS Status = WSKSocket(&Socket, static_cast<ADDRESS_FAMILY>(af),
        static_cast<USHORT>(type), static_cast<ULONG>(protocol), nullptr);

    return WSKSetLastError(Status), Socket;
}

int WSKAPI closesocket(
    _In_ SOCKET s
)
{
    NTSTATUS Status = WSKCloseSocket(s);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : SOCKET_SUCCESS);
}

int WSKAPI bind(
    _In_ SOCKET s,
    _In_reads_bytes_(addrlen) const struct sockaddr* addr,
    _In_ int addrlen
)
{
    NTSTATUS Status = WSKBind(s, (PSOCKADDR)addr, addrlen);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : SOCKET_SUCCESS);
}

int WSKAPI listen(
    _In_ SOCKET s,
    _In_ int backlog
)
{
    NTSTATUS Status = WSKListen(s, backlog);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : SOCKET_SUCCESS);

}

int WSKAPI connect(
    _In_ SOCKET s,
    _In_reads_bytes_(addrlen) const struct sockaddr* addr,
    _In_ int addrlen
)
{
    NTSTATUS Status = WSKConnect(s, (PSOCKADDR)addr, addrlen);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : SOCKET_SUCCESS);
}

int WSKAPI shutdown(
    _In_ SOCKET s,
    _In_ int how
)
{
    UNREFERENCED_PARAMETER(how);

    NTSTATUS Status = WSKDisconnect(s, 0);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : SOCKET_SUCCESS);
}

SOCKET WSKAPI accept(
    _In_ SOCKET s,
    _Out_writes_bytes_opt_(*addrlen) struct sockaddr* addr,
    _Inout_opt_ int* addrlen
)
{
    SOCKET   Socket = WSK_INVALID_SOCKET;
    NTSTATUS Status = WSKAccpet(s, &Socket, nullptr, 0, addr, addrlen ? *addrlen : 0);
    return WSKSetLastError(Status), Socket;
}

int WSKAPI send(
    _In_ SOCKET s,
    _In_reads_bytes_(len) const char* buf,
    _In_ int len,
    _In_ int flags
)
{
    SIZE_T NumberOfBytesSent = 0u;

    NTSTATUS Status = WSKSend(s, const_cast<char*>(buf), len, &NumberOfBytesSent, flags, nullptr, nullptr);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : static_cast<int>(NumberOfBytesSent));
}

int WSKAPI recv(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char* buf,
    _In_ int len,
    _In_ int flags
)
{
    SIZE_T NumberOfBytesRecvd = 0u;

    NTSTATUS Status = WSKReceive(s, buf, len, &NumberOfBytesRecvd, flags, nullptr, nullptr);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : static_cast<int>(NumberOfBytesRecvd));
}

int WSKAPI sendto(
    _In_ SOCKET s,
    _In_reads_bytes_(len) const char* buf,
    _In_ int len,
    _In_ int flags,
    _In_reads_bytes_opt_(tolen) const struct sockaddr* to,
    _In_ int tolen
)
{
    SIZE_T NumberOfBytesSent = 0u;

    NTSTATUS Status = WSKSendTo(s, const_cast<char*>(buf), len, &NumberOfBytesSent, flags, const_cast<PSOCKADDR>(to), tolen, nullptr, nullptr);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : static_cast<int>(NumberOfBytesSent));
}

int WSKAPI recvfrom(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char* buf,
    _In_ int len,
    _In_ int flags,
    _Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr* from,
    _Inout_opt_ int* fromlen
)
{
    SIZE_T NumberOfBytesRecvd = 0u;

    NTSTATUS Status = WSKReceiveFrom(s, buf, len, &NumberOfBytesRecvd, flags, const_cast<PSOCKADDR>(from), fromlen ? *fromlen : 0, nullptr, nullptr);
    if (NT_SUCCESS(Status))
    {
        if (fromlen)
        {
            *fromlen = static_cast<int>(NumberOfBytesRecvd);
        }
    }

    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : static_cast<int>(NumberOfBytesRecvd));
}

int WSKAPI setsockopt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _In_reads_bytes_opt_(optlen) const char* optval,
    _In_ int optlen
)
{
    NTSTATUS Status = WSKSetSocketOpt(s, level, optname, const_cast<char*>(optval), optlen);
    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : SOCKET_SUCCESS);
}

int WSKAPI getsockopt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _Out_writes_bytes_(*optlen) char* optval,
    _Inout_ int* optlen
)
{
    SIZE_T OptionLength = *optlen;

    NTSTATUS Status = WSKGetSocketOpt(s, level, optname, optval, &OptionLength);
    if (NT_SUCCESS(Status))
    {
        *optlen = static_cast<int>(OptionLength);
    }

    return WSKSetLastError(Status), (!NT_SUCCESS(Status) ? SOCKET_ERROR : SOCKET_SUCCESS);
}

static NTSTATUS WSKAPI convert_addrinfo_to_addrinfoex(
    _In_ addrinfoexW**      target,
    _In_opt_ const addrinfo*    source
)
{
    NTSTATUS     Status  = STATUS_SUCCESS;
    addrinfoexW* Result  = nullptr;
    sockaddr*    Address = nullptr;

    ANSI_STRING     CanonicalNameA{};
    UNICODE_STRING  CanonicalNameW{};

    do
    {
        *target = nullptr;

        if (source == nullptr)
        {
            break;
        }

        Result = (addrinfoexW*)ExAllocatePoolZero(PagedPool, sizeof addrinfoexW, WSK_POOL_TAG);
        if (Result == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Result->ai_flags    = (source->ai_flags & ~AI_EXTENDED);
        Result->ai_family   = source->ai_family;
        Result->ai_socktype = source->ai_socktype;
        Result->ai_protocol = source->ai_protocol;
        Result->ai_addrlen  = source->ai_addrlen;

        if (source->ai_canonname)
        {
            RtlInitAnsiString(&CanonicalNameA, source->ai_canonname);

            Status = RtlAnsiStringToUnicodeString(&CanonicalNameW, &CanonicalNameA, TRUE);
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            Result->ai_canonname = CanonicalNameW.Buffer;
        }

        if (source->ai_addr)
        {
            Address = (sockaddr*)ExAllocatePoolZero(PagedPool, source->ai_addrlen, WSK_POOL_TAG);
            if (Address == nullptr)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            Result->ai_addr = Address;
        }

        Status = convert_addrinfo_to_addrinfoex(&Result->ai_next, source->ai_next);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        *target = Result;

    } while (false);

    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&CanonicalNameW);

        if (Address)
        {
            ExFreePoolWithTag(Address, WSK_POOL_TAG);
        }

        if (Result)
        {
            ExFreePoolWithTag(Result, WSK_POOL_TAG);
        }
    }

    return Status;
}

static NTSTATUS WSKAPI convert_addrinfoex_to_addrinfo(
    _In_ addrinfo**         target,
    _In_opt_ const addrinfoexW* source
)
{
    NTSTATUS        Status  = STATUS_SUCCESS;
    addrinfo*       Result  = nullptr;
    sockaddr*       Address = nullptr;

    ANSI_STRING     CanonicalNameA{};
    UNICODE_STRING  CanonicalNameW{};

    do
    {
        *target = nullptr;

        if (source == nullptr)
        {
            break;
        }

        Result = (addrinfo*)ExAllocatePoolZero(PagedPool, sizeof addrinfo, WSK_POOL_TAG);
        if (Result == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Result->ai_flags    = (source->ai_flags & ~AI_EXTENDED);
        Result->ai_family   = source->ai_family;
        Result->ai_socktype = source->ai_socktype;
        Result->ai_protocol = source->ai_protocol;
        Result->ai_addrlen  = source->ai_addrlen;

        if (source->ai_canonname)
        {
            RtlInitUnicodeString(&CanonicalNameW, source->ai_canonname);

            Status = RtlUnicodeStringToAnsiString(&CanonicalNameA, &CanonicalNameW, TRUE);
            if (!NT_SUCCESS(Status))
            {
                break;
            }

            Result->ai_canonname = CanonicalNameA.Buffer;
        }

        if (source->ai_addr)
        {
            Address = (sockaddr*)ExAllocatePoolZero(PagedPool, source->ai_addrlen, WSK_POOL_TAG);
            if (Address == nullptr)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            Result->ai_addr = Address;
        }

        Status = convert_addrinfoex_to_addrinfo(&Result->ai_next, source->ai_next);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        *target = Result;

    } while (false);

    if (!NT_SUCCESS(Status))
    {
        RtlFreeAnsiString(&CanonicalNameA);

        if (Address)
        {
            ExFreePoolWithTag(Address, WSK_POOL_TAG);
        }

        if (Result)
        {
            ExFreePoolWithTag(Result, WSK_POOL_TAG);
        }
    }

    return Status;
}

int WSKAPI getaddrinfo(
    _In_opt_ const char* nodename,
    _In_opt_ const char* servname,
    _In_opt_ const struct addrinfo* hints,
    _Outptr_ struct addrinfo** res
)
{
    NTSTATUS        Status  = STATUS_SUCCESS;
    addrinfoexW*    Hints   = nullptr;
    addrinfoexW*    Result  = nullptr;

    UNICODE_STRING  HostNameW{};
    UNICODE_STRING  ServNameW{};

    do
    {
        *res = nullptr;

        if (nodename)
        {
            ANSI_STRING HostNameA{};
            RtlInitAnsiString(&HostNameA, nodename);

            Status = RtlAnsiStringToUnicodeString(&HostNameW, &HostNameA, TRUE);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        if (servname)
        {
            ANSI_STRING ServNameA{};
            RtlInitAnsiStringEx(&ServNameA, servname);

            Status = RtlAnsiStringToUnicodeString(&ServNameW, &ServNameA, TRUE);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        Status = convert_addrinfo_to_addrinfoex(&Hints, hints);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        Status = WSKGetAddrInfo(HostNameW.Buffer, ServNameW.Buffer, NS_ALL, nullptr,
            Hints, &Result, WSK_INFINITE_WAIT, nullptr, nullptr);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        Status = convert_addrinfoex_to_addrinfo(res, Result);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

    } while (false);

    RtlFreeUnicodeString(&HostNameW);
    RtlFreeUnicodeString(&ServNameW);

    WSKFreeAddrInfo(Hints);
    WSKFreeAddrInfo(Result);

    return Status;
}

void WSKAPI freeaddrinfo(
    _In_  struct addrinfo* ai
)
{
    if (ai)
    {
        if (ai->ai_next)
        {
            freeaddrinfo(ai->ai_next);
        }

        if (ai->ai_canonname)
        {
            ExFreePool(ai->ai_canonname);
        }

        if (ai->ai_addr)
        {
            ExFreePoolWithTag(ai->ai_addr, WSK_POOL_TAG);
        }

        ExFreePoolWithTag(ai, WSK_POOL_TAG);
    }
}

int WSKAPI getnameinfo(
    _In_reads_bytes_(salen) const struct sockaddr* sa,
    _In_  socklen_t salen,
    _Out_writes_bytes_(hostlen) char* host,
    _In_  size_t hostlen,
    _Out_writes_bytes_(servlen) char* serv,
    _In_  size_t servlen,
    _In_  int flags
)
{
    NTSTATUS Status = STATUS_SUCCESS;

    UNICODE_STRING HostName{};
    UNICODE_STRING ServName{};

    do
    {
        void* NameBuffer = nullptr;

        NameBuffer = ExAllocatePoolZero(PagedPool, NI_MAXHOST * sizeof(wchar_t), WSK_POOL_TAG);
        if (NameBuffer == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlInitEmptyUnicodeString(&HostName, static_cast<PWCH>(NameBuffer),
            static_cast<USHORT>(NI_MAXHOST * sizeof(wchar_t)));

        NameBuffer = ExAllocatePoolZero(PagedPool, NI_MAXSERV * sizeof(wchar_t), WSK_POOL_TAG);
        if (NameBuffer == nullptr)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlInitEmptyUnicodeString(&ServName, static_cast<PWCH>(NameBuffer),
            static_cast<USHORT>(NI_MAXSERV * sizeof(wchar_t)));

        Status = WSKGetNameInfo(sa, salen,
            HostName.Buffer, HostName.MaximumLength,
            ServName.Buffer, ServName.MaximumLength,
            flags);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        HostName.Length = static_cast<USHORT>(wcslen(HostName.Buffer) * sizeof(wchar_t));
        ServName.Length = static_cast<USHORT>(wcslen(ServName.Buffer) * sizeof(wchar_t));

        if (host)
        {
            ANSI_STRING HostNameA{};
            RtlInitEmptyAnsiString(&HostNameA, host, static_cast<USHORT>(hostlen));

            Status = RtlUnicodeStringToAnsiString(&HostNameA, &HostName, false);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        if (serv)
        {
            ANSI_STRING ServNameA{};
            RtlInitEmptyAnsiString(&ServNameA, serv, static_cast<USHORT>(servlen));

            Status = RtlUnicodeStringToAnsiString(&ServNameA, &ServName, false);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

    } while (false);

    RtlFreeUnicodeString(&HostName);
    RtlFreeUnicodeString(&ServName);

    return WSKSetLastError(Status), Status;
}

int WSKAPI inet_pton(
    _In_    int         Family,
    _In_    const char* AddressString,
    _When_(Family == AF_INET , _Out_writes_bytes_(sizeof(IN_ADDR)))
    _When_(Family == AF_INET6, _Out_writes_bytes_(sizeof(IN6_ADDR)))
    void*   Address
)
{
    NTSTATUS Status     = STATUS_SUCCESS;
    PCSTR    Terminator = nullptr;

    switch (Family)
    {
    default:
        Status = STATUS_INVALID_PARAMETER;
        break;

    case AF_INET:
        Status = RtlIpv4StringToAddressA(AddressString, true, &Terminator,
            static_cast<in_addr*>(Address));
        break;

    case AF_INET6:
        Status = RtlIpv6StringToAddressA(AddressString, &Terminator,
            static_cast<in6_addr*>(Address));
        break;
    }

    WSKSetLastError(Status);

    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_INVALID_PARAMETER)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    }

    return 1;
}

PCSTR WSKAPI inet_ntop(
    _In_    int             Family,
    _In_    const void*     Address,
    _Out_writes_(StringBufSize) char* AddressString,
    _In_    size_t          StringBufSize
)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PCSTR    Result = nullptr;

    switch (Family)
    {
    default:
    {
        Status = STATUS_INVALID_PARAMETER;
        break;
    }
    case AF_INET:
    {
        if (StringBufSize < _countof("255.255.255.255"))
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Result = RtlIpv4AddressToStringA(static_cast<const in_addr*>(Address), AddressString);
        break;
    }
    case AF_INET6:
    {
        if (StringBufSize < _countof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")) // IPv4-mapped IPv6
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }

        Result = RtlIpv6AddressToStringA(static_cast<const in6_addr*>(Address), AddressString);
        break;
    }
    }

    WSKSetLastError(Status);

    return Result;
}

unsigned long WSKAPI htonl(
    _In_ unsigned long hostlong
)
{
    return RtlUlongByteSwap(hostlong);
}

unsigned long WSKAPI ntohl(
    _In_ unsigned long netlong
)
{
    return RtlUlongByteSwap(netlong);
}

unsigned short WSKAPI htons(
    _In_ unsigned short hostshort
)
{
    return RtlUshortByteSwap(hostshort);
}

unsigned short WSKAPI ntohs(
    _In_ unsigned short netshort
)
{
    return RtlUshortByteSwap(netshort);
}


#ifdef __cplusplus
}
#endif
