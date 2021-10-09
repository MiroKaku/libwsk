#include <ntddk.h>
#include <wdm.h>
#include "src\libwsk.h"

EXTERN_C_START
DRIVER_INITIALIZE   DriverEntry;
DRIVER_UNLOAD       DriverUnload;
EXTERN_C_END

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

    } while (false);

    return Status;
}

VOID DriverUnload(_In_ DRIVER_OBJECT* DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    WSKCleanup();
}
