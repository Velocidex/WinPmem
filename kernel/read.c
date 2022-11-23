/*
  Copyright 2018 Velocidex Innovations <mike@velocidex.com>
  Copyright 2014-2017 Google Inc.
  Copyright 2012 Michael Cohen <scudette@gmail.com>
  Authors: Viviane Zwanger, Michael Cohen <mike@velocidex.com>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "read.h"
#include "winpmem.h"

// This opens a section handle to the physicalMemory device and quicksaves
// the handle in the device extension.
_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN setupPhysMemSectionHandle(_Out_ PHANDLE pMemoryHandle)
{
    NTSTATUS ntstatus;
    UNICODE_STRING PhysicalMemoryPath;
    OBJECT_ATTRIBUTES MemoryAttributes;

    PAGED_CODE();

    if (!pMemoryHandle) return FALSE;

    *pMemoryHandle = 0;  // initialize out variable with zero.

    RtlInitUnicodeString(&PhysicalMemoryPath, L"\\Device\\PhysicalMemory");

    InitializeObjectAttributes(&MemoryAttributes,
                               &PhysicalMemoryPath,
                               OBJ_KERNEL_HANDLE,
                               (HANDLE) NULL,
                               (PSECURITY_DESCRIPTOR) NULL);

    ntstatus = ZwOpenSection(pMemoryHandle, SECTION_MAP_READ, &MemoryAttributes);

    if (!NT_SUCCESS(ntstatus))
    {
        DbgPrint("Error: failed ZwOpenSection(MemoryHandle) => %08X\n", ntstatus);
        return FALSE;
    }

    return TRUE;
}


// Method I.
// This method is thread-safe and does not need protection of a mutex.
// This routine requires PASSIVE LEVEL and can't work under a mutex.
_IRQL_requires_max_(PASSIVE_LEVEL)
ULONG PhysicalMemoryPartialRead(_In_ HANDLE memoryHandle,
                                _In_ LARGE_INTEGER physAddr,
                                _Inout_ unsigned char * buf,
                                _In_ ULONG count)
{
    ULONG page_offset = physAddr.QuadPart % PAGE_SIZE;
    ULONG to_read = min(PAGE_SIZE - page_offset, count);
    PUCHAR mapped_buffer = NULL;
    SIZE_T ViewSize = PAGE_SIZE;
    NTSTATUS ntstatus;
    ULONG result = 0;

    if (!(memoryHandle && physAddr.QuadPart && buf && count))
    {
        return 0;
    }

    // The mapview should never fail on physical memory device.
    ntstatus = ZwMapViewOfSection(memoryHandle, ZwCurrentProcess() ,
                  &mapped_buffer, 0L, PAGE_SIZE, &physAddr,
                  &ViewSize, ViewUnmap, 0, PAGE_READONLY);

    if ((ntstatus != STATUS_SUCCESS) || (!mapped_buffer))
    {
        DbgPrint("Error: ZwMapViewOfSection failed. physAddr 0x%llX, status %08x.\n", physAddr.QuadPart,ntstatus); // real error
        goto error;
    }

    // From performance-technical view it seems unwise to mapview for each tiny read, but ZwMapViewOfSection(wholeRAM) is a very bad idea, in many terms.
    // Thus, this is a suitable safe-but-slow method for reading only portions of (RAM-backed) RAM. This method is not optimal for reading the whole RAM (but it can be used).

    // =warning=
    // On a Windows with Hyper-V layer/VSM  (or whatever you want to name it, for me it's simply the HV layer beneath the OS.)
    // the host OS is above the Hyper-V layer. The HV (which is below the OS) can
    // and will block certain reads from certain memory locations if "it" does not want it.
    // It is happening outside of the "OS". As a rather profane kernel driver, we must live with it
    // and be prepared to be unable to read from a memory location, without any "sane" reason. (like, "out of nothing")
    // The approach: we very carefully check if it's readable and if yes, we return the bytes.
    // Otherwise we return immediately with a read error.

    try // Hyper-v/VSM induced possible read error
    {
        // "be super extra careful here."
        RtlCopyMemory(buf, mapped_buffer + page_offset, to_read);  // ProbeForRead would not help here. This is kernel VA already. We must face the fact that we might fail the read.
    }
    except(EXCEPTION_EXECUTE_HANDLER)
    {
        // RtlZeroMemory(buf, to_read); // return zeros instead

        DbgPrint("Error (VSM read error): unable to read %u bytes from %p, doing padding instead.\n", to_read, mapped_buffer+page_offset);
        goto error;
        // result = to_read;
    }

    result = to_read;

error:
    ZwUnmapViewOfSection(ZwCurrentProcess(), mapped_buffer);

    return result;
}


// Method II.
// This method is thread-safe and does not need protection of a mutex.
// It can work at higher IRQL but doesn't.
// Read a single page using MmMapIoSpace.
ULONG MapIOPagePartialRead(_In_ LARGE_INTEGER physAddr, _Inout_ unsigned char * buf, _In_ ULONG count)
{
    ULONG page_offset = physAddr.QuadPart % PAGE_SIZE;
    ULONG to_read = min(PAGE_SIZE - page_offset, count);
    PUCHAR mapped_buffer = NULL;
    LARGE_INTEGER ViewBase;
    ULONG result = 0;

    if (!(physAddr.QuadPart && buf && count))
    {
        return 0;
    }

    // Round to page size
    ViewBase.QuadPart = physAddr.QuadPart - page_offset;

    // =warning=
    // On a Windows with Hyper-V layer/VSM  (or whatever you want to name it, for me it's simply the HV layer beneath the OS.)
    // the host OS is above the Hyper-V layer. The HV (which is below the OS) can
    // and will block certain reads from certain memory locations if "it" does not want it.
    // It is happening outside of the "OS". As a rather profane kernel driver, we must live with it
    // and be prepared to be unable to read from a memory location, without any "sane" reason. (like, "out of nothing")
    // The approach: we very carefully check if it's readable and if yes, we return the bytes.
    // Otherwise we return immediately with a read error.

    try // Hyper-v/VSM induced possible read error
    {
        // "be super extra careful here."

        mapped_buffer = MmMapIoSpace(ViewBase, PAGE_SIZE, MmCached);  // <= may fail and BSOD on a machine with Hyper-V layer / "VSM".

        if (mapped_buffer)
        {
            RtlCopyMemory(buf, mapped_buffer+page_offset, to_read);
        }
        else
        {
            // RtlZeroMemory(buf, to_read); // return zeros instead
            DbgPrint("Error: unable to read %u bytes from %p, doing padding instead.\n", to_read, mapped_buffer+page_offset); // real error
            return 0;
            // result = to_read;
        }
    }
    except(EXCEPTION_EXECUTE_HANDLER)
    {
        // RtlZeroMemory(buf, to_read); // return zeros instead
        DbgPrint("Error (VSM read error): unable to read %u bytes from %p, doing padding instead.\n", to_read, mapped_buffer+page_offset);
        return 0;
        // result = to_read;
        // No unmapping of the buffer: it did not go well.
    }

    MmUnmapIoSpace(mapped_buffer, PAGE_SIZE);
    result = to_read;

    return result;
}


#if defined(_WIN64)

// Method III.
// !! This method is not thread-safe and crucially requires protection of a mutex.
// Read a single page using direct PTE mapping.
_IRQL_requires_max_(APC_LEVEL)
ULONG PTEMmapPartialRead(_Inout_ PPTE_METHOD_DATA pPtedata, _In_ LARGE_INTEGER physAddr, _Inout_ unsigned char * buf, _In_ ULONG count)
{
    ULONG page_offset = physAddr.QuadPart % PAGE_SIZE;
    ULONG to_read = min(PAGE_SIZE - page_offset, count);
    LARGE_INTEGER viewPage;
    ULONG result = 0;
    unsigned char * toxic_source = NULL;

    if (!(pPtedata && physAddr.QuadPart && buf && count))
    {
        return 0;
    }

    // Round to page size
    viewPage.QuadPart = physAddr.QuadPart - page_offset;

    if (pte_remap_rogue_page(pPtedata, viewPage.QuadPart) == PTE_SUCCESS)
    {
        toxic_source = (PVOID) (((ULONG_PTR) pPtedata->page_aligned_rogue_ptr.value) + page_offset); // toxic, but not the userspace buffer this time.

        // =warning=
        // On a Windows with Hyper-V layer/VSM  (or whatever you want to name it, for me it's simply the HV layer beneath the OS.)
        // the host OS is above the Hyper-V layer. The HV (which is below the OS) can
        // and will block certain reads from certain memory locations if "it" does not want it.
        // It is happening outside of the "OS". As a rather profane kernel driver, we must live with it
        // and be prepared to be unable to read from a memory location, without any "sane" reason. (like, "out of nothing")
        // The approach: we very carefully check if it's readable and if yes, we return the bytes.
        // Otherwise we return immediately with a read error.

        try  // Hyper-v/VSM induced possible read error
        { // "be super extra careful here."

            RtlCopyMemory(buf, toxic_source, to_read); // copy content to usermode NEITHER buffer.

        } except(EXCEPTION_EXECUTE_HANDLER)
        {
            // RtlZeroMemory(buf, to_read); // return zeros instead
            DbgPrint("Warning: unable to read %u bytes from %p for %llx, doing padding instead.\n", to_read, toxic_source, viewPage.QuadPart);
            return 0;
            // result = to_read;
        }
        result = to_read;
    }
    // Failed to map page, or an exception occured - error out.
    return result;
}
#endif

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS DeviceRead(_In_ PDEVICE_EXTENSION extension,
                    _In_ LARGE_INTEGER physAddr_cursor,
                    _Inout_ unsigned char * toxic_buffer_cursor, _In_ ULONG howMuchToRead,
                    _Out_ PULONG total_read)
{
    ULONG bytes_read = 0;
    ULONG current_read_window = 0;
    unsigned char * mdl_buffer = NULL;
    PMDL mdl = NULL;
    NTSTATUS status = STATUS_SUCCESS;

    *total_read = 0;

    if (!howMuchToRead) return STATUS_SUCCESS;  // read 0 bytes? Fine, already finished then.

    // Fast mutex, APC LEVEL, and the three methods:
    // The fast mutex sets the IRQL to APC_LEVEL. ZwMapViewOfSection (or ZwReadFile) requires PASSIVE_LEVEL and will not work on APC_LEVEL.
    // The PTE method is not thread-safe and needs mutex protection. The other two methods are thread-safe.

    #if defined(_WIN64)
    if (extension->mode == PMEM_MODE_PTE)  // The PTE method is not thread-safe.
    {
        ExAcquireFastMutex(&extension->mu); // Don't forget to always free the Mutex!
    }
    #endif

    while (*total_read < howMuchToRead)
    {
        current_read_window =  min(PAGE_SIZE, howMuchToRead - *total_read);  // read windows is either PAGE_SIZE (maximum), or a remaining rest: total read minus all that has already been read.

        // Allocate an mdl. Must be freed afterwards (if the call succeeds).
        mdl = IoAllocateMdl(toxic_buffer_cursor, current_read_window,  FALSE, TRUE, NULL); // <= toxic buffer address increases each time in the loop.
        if (!mdl)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto end;
        }

        try
        {
            MmProbeAndLockPages(mdl, UserMode, IoWriteAccess);
        }
        except(EXCEPTION_EXECUTE_HANDLER)
        {

            DbgPrint("Exception while locking I/O buffer 0x%08x.\n", status);
            status = GetExceptionCode();
            IoFreeMdl(mdl);
            goto end;
        }

        // Okay, probed for write access and locked in physical memory.

        mdl_buffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority );

        if (!mdl_buffer)
        {
            MmUnlockPages(mdl);
            IoFreeMdl(mdl);
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto end;
        }

        if (extension->mode == PMEM_MODE_PHYSICAL)
        {
            if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            {
                bytes_read = PhysicalMemoryPartialRead(extension->MemoryHandle, physAddr_cursor, mdl_buffer, current_read_window);
            }
            else
            {
                DbgPrint("Assertion failed: irql > 0.\n");
                bytes_read = 0;
            }
        }
        else if (extension->mode == PMEM_MODE_IOSPACE)
        {
            bytes_read = MapIOPagePartialRead(physAddr_cursor, mdl_buffer, current_read_window);
        }
        #if defined(_WIN64)
        else if (extension->mode == PMEM_MODE_PTE)
        {
            bytes_read = PTEMmapPartialRead(&extension->pte_data, physAddr_cursor, mdl_buffer, current_read_window);
        }
        #endif
        else
        {
            bytes_read = 0;
        }

        if (bytes_read==0)
        {
            // As it is now, the issue is that we do not know whether a real error happened or 'only' a VSM/Hyper-v induced read error.
            // The read handler function returns either the number of bytes or 0 (but no status).
            // We could avoid that by giving a ULONG * bytes_read to the read handler function and have a NTSTATUS returned instead.
            DbgPrint("An error occurred: no bytes read.\n");
            MmUnlockPages(mdl);
            IoFreeMdl(mdl);
            status = STATUS_IO_DEVICE_ERROR;
            goto end;
        }

        MmUnlockPages(mdl);
        IoFreeMdl(mdl);

        physAddr_cursor.QuadPart += bytes_read;
        toxic_buffer_cursor += bytes_read;
        *total_read += bytes_read;

    }

end:
    #if defined(_WIN64)
    if (extension->mode == PMEM_MODE_PTE)
    {
        ExReleaseFastMutex(&extension->mu);
    }
    #endif

    return status;
}


// FAST I/O read
_IRQL_requires_max_(PASSIVE_LEVEL)
BOOLEAN pmemFastIoRead (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER BufOffset,
    __in ULONG BufLen,
    __in BOOLEAN Wait,
    __in ULONG LockKey,
    __out_bcount(BufLen) PVOID toxic_buffer,
    __out PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject )
{
    PDEVICE_EXTENSION extension;
    ULONG total_read = 0;
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T buffer_address = (SIZE_T) toxic_buffer;
    LARGE_INTEGER physAddr;

    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(LockKey);

    PAGED_CODE();

    extension = DeviceObject->DeviceExtension;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) // Does not happen.
    {
        status = STATUS_SUCCESS;
        goto bail_out;
    }

    // DbgPrint("pmemFastIoReadn");


    // Do security checkings now.
    // The usermode program is not allowed to order more than ONE PAGE_SIZE at a time.
    // But the arbitrary read/write allows up to 1-PAGESIZE bytes. So...

    if (!toxic_buffer)
    {
        status = STATUS_INVALID_PARAMETER;
        DbgPrint("Error in pmemFastIoRead: 0x%08x, Bad/nonexisting NULL buffer.\n", status);
        goto bail_out;
    }

    // xxx: Those usermode programs should disturb us only with serious requests. We are a busy driver.

    if (!(BufLen))
    {
        status = STATUS_INVALID_PARAMETER;
        DbgPrint("Error in  in pmemFastIoRead: the caller  wants to read less than one byte.\n");
        goto bail_out;
    }

    if (!BufOffset)
    {
        status = STATUS_INVALID_PARAMETER;
        DbgPrint("Error in  in pmemFastIoRead: no physical address specified.\n");
        goto bail_out;
    }

    physAddr = *BufOffset;

    // xxx: might look into ntstatus.h for nicer NTSTATUS codes.

    // 'Probe' if the usermode program spoke the truth.
    try
    {
        if (BufLen <= (PAGE_SIZE * 4))
        {
            ProbeForWrite( toxic_buffer, BufLen, 1 ); // Generates warning C6001: Using uninitialized memory '*toxic_buffer'.
        }
        else
        {
            // I poke the large buffer in the middle, front and back. ;-)
            #pragma warning( push )
            #pragma warning( disable:6001 )
            ProbeForWrite( toxic_buffer, PAGE_SIZE, 1 );  // Generates warning C6001: Using uninitialized memory '*toxic_buffer'.
            #pragma warning( pop )
            ProbeForWrite( (void *) (buffer_address + (BufLen>>1) - PAGE_SIZE), PAGE_SIZE, 1 );
            ProbeForWrite( (void *) (buffer_address + BufLen - PAGE_SIZE) , PAGE_SIZE, 1 );
        }
        // ProbeForWrite( toxic_buffer, BufLen, 1 );
    }
    except(EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
        DbgPrint("Error: 0x%08x, write-probe in pmemFastIoRead. Bad/nonexisting buffer.\n", status);
        status = STATUS_INVALID_PARAMETER; // The caller tried to give us a bad/non-existing buffer. That's invalid.
        goto bail_out;
    }

    // All things considered, the buffer looked good, of right size, and was accessible for write.
    // It's accepted for the next stage. Don't touch, it's still considered poisonous.

    //DbgPrint("PmemRead\n");
    //DbgPrint("Buffer: %llx, BufLen: 0x%x\n", physAddr, BufLen);

    // ASSERTION: this has been set by SET MODE IOCTL and was very carefully checked. (It is also prevented from being changed.)
    ASSERT((extension->mode == PMEM_MODE_IOSPACE) ||
           (extension->mode == PMEM_MODE_PTE) ||
           (extension->mode == PMEM_MODE_PHYSICAL));

    status = DeviceRead(extension, physAddr, toxic_buffer, BufLen, &total_read);

    // Also check the return of Device Read. Do not simply return.
    if ((status != STATUS_SUCCESS) || (total_read == 0))
    {
        // As it is now, the issue is that we do not know whether a real error happened or 'only' a VSM/Hyper-v induced read error.
        // The read handler function returns either the number of bytes or 0 (but no status).
        // We could avoid that by giving a ULONG * bytes_read to the read handler function and have a NTSTATUS returned instead.
        DbgPrint("Error: a fatal fast I/O read error occurred: no bytes read.\n");
        status = STATUS_IO_DEVICE_ERROR;
        goto bail_out;
    }

    // DbgPrint("Fast I/O read status on return: %08x.\n",status);

    IoStatus->Status = status;
    IoStatus->Information = total_read;
    return TRUE;

    bail_out:

    IoStatus->Status = status;
    IoStatus->Information = 0;
    return TRUE;

}



NTSTATUS PmemRead(IN PDEVICE_OBJECT  DeviceObject, IN PIRP  Irp)
{
    PVOID toxic_buffer;       // Buffer provided by user space. Don't touch it.
    SIZE_T buffer_address;
    ULONG BufLen;    //Buffer length for user provided buffer.
    LARGE_INTEGER physAddr; // The file offset requested from userspace.
    PIO_STACK_LOCATION pIoStackIrp;
    PDEVICE_EXTENSION extension;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG total_read = 0;

    PAGED_CODE();

    if(KeGetCurrentIrql() != PASSIVE_LEVEL) // Cannot happen (unless a malicious kernel driver sends us with evil intention, e.g., to kill us, a high IRQL package.)
    {
        status = STATUS_SUCCESS;
        goto bail_out;
    }

    extension = DeviceObject->DeviceExtension;

    pIoStackIrp = IoGetCurrentIrpStackLocation(Irp);
    pIoStackIrp->FileObject->PrivateCacheMap = (PVOID) -1;
    BufLen = pIoStackIrp->Parameters.Read.Length;
    physAddr = pIoStackIrp->Parameters.Read.ByteOffset;
    toxic_buffer =  Irp->UserBuffer;
    // xxx: what we get from userspace is inside here for the NEITHER I/O type.

    // xxx:
    // maybe it's even a kernel driver or rootkit calling us for help.
    // I will be referring to it mostly as the "usermode program", but also keep in mind it could be a kernelmode system thread.

    // Do security checkings now.
    // The usermode program is not allowed to order more than ONE PAGE_SIZE at a time.
    // But the arbitrary read/write allows up to 1-PAGESIZE bytes. So...

    #pragma warning(disable:6001)

    if (!(toxic_buffer))
    {
        DbgPrint("error: provided buffer in read request was invalid.\n");
        status = STATUS_INVALID_PARAMETER;
        goto bail_out;
    }

    // xxx: Those usermode programs should disturb us only with serious requests. We are a busy driver.

    if (!(BufLen))
    {
        DbgPrint("Complain: the caller  wants to read less than one byte.\n");
        // xxx: What would be good? "Not implemented"? Nah. Better STATUS_INVALID_PARAMETER.
        status = STATUS_INVALID_PARAMETER;
        goto bail_out;
    }

    // xxx: might look into ntstatus.h for nicer NTSTATUS codes.

    // 'Probe' if the usermode program spoke the truth.
    try
    {
        buffer_address = (SIZE_T) toxic_buffer;

        if (BufLen <= (PAGE_SIZE * 4))
        {
            ProbeForWrite( toxic_buffer, BufLen, 1 );
        }
        else
        {
            // I poke the large buffer in the middle, front and back. ;-)
            ProbeForWrite( toxic_buffer, PAGE_SIZE, 1 );
            ProbeForWrite( (void *) (buffer_address + (BufLen>>1) - PAGE_SIZE), PAGE_SIZE, 1 );
            ProbeForWrite( (void *) (buffer_address + BufLen - PAGE_SIZE) , PAGE_SIZE, 1 );
        }
        // ProbeForWrite( toxic_buffer, BufLen, 1 );
    }
    except(EXCEPTION_EXECUTE_HANDLER)
    {

        status = GetExceptionCode();
        DbgPrint("Error: 0x%08x, probe in PmemRead. A naughty process sent us a bad/nonexisting buffer.\n", status);
        status = STATUS_INVALID_PARAMETER; // The caller tried to give us a bad/non-existing buffer. That's invalid.
        goto bail_out;
    }

    // All things considered, the buffer looked good, of right size, and was accessible for write.
    // It's accepted for the next stage. Don't touch, it's still considered poisonous.


    //DbgPrint("PmemRead:\n");
    //DbgPrint("Buffer: %llx, BufLen: 0x%x\n", physAddr, BufLen);

    // ASSERTION: this has been set by SET MODE IOCTL and was very carefully checked. (It is also prevented from being changed.)
    ASSERT((extension->mode == PMEM_MODE_IOSPACE) ||
           (extension->mode == PMEM_MODE_PTE) ||
           (extension->mode == PMEM_MODE_PHYSICAL));

    status = DeviceRead(extension, physAddr, toxic_buffer, BufLen, &total_read);

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = total_read;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;

bail_out:

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

#if PMEM_WRITE_ENABLED == 1

//
// NOTE: I limited it to PAGE_SIZE. It's safe, but restricted. (relatively spoken)
// The usermode buffer is not locked down. Users that compile the driver for themselves and use it testsigned are considered to be mature and careful.
// This has been left as a reminder to be careful.

NTSTATUS PmemWrite(IN PDEVICE_OBJECT  DeviceObject, IN PIRP  Irp)
{
    PVOID toxic_buffer;       //Buffer provided by user space.
    ULONG BufLen;    //Buffer length for user provided buffer.
    LARGE_INTEGER physAddr; // The file offset requested from userspace.
    PIO_STACK_LOCATION pIoStackIrp;
    PDEVICE_EXTENSION extension;
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T ViewSize = PAGE_SIZE;
    PUCHAR mapped_buffer = NULL;
    ULONG page_offset = 0;
    LARGE_INTEGER offset;
    ULONG written = 0;

    PAGED_CODE();

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) // Cannot happen (unless a malicious kernel driver sends us with evil intention, e.g., to kill us, a high IRQL package.)
    {
        status = STATUS_SUCCESS;
        written = 0;
        goto exit;
    }

    extension = DeviceObject->DeviceExtension;

    if (!extension->WriteEnabled)
    {
        DbgPrint("Error (write): access denied -- write mode not enabled.\n");
        written = 0;
        status = STATUS_ACCESS_DENIED;
        goto exit;
    }

    pIoStackIrp = IoGetCurrentIrpStackLocation(Irp);

    // buffer length is going to be checked for being between 1-PAGESIZE.
    BufLen = pIoStackIrp->Parameters.Write.Length;

    // Where to write exactly.
    physAddr.QuadPart = pIoStackIrp->Parameters.Write.ByteOffset;

    toxic_buffer = pIoStackIrp->Parameters.DeviceIoControl.Type3InputBuffer;

    // xxx: hope the caller knows what he's doing here. That's an arbitrary read to anywhere from usermode.
    // Maybe we should insert some kind of quick puzzle to ensure the caller is sane.

    // Security checks now.

    if (!(toxic_buffer))
    {
        DbgPrint("Error (write): provided buffer in write request was invalid.\n");
        written = 0;
        status = STATUS_INVALID_PARAMETER;
        goto exit;
    }

    if (BufLen > PAGE_SIZE)
    {
        DbgPrint("Error (write): Currently not implemented: the caller wants to write more than a PAGE_SIZE!\n");
        // Currently:
        written = 0;
        status = STATUS_NOT_IMPLEMENTED;
        goto exit;
    }

    // xxx: Those usermode programs should disturb us only with serious requests. We are a busy driver.

    if (!(BufLen))
    {
        DbgPrint("Error (write): invalid request -- the caller wants to write less than one byte.\n");
        written = 0;
        status = STATUS_INVALID_PARAMETER;
        goto exit;
    }

    // Now check if the usermode program spoke the truth.
    try
    {
        // xxx: jep. that's a read from our point of view, though we are implementing a 'WriteFile'.
        ProbeForRead( toxic_buffer, BufLen, 1 );
    }
    except(EXCEPTION_EXECUTE_HANDLER)
    {

        status = GetExceptionCode();
        DbgPrint("Error (write): status 0x%08x. Write probe failed: A naughty process sent us a bad/nonexisting buffer.\n", status);
        written = 0;
        status = STATUS_INVALID_PARAMETER;
        goto exit;
    }

    // xxx: All things considered, the buffer looked good, of right size, and was accessible for reading.
    // Whatever the user is doing is his responsibility, this includes freeing the usermode buffer.
    // The usermode buffer has not been secured, because it is futile. All is up to the user now with the WRITE ENABLED.

    // We will use it to write #somewhere# #something# given by #somebody#. This is what one calls "dangerous".

    page_offset = physAddr.QuadPart % PAGE_SIZE;
    offset.QuadPart = physAddr.QuadPart - page_offset;  // Page aligned.

    // How much we need to write rounded up to the next page.
    ViewSize = BufLen + page_offset;
    ViewSize += PAGE_SIZE - (ViewSize % PAGE_SIZE);

    if (!extension->MemoryHandle)
    {
        DbgPrint("Error (write): physical device handle not available!\n"); // real error
        written = 0;
        status = STATUS_IO_DEVICE_ERROR;
        goto exit;
    }


    status = ZwMapViewOfSection(extension->MemoryHandle, ZwCurrentProcess(),
                                &mapped_buffer, 0L, PAGE_SIZE, &offset,
                                &ViewSize, ViewUnmap, 0, PAGE_READWRITE);

    if ((status != STATUS_SUCCESS) || (!mapped_buffer))
    {
        DbgPrint("Error (write): ZwMapViewOfSection failed, %lld %ld (%ld).\n", offset, ViewSize, status); // real error
        written = 0;
        status = STATUS_IO_DEVICE_ERROR;
        goto exit;
    }

    // =warning=
    // On a Windows with Hyper-V layer/VSM  (or whatever you want to name it, for me it's simply the HV layer beneath the OS.)
    // the host OS is above the Hyper-V layer. The HV (which is below the OS) can
    // and will block certain reads from certain memory locations if "it" does not want it.
    // It is happening outside of the "OS". As a rather profane kernel driver, we must live with it
    // and be prepared to be unable to read from a memory location, without any "sane" reason. (like, "out of nothing")
    // The approach: we very carefully check if it's readable and if yes, we return the bytes.
    // Otherwise we return immediately with a write error.

    try // Hyper-v/VSM induced possible write error
    {
        // "be super extra careful here."
        RtlCopyMemory(mapped_buffer + page_offset, toxic_buffer, BufLen);
    }
    except(EXCEPTION_EXECUTE_HANDLER)
    {
        DbgPrint("Error (VSM write error.).\n");
        status = STATUS_IO_DEVICE_ERROR;
        written = 0;
        ZwUnmapViewOfSection(ZwCurrentProcess(), mapped_buffer);
        goto exit;
    }

    ZwUnmapViewOfSection(ZwCurrentProcess(), mapped_buffer);

    exit:
    Irp->IoStatus.Information = written;
    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

#endif
