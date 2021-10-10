#pragma once

using SOCKET = UINT_PTR;

#ifndef WSK_INVALID_SOCKET
#  define WSK_INVALID_SOCKET static_cast<SOCKET>(~0)
#endif

#ifndef WSK_FLAG_INVALID_SOCKET
#  define WSK_FLAG_INVALID_SOCKET 0xffffffff
#endif

//////////////////////////////////////////////////////////////////////////
// Public Function

VOID WSKAPI WSKSocketsAVLTableInitialize();

VOID WSKAPI WSKSocketsAVLTableCleanup();

BOOLEAN WSKAPI WSKSocketsAVLTableInsert(
    _Out_ SOCKET*       SocketFD,
    _In_  PWSK_SOCKET   Socket,
    _In_  USHORT        SocketType
);

BOOLEAN WSKAPI WSKSocketsAVLTableDelete(
    _In_  SOCKET        SocketFD
);

BOOLEAN WSKAPI WSKSocketsAVLTableFind(
    _In_  SOCKET        SocketFD,
    _Out_ PWSK_SOCKET*  Socket,
    _Out_ USHORT*       SocketType
);

BOOLEAN WSKAPI WSKSocketsAVLTableUpdate(
    _In_  SOCKET        SocketFD,
    _In_opt_ PWSK_SOCKET Socket,
    _In_opt_ USHORT     SocketType
);

SIZE_T WSKAPI WSKSocketsAVLTableSize();
