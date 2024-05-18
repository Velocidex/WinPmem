#include <stdio.h>
#include <stdlib.h>
#include <ntifs.h>

#define IOCTL_SET_MODE    CTL_CODE(0x22, 0x101, 3, 3)

#define IOCTL_REVERSE_SEARCH_QUERY  CTL_CODE(0x22, 0x104, 3, 3)

#define PMEM_MODE_IOSPACE 0
#define PMEM_MODE_PHYSICAL 1
#define PMEM_MODE_PTE 2

#if defined(_WIN64)

// Install Winpmem yourself.

NTSTATUS setMode(_In_ HANDLE winpmemHandle, _In_ ULONG mode);
NTSTATUS reverseQuery(_In_ HANDLE winpmemHandle, _In_ UINT64 VA_Addr, _Out_ PUINT64 PhysAddr);
BOOLEAN openDevice(_Out_ PHANDLE pDevice, _In_ PWCHAR name, _In_ ACCESS_MASK DesiredAccess, _In_ ULONG ShareAccess);
BOOLEAN doPhysicalReadFromWinpmem(_In_ HANDLE winpmemHandle, _Out_ unsigned char * buffer, _In_ ULONG buffersize, _In_ PLARGE_INTEGER PhysAddr, _In_ ULONG mode);

void main(_In_ ULONG argc, _In_reads_(argc) PCHAR argv[])
{
    NTSTATUS status = STATUS_SUCCESS;

    // Please choose one of the three methods.
    ULONG mode = PMEM_MODE_PHYSICAL; // PMEM_MODE_PTE; // PMEM_MODE_PHYSICAL;

    HANDLE winpmemHandle = NULL;
    BOOLEAN bResult = TRUE;
    LARGE_INTEGER PhysAddr;
    unsigned __int64 qword = 0;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    // Alternative testing:
    
    if (argc < 2)
    {
        printf("Usage: %s 0xPhysicalAddress.\n", argv[0]);
        return;
    }
    PhysAddr.QuadPart = _strtoui64(argv[1], NULL, 16);

    if (!PhysAddr.QuadPart)
    {
        printf("Not going to use physical address 0. This is foolish. :o)\n");
        return;
    }

    // For using PhysAddr directly in the Read, skip the reverse search query.


    bResult = openDevice(&winpmemHandle, L"\\Device\\pmem",
                        FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE);

    if (!bResult)
    {
        printf("Error: open device failed.\n");
        goto exit;

    }

    status = setMode(winpmemHandle, mode);

    if (status == STATUS_ACCESS_DENIED)
    {
        printf("Mode has already been set previously. Continuing.\n");
    }
    else if (status != STATUS_SUCCESS)
    {
        printf("Error: setting mode failed with status %08x.\n", status);
        goto exit;
    }

    bResult = doPhysicalReadFromWinpmem(winpmemHandle, (unsigned char *) &qword, sizeof(UINT64), &PhysAddr, mode);

    if (bResult)
    {
        printf("Winpmem says the (QWORD) content of physical address %llx is: %llx.\n", PhysAddr.QuadPart, qword);
    }

exit:

    if (winpmemHandle) NtClose(winpmemHandle);

    return;
}



NTSTATUS setMode(_In_ HANDLE winpmemHandle, _In_ ULONG mode)
{
    NTSTATUS status = STATUS_SUCCESS;
    IO_STATUS_BLOCK iosb = {0};

    RtlZeroMemory(&iosb, sizeof(iosb));

    status = NtDeviceIoControlFile(winpmemHandle, NULL, NULL, NULL, &iosb, IOCTL_SET_MODE, &mode, sizeof(ULONG),NULL , 0);

    return status;
}

NTSTATUS reverseQuery(_In_ HANDLE winpmemHandle, _In_ UINT64 VA_Addr, _Out_ PUINT64 PhysAddr)
{
    NTSTATUS status = STATUS_SUCCESS;
    IO_STATUS_BLOCK iosb = {0};

    if (!(VA_Addr && PhysAddr)) return STATUS_INVALID_PARAMETER;

    *PhysAddr = 0;

    RtlZeroMemory(&iosb, sizeof(iosb));

    status = NtDeviceIoControlFile(winpmemHandle, NULL, NULL, NULL, &iosb, IOCTL_REVERSE_SEARCH_QUERY, &VA_Addr, sizeof(UINT64), PhysAddr , sizeof(UINT64));

    return status;
}


BOOLEAN openDevice(_Out_ PHANDLE pDevice, _In_ PWCHAR name, _In_ ACCESS_MASK DesiredAccess, _In_ ULONG ShareAccess)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr = { 0 };
    UNICODE_STRING deviceName;
    IO_STATUS_BLOCK iostatusblock;

    if (!((pDevice) && (name))) return FALSE;

    *pDevice = 0;

    RtlInitUnicodeString(&deviceName, name);

    objAttr.Length = sizeof( objAttr );
    objAttr.ObjectName = &deviceName;

    status = NtCreateFile(
                pDevice,
                DesiredAccess,
                &objAttr,
                &iostatusblock,
                NULL,
                FILE_ATTRIBUTE_NORMAL,
                ShareAccess,
                FILE_OPEN,
                FILE_NON_DIRECTORY_FILE|FILE_SYNCHRONOUS_IO_NONALERT,
                NULL,
                0
            );
    if ( !NT_SUCCESS(status) )
    {
        printf("Error (0x%08x): Open device failed.\n", status);
        return FALSE;
    }

    return TRUE;
}


BOOLEAN doPhysicalReadFromWinpmem(_In_ HANDLE winpmemHandle, _Out_ unsigned char * buffer, _In_ ULONG buffersize, _In_ PLARGE_INTEGER PhysAddr, _In_ ULONG mode)
{
    NTSTATUS status = STATUS_SUCCESS;
    IO_STATUS_BLOCK iosb = {0};

    if (!(winpmemHandle && buffer && buffersize && PhysAddr && mode)) return FALSE;

    memset(buffer, 0, buffersize);

    status = NtReadFile(winpmemHandle, NULL, NULL, NULL, &iosb, buffer, buffersize, PhysAddr, NULL);
    if ( status != STATUS_SUCCESS)
    {
        printf("NtReadFile failed: status: %08x. iosb: %08x, information: %llx.\n",
                                   status, iosb.Status, iosb.Information);
    }

    printf("iosb.information: %llx.\n", iosb.Information);

    return TRUE;

}

#endif // end of win64 sanity check ^^
