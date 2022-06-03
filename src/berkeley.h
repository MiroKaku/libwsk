#pragma once
#include "libwsk.h"

/*
 * This is used instead of -1, since the
 * SOCKET type is unsigned.
 */
#ifndef INVALID_SOCKET
#define INVALID_SOCKET  WSK_INVALID_SOCKET
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR    (-1)
#endif

#ifndef SOCKET_SUCCESS
#define SOCKET_SUCCESS  (0)
#endif

using socklen_t = int;

/* Socket function prototypes */

#ifdef __cplusplus
extern "C" {
#endif

SOCKET WSKAPI socket(
    _In_ int af,
    _In_ int type,
    _In_ int protocol
);

int WSKAPI closesocket(
    _In_ SOCKET s
);

int WSKAPI bind(
    _In_ SOCKET s,
    _In_reads_bytes_(addrlen) const struct sockaddr* addr,
    _In_ int addrlen
);

int WSKAPI listen(
    _In_ SOCKET s,
    _In_ int backlog
);

int WSKAPI connect(
    _In_ SOCKET s,
    _In_reads_bytes_(addrlen) const struct sockaddr* addr,
    _In_ int addrlen
);

int WSKAPI shutdown(
    _In_ SOCKET s,
    _In_ int how
);

SOCKET WSKAPI accept(
    _In_ SOCKET s,
    _Out_writes_bytes_opt_(*addrlen) struct sockaddr* addr,
    _Inout_opt_ int* addrlen
);

int WSKAPI send(
    _In_ SOCKET s,
    _In_reads_bytes_(len) const char* buf,
    _In_ int len,
    _In_ int flags
);

int WSKAPI recv(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char* buf,
    _In_ int len,
    _In_ int flags
);

int WSKAPI sendto(
    _In_ SOCKET s,
    _In_reads_bytes_(len) const char* buf,
    _In_ int len,
    _In_ int flags,
    _In_reads_bytes_opt_(tolen) const struct sockaddr* to,
    _In_ int tolen
);

int WSKAPI recvfrom(
    _In_ SOCKET s,
    _Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char* buf,
    _In_ int len,
    _In_ int flags,
    _Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr* from,
    _Inout_opt_ int* fromlen
);

int WSKAPI setsockopt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _In_reads_bytes_opt_(optlen) const char* optval,
    _In_ int optlen
);

int WSKAPI getsockopt(
    _In_ SOCKET s,
    _In_ int level,
    _In_ int optname,
    _Out_writes_bytes_(*optlen) char* optval,
    _Inout_ int* optlen
);

int WSKAPI getaddrinfo(
    _In_opt_ const char* nodename,
    _In_opt_ const char* servname,
    _In_opt_ const struct addrinfo* hints,
    _Outptr_ struct addrinfo** res
);

void WSKAPI freeaddrinfo(
    _In_  struct addrinfo* ai
);

int WSKAPI getnameinfo(
    _In_reads_bytes_(salen) const struct sockaddr* sa,
    _In_  socklen_t salen,
    _Out_writes_bytes_(hostlen) char* host,
    _In_  size_t hostlen,
    _Out_writes_bytes_(servlen) char* serv,
    _In_  size_t servlen,
    _In_  int flags
);

int WSKAPI inet_pton(
    _In_    int         Family,
    _In_    const char* AddressString,
    _When_(Family == AF_INET, _Out_writes_bytes_(sizeof(IN_ADDR)))
    _When_(Family == AF_INET6, _Out_writes_bytes_(sizeof(IN6_ADDR)))
    void* Address
);

PCSTR WSKAPI inet_ntop(
    _In_    int             Family,
    _In_    const void* Address,
    _Out_writes_(StringBufSize) char* AddressString,
    _In_    size_t          StringBufSize
);

unsigned long WSKAPI htonl(
    _In_ unsigned long hostlong
);

unsigned long WSKAPI ntohl(
    _In_ unsigned long netlong
);

unsigned short WSKAPI htons(
    _In_ unsigned short hostshort
);

unsigned short WSKAPI ntohs(
    _In_ unsigned short netshort
);

#ifdef __cplusplus
}
#endif
