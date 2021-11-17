#pragma once

using SOCKET = UINT_PTR;

#ifndef WSK_INVALID_SOCKET
#  define WSK_INVALID_SOCKET static_cast<SOCKET>(~0)
#endif

#ifndef WSK_FLAG_INVALID_SOCKET
#  define WSK_FLAG_INVALID_SOCKET 0xffffffff
#endif

//////////////////////////////////////////////////////////////////////////
// Private Struct

struct SOCKET_OBJECT
{
    PWSK_SOCKET Socket;
    USHORT      SocketType;     // WSK_FLAG_xxxxxx_SOCKET
    USHORT      FileDescriptor; // SOCKET FD

    ULONG       SendTimeout;
    ULONG       RecvTimeout;

    PVOID       Context;
};
using PSOCKET_OBJECT = SOCKET_OBJECT*;

//////////////////////////////////////////////////////////////////////////
// Public Function

VOID WSKAPI WSKSocketsAVLTableInitialize();

VOID WSKAPI WSKSocketsAVLTableCleanup();

BOOLEAN WSKAPI WSKSocketsAVLTableInsert(
    _Out_ SOCKET*        SocketFD,
    _In_  PWSK_SOCKET    Socket,
    _In_  USHORT         SocketType
);

BOOLEAN WSKAPI WSKSocketsAVLTableDelete(
    _In_  SOCKET         SocketFD
);

BOOLEAN WSKAPI WSKSocketsAVLTableFind(
    _In_  SOCKET         SocketFD,
    _Out_ SOCKET_OBJECT* SocketObject
);

BOOLEAN WSKAPI WSKSocketsAVLTableUpdate(
    _In_  SOCKET         SocketFD,
    _In_  SOCKET_OBJECT* SocketObject
);

SIZE_T WSKAPI WSKSocketsAVLTableSize();
