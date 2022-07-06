#include "universal.h"
#include "socket.h"


//////////////////////////////////////////////////////////////////////////
// Global  Data

static NPAGED_LOOKASIDE_LIST WSKSocketsLookasidePool;
static RTL_AVL_TABLE         WSKSocketsAVLTable;
static FAST_MUTEX            WSKSocketsAVLTableMutex;

//////////////////////////////////////////////////////////////////////////
// Private Function

RTL_GENERIC_COMPARE_RESULTS NTAPI WSKSocketsAVLNodeCompare(
    _In_ RTL_AVL_TABLE* Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct
)
{
    UNREFERENCED_PARAMETER(Table);

    auto Socket1 = static_cast<PSOCKET_OBJECT>(FirstStruct);
    auto Socket2 = static_cast<PSOCKET_OBJECT>(SecondStruct);

    return 
        (Socket1->FileDescriptor < Socket2->FileDescriptor) ? GenericLessThan : 
        (Socket1->FileDescriptor > Socket2->FileDescriptor) ? GenericGreaterThan : GenericEqual;
}

PVOID NTAPI WSKSocketsAVLNodeAllocate(
    _In_ RTL_AVL_TABLE* Table,
    _In_ CLONG ByteSize
)
{
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(ByteSize);

    return ExAllocateFromNPagedLookasideList(&WSKSocketsLookasidePool);
}

VOID NTAPI WSKSocketsAVLNodeFree(
    _In_ RTL_AVL_TABLE* Table,
    _In_ __drv_freesMem(Mem) _Post_invalid_ PVOID Buffer
)
{
    UNREFERENCED_PARAMETER(Table);

    return ExFreeToNPagedLookasideList(&WSKSocketsLookasidePool, Buffer);
}

//////////////////////////////////////////////////////////////////////////
// Public Function

VOID WSKAPI WSKSocketsAVLTableInitialize()
{
    ExInitializeNPagedLookasideList(&WSKSocketsLookasidePool, nullptr, nullptr,
#if (NTDDI_VERSION < NTDDI_WIN8)
        0,
#else
        POOL_NX_ALLOCATION,
#endif
        max(sizeof SOCKET_OBJECT + sizeof RTL_BALANCED_LINKS, 64), WSK_POOL_TAG, 0);

    ExInitializeFastMutex(&WSKSocketsAVLTableMutex);

    RtlInitializeGenericTableAvl(&WSKSocketsAVLTable, &WSKSocketsAVLNodeCompare,
        &WSKSocketsAVLNodeAllocate, &WSKSocketsAVLNodeFree, &WSKSocketsLookasidePool);
}

VOID WSKAPI WSKSocketsAVLTableCleanup()
{
    SOCKET_OBJECT* Socket = static_cast<SOCKET_OBJECT*>(RtlGetElementGenericTableAvl(&WSKSocketsAVLTable, 0));

    while (Socket)
    {
        RtlDeleteElementGenericTableAvl(&WSKSocketsAVLTable, Socket);

        Socket = static_cast<SOCKET_OBJECT*>(RtlGetElementGenericTableAvl(&WSKSocketsAVLTable, 0));
    }

    ExDeleteNPagedLookasideList(&WSKSocketsLookasidePool);
}

BOOLEAN WSKAPI WSKSocketsAVLTableInsert(
    _Out_ SOCKET*       SocketFD,
    _In_  PWSK_SOCKET   Socket,
    _In_  USHORT        SocketType
)
{
    PAGED_CODE();

    static volatile short _FD = 4;

    SOCKET_OBJECT SockObject{};
    SockObject.Socket       = Socket;
    SockObject.SocketType   = SocketType;
    SockObject.SendTimeout  = WSK_INFINITE_WAIT;
    SockObject.RecvTimeout  = WSK_INFINITE_WAIT;

    BOOLEAN Inserted = FALSE;

    ExAcquireFastMutex(&WSKSocketsAVLTableMutex);
    {
        do 
        {
            SockObject.FileDescriptor = InterlockedCompareExchange16(&_FD, _FD + 4, _FD);

            if (!RtlInsertElementGenericTableAvl(&WSKSocketsAVLTable, &SockObject, sizeof SockObject, &Inserted))
            {
                break;
            }

        } while (!Inserted);
    }
    ExReleaseFastMutex(&WSKSocketsAVLTableMutex);

    if (Inserted)
    {
        *SocketFD = SockObject.FileDescriptor;
    }
    else
    {
        *SocketFD = WSK_INVALID_SOCKET;
    }

    return Inserted;
}

BOOLEAN WSKAPI WSKSocketsAVLTableDelete(
    _In_  SOCKET SocketFD
)
{
    PAGED_CODE();

    SOCKET_OBJECT SockObject{};
    SockObject.FileDescriptor = static_cast<USHORT>(SocketFD);

    BOOLEAN Deleted = FALSE;

    ExAcquireFastMutex(&WSKSocketsAVLTableMutex);
    {
        Deleted = RtlDeleteElementGenericTableAvl(&WSKSocketsAVLTable, &SockObject);
    }
    ExReleaseFastMutex(&WSKSocketsAVLTableMutex);

    return Deleted;
}

BOOLEAN WSKAPI WSKSocketsAVLTableFind(
    _In_  SOCKET         SocketFD,
    _Out_ SOCKET_OBJECT* SocketObject
)
{
    PAGED_CODE();

    BOOLEAN Found = FALSE;

    SocketObject->FileDescriptor = static_cast<USHORT>(SocketFD);

    ExAcquireFastMutex(&WSKSocketsAVLTableMutex);
    {
        auto Node = static_cast<SOCKET_OBJECT*>(RtlLookupElementGenericTableAvl(&WSKSocketsAVLTable, SocketObject));
        if (Node != nullptr)
        {
            Found = TRUE;

            *SocketObject = *Node;
        }
    }
    ExReleaseFastMutex(&WSKSocketsAVLTableMutex);

    return Found;
}

BOOLEAN WSKAPI WSKSocketsAVLTableUpdate(
    _In_  SOCKET         SocketFD,
    _In_  SOCKET_OBJECT* SocketObject
)
{
    PAGED_CODE();

    BOOLEAN Found = FALSE;

    SocketObject->FileDescriptor = static_cast<USHORT>(SocketFD);

    ExAcquireFastMutex(&WSKSocketsAVLTableMutex);
    {
        auto Node = static_cast<SOCKET_OBJECT*>(RtlLookupElementGenericTableAvl(&WSKSocketsAVLTable, SocketObject));
        if (Node != nullptr)
        {
            Found = TRUE;

            Node->SendTimeout = SocketObject->SendTimeout;
            Node->RecvTimeout = SocketObject->RecvTimeout;
        }
    }
    ExReleaseFastMutex(&WSKSocketsAVLTableMutex);

    return Found;
}

SIZE_T WSKAPI WSKSocketsAVLTableSize()
{
    PAGED_CODE();

    SIZE_T Size = 0u;

    ExAcquireFastMutex(&WSKSocketsAVLTableMutex);
    {
        Size = RtlNumberGenericTableElementsAvl(&WSKSocketsAVLTable);
    }
    ExReleaseFastMutex(&WSKSocketsAVLTableMutex);

    return Size;
}
