#include <ntddk.h>
#include <wdm.h>


EXTERN_C_START
DRIVER_INITIALIZE   DriverEntry;
DRIVER_UNLOAD       DriverUnload;
EXTERN_C_END


NTSTATUS DriverEntry(_In_ DRIVER_OBJECT* DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS Result = STATUS_SUCCESS;

    do 
    {
        DriverObject->DriverUnload = DriverUnload;


    } while (false);

    return Result;
}

VOID DriverUnload(_In_ DRIVER_OBJECT* DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}
