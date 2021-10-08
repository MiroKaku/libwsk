#include <ntddk.h>
#include <wdm.h>
#include "src\libwsk.h"


EXTERN_C_START
DRIVER_INITIALIZE   DriverEntry;
DRIVER_UNLOAD       DriverUnload;
EXTERN_C_END

#define MAX_ADDRESS_STRING_LENGTH   64u

NTSTATUS DriverEntry(_In_ DRIVER_OBJECT* DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS     Status    = STATUS_SUCCESS;
    PADDRINFOEXW DNSResult = nullptr;

    do 
    {
        DriverObject->DriverUnload = DriverUnload;

        WSKDATA WSKData{};
        Status = WSKStartup(MAKE_WSK_VERSION(1, 0), &WSKData);
        if (!NT_SUCCESS(Status))
        {
            break;
        }

        ADDRINFOEXW Hints{};
        Hints.ai_family = AF_UNSPEC;

        Status = WSKGetAddrInfo(L"httpbin.org", L"https", NS_DNS, nullptr, &Hints, &DNSResult,
            WSK_NO_WAIT, [](
                _In_ NTSTATUS Status,
                _In_ ULONG_PTR Bytes,
                _In_ PADDRINFOEXW Result)
            {
                UNREFERENCED_PARAMETER(Bytes);

                if (!NT_SUCCESS(Status))
                {
                    return;
                }

                auto DNSResult = Result;
                while (DNSResult)
                {
                    WCHAR  AddressString[MAX_ADDRESS_STRING_LENGTH]{};
                    UINT32 AddressStringLength = MAX_ADDRESS_STRING_LENGTH;

                    WSKAddressToString(
                        reinterpret_cast<SOCKADDR_INET*>(DNSResult->ai_addr),
                        static_cast<UINT32>(DNSResult->ai_addrlen),
                        AddressString,
                        &AddressStringLength);

                    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                        "IP Address: %ls\n", AddressString);

                    DNSResult = DNSResult->ai_next;
                }

                WSKFreeAddrInfo(Result);
            });
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
