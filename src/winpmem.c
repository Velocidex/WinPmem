/*
  Copyright 2018 Velocidex Innovations <mike@velocidex.com>
  Copyright 2014-2017 Google Inc.
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

#include "winpmem.h"
#include "pte_mmap.c"
#include "read.c"
#include "kd.c"

_IRQL_requires_max_(PASSIVE_LEVEL)
DRIVER_UNLOAD IoUnload;

_IRQL_requires_max_(PASSIVE_LEVEL)
DRIVER_INITIALIZE DriverEntry;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS AddMemoryRanges(PWINPMEM_MEMORY_INFO info) ;

_IRQL_requires_max_(PASSIVE_LEVEL)
__drv_dispatchType(IRP_MJ_CREATE)  __drv_dispatchType(IRP_MJ_CLOSE) DRIVER_DISPATCH wddCreateClose;

_IRQL_requires_max_(PASSIVE_LEVEL)
__drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH wddDispatchDeviceControl;


#ifdef ALLOC_PRAGMA

#pragma alloc_text( PAGE , IoUnload )
#pragma alloc_text( INIT , DriverEntry )
#pragma alloc_text( PAGE , AddMemoryRanges )
#pragma alloc_text( PAGE , wddCreateClose )
#pragma alloc_text( PAGE , wddDispatchDeviceControl )
#endif


VOID IoUnload(IN PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING DeviceLinkUnicodeString;
    PDEVICE_OBJECT pDeviceObject = NULL;
     PDEVICE_EXTENSION ext = NULL;

    ASSERT(DriverObject); // xxx: No checks needed unless the kernel is in serious trouble.

    pDeviceObject = DriverObject->DeviceObject;

    // Checking every object allows to call the IoUnload routine from DriverEntry at any point for cleaning/freeing whatever needs to be freed/cleaned.

    if ((SIZE_T) pDeviceObject > (SIZE_T) MM_SYSTEM_RANGE_START)
    {
        ASSERT((SIZE_T) pDeviceObject->DeviceExtension > (SIZE_T) MM_SYSTEM_RANGE_START); // does not happen.
        ext = (PDEVICE_EXTENSION) pDeviceObject->DeviceExtension;

        #if defined(_WIN64)
        if (ext->pte_data.pte_method_is_ready_to_use) restoreOriginalRoguePage(&ext->pte_data);
        #endif
        if (ext->MemoryHandle) ZwClose(ext->MemoryHandle);

        RtlInitUnicodeString (&DeviceLinkUnicodeString, L"\\??\\" PMEM_DEVICE_NAME);
        IoDeleteSymbolicLink (&DeviceLinkUnicodeString);

        if ((SIZE_T) (DriverObject->FastIoDispatch) > (SIZE_T) MM_SYSTEM_RANGE_START)
        {
            ExFreePool(DriverObject->FastIoDispatch);
        }

        IoDeleteDevice(pDeviceObject);
    }
}


/*
  Gets information about the memory layout.

  - The Physical memory address ranges.
*/

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS AddMemoryRanges(PWINPMEM_MEMORY_INFO info)
{
  PPHYSICAL_MEMORY_RANGE MmPhysicalMemoryRange = MmGetPhysicalMemoryRanges();
  int number_of_runs = 0;

  // Enumerate address ranges.
  // This routine returns the virtual address of a nonpaged pool block which contains the physical memory ranges in the system.
  // The returned block contains physical address and page count pairs.
  // The last entry contains zero for both.
  // The caller must understand that this block can change at any point before or after this snapshot.
  // It is the caller's responsibility to free this block.

  if (MmPhysicalMemoryRange == NULL)
  {
    return STATUS_ACCESS_DENIED;
  }

  /** Find out how many ranges there are. */
  for(number_of_runs=0;
      (MmPhysicalMemoryRange[number_of_runs].BaseAddress.QuadPart) ||
        (MmPhysicalMemoryRange[number_of_runs].NumberOfBytes.QuadPart);
      number_of_runs++);

    WinDbgPrint("Memory range runs found: %d.\n",number_of_runs);

  /* Do we have enough space? */
  if (number_of_runs > NUMBER_OF_RUNS)
    {
        ExFreePool(MmPhysicalMemoryRange); // free the memory allocated by MmGetPhysicalMemoryRanges(), even in the error branches. There are multiple exit nodes here which is a bit dangerous.
        return STATUS_INFO_LENGTH_MISMATCH;
    }

  info->NumberOfRuns.QuadPart = number_of_runs;
  RtlCopyMemory(&info->Run[0], MmPhysicalMemoryRange, number_of_runs * sizeof(PHYSICAL_MEMORY_RANGE));

  ExFreePool(MmPhysicalMemoryRange);

  return STATUS_SUCCESS;
}



NTSTATUS wddCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
  if (!DeviceObject || !Irp)
  {
    return STATUS_INVALID_PARAMETER;
  }
  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = 0;

  IoCompleteRequest(Irp,IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}


NTSTATUS wddDispatchDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG IoControlCode;
    PVOID inBuffer;
    PVOID outBuffer;
    PDEVICE_EXTENSION ext;
    ULONG InputLen, OutputLen;
	
	unsigned char * mdl_inbuffer = NULL;
	unsigned char * mdl_outbuffer = NULL;
    PMDL mdl_in = NULL;  // The KM VA for the nailed UM PA.
    PMDL mdl_out = NULL; // The KM VA for the nailed UM PA.

    PAGED_CODE();

    Irp->IoStatus.Information = 0;

    if(KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        status = STATUS_SUCCESS;
        goto exit;
    }

    ext = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

    IrpStack = IoGetCurrentIrpStackLocation(Irp);

    inBuffer = IrpStack->Parameters.DeviceIoControl.Type3InputBuffer;
    outBuffer = Irp->UserBuffer;

    OutputLen = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    InputLen = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    IoControlCode = IrpStack->Parameters.DeviceIoControl.IoControlCode;
	
	// Point 1: the inputbuffer.
	
	// Not every ioctl uses an inputbuffer, but if there is one, nail it.
	
	if ((inBuffer) && ((UINT64)inBuffer < MM_USER_PROBE_ADDRESS))  // pre-check, just for shortening the fail path.
	{
		mdl_in = IoAllocateMdl(inBuffer, InputLen,  FALSE, TRUE, NULL); 
		if (!mdl_in)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto exit;
		}

		try
		{
			MmProbeAndLockPages(mdl_in, UserMode, IoReadAccess);
		}
		except(EXCEPTION_EXECUTE_HANDLER)
		{
			status = GetExceptionCode();
			DbgPrint("Error %08x: exception while locking UM inbuffer.\n", status);
			IoFreeMdl(mdl_in);
			goto exit;
		}

		// If we got here, MmProbeAndLockPages probed 
		// and we have the physical pages resident.
		// The UM VA address might still become invalid 
		// (only if the Usermode program is malicious).
		// 	Thus, we must get our own VA for the PA to be actually safe.

		mdl_inbuffer = MmGetSystemAddressForMdlSafe(mdl_in, NormalPagePriority );

		if (!mdl_inbuffer)
		{
			// According to current MSDN, when MmGetSystemAddressForMdlSafe was used, 
			// these following steps are not explicitly required. 
			// Not trusting this and prefer doing it explicitly.
			
			MmUnlockPages(mdl_in);
			IoFreeMdl(mdl_in);
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto exit;
		}
	}
	// inputbuffer, if any, is now accessible via KM VA. 
	
	// Point 2: the outputbuffer.
	
	// Not every ioctl uses an outputbuffer, but if there is one, we nail it, too.
	
	// Allocate an mdl. 
	if ((outBuffer) && ((UINT64)outBuffer < MM_USER_PROBE_ADDRESS)) // pre-check, for fail fast
	{
		mdl_out = IoAllocateMdl(outBuffer, OutputLen,  FALSE, TRUE, NULL); 
		if (!mdl_out)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto exit;
		}

		try
		{
			MmProbeAndLockPages(mdl_out, UserMode, IoWriteAccess);
		}
		except(EXCEPTION_EXECUTE_HANDLER)
		{
			status = GetExceptionCode();
			DbgPrint("Error %08x: exception while locking UM outbuffer.\n", status);
			IoFreeMdl(mdl_out);
			goto exit;
		}
		
		mdl_outbuffer = MmGetSystemAddressForMdlSafe(mdl_out, NormalPagePriority );

		if (!mdl_outbuffer)
		{
			// According to current MSDN, when MmGetSystemAddressForMdlSafe was used, 
			// these following steps are not explicitly required. 
			// Not trusting this and prefer doing it explicitly.
			
			MmUnlockPages(mdl_out);
			IoFreeMdl(mdl_out);
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto exit;
		}
		
		// Now we have an own KM VA for the UM PA stuff. 
	
	}
	
	// outputbuffer, if any, is now accessible via KM VA, using R+W PTE.

    switch (IoControlCode)
    {
	
    case IOCTL_GET_INFO:
    {
        PWINPMEM_MEMORY_INFO pInfo = NULL;

        if (!mdl_outbuffer)
        {
            DbgPrint("Error: no outbuffer in IOCTL_GET_INFO.\n");
            status = STATUS_INVALID_PARAMETER;
            goto exit;
        }

		// Check if the acquired space accomodates for the WINPMEM_MEMORY_INFO struct.
        if (OutputLen < sizeof(WINPMEM_MEMORY_INFO))
        {
            DbgPrint("Error: outbuffersize too small for the info struct!\n");
            status = STATUS_INFO_LENGTH_MISMATCH;
            goto exit;
        }

        pInfo = (PWINPMEM_MEMORY_INFO) mdl_outbuffer;

        // Ensure we clear the buffer first.
        RtlZeroMemory(pInfo, sizeof(WINPMEM_MEMORY_INFO));

        status = AddMemoryRanges(pInfo);

        if (status != STATUS_SUCCESS)
        {
            DbgPrint("Error: AddMemoryRanges returned %08x. output buffer size: 0x%x\n",status,OutputLen);
            goto exit;
        }

        WinDbgPrint("Returning info on the system memory.\n");

        // We are currently running in user context which means __readcr3() will
        // return the process CR3. So we return the kernel CR3 we found in DriverEntry in system process context.

        pInfo->CR3.QuadPart = ext->CR3.QuadPart; // The saved CR3 from DriverEntry/System process context.
        pInfo->NtBuildNumber.QuadPart = (SIZE_T) *NtBuildNumber; // value of NtBuildNumber
        pInfo->NtBuildNumberAddr.QuadPart = (SIZE_T) NtBuildNumber;  // Address of NtBuildNumber (this might not be needed anymore?)
        pInfo->KernBase.QuadPart = ext->kernelbase.QuadPart;

        // Fill in KPCR.
        GetKPCR(pInfo); 

        // This is the length of the response.
        Irp->IoStatus.Information = sizeof(WINPMEM_MEMORY_INFO);

        status = STATUS_SUCCESS;
    }; break;  // end of IOCTL_GET_INFO

    // set or change mode and check availability of neccessary functions
    case IOCTL_SET_MODE:
    {
        ULONG mode = 0;

        WinDbgPrint("Setting Acquisition mode.\n");

        if (ext->mode)
        {
            DbgPrint("Sorry, the mode has already been set to method %u! Hot resetting of the mode is not allowed for safety.\n", ext->mode);
            status = STATUS_ACCESS_DENIED;
            goto exit;
        }

        if ((!mdl_inbuffer) || (InputLen < sizeof(ULONG)))
        {
            DbgPrint("Error: no (adequate) inbuffer in IOCTL_SET_MODE.\n");
            status = STATUS_INFO_LENGTH_MISMATCH;
            goto exit;
        }

        mode = *(PULONG)mdl_inbuffer;

        switch(mode)
        {
            case PMEM_MODE_PHYSICAL:
                if (ext->MemoryHandle)
                {
                    WinDbgPrint("SET MODE: using physical memory device for acquisition.\n");
                    status = STATUS_SUCCESS;
                    ext->mode = mode;
                }
                else
                {
                    DbgPrint("Error: the acquisition mode 'physical memory device' failed setup and is not available.\n");
                    status = STATUS_NOT_SUPPORTED;
                }
                break;

            case PMEM_MODE_IOSPACE:
                // always works, if it works.
                WinDbgPrint("SET MODE: Using MmMapIoSpace for acquisition.\n");
                status = STATUS_SUCCESS;
                ext->mode = mode;
                break;

            case PMEM_MODE_PTE:

                #if defined(_WIN64)
                if (ext->pte_data.pte_method_is_ready_to_use)
                {
                    WinDbgPrint("SET MODE: Using PTE Remapping for acquisition.\n");
                    status = STATUS_SUCCESS;
                    ext->mode = mode;
                }
                else
                {
                    DbgPrint("Error: the acquisition mode PTE is not available for your system.\n");
                    status = STATUS_NOT_SUPPORTED;
                }
                #else

                DbgPrint("PTE Remapping has not been implemented on 32 bit OS.\n");
                status = STATUS_NOT_IMPLEMENTED;

                #endif

                break;

            default:
                DbgPrint("Invalid acquisition mode %u.\n", mode);
                status = STATUS_INVALID_PARAMETER;

        }; // switch mode


    }; break;  // end of IOCTL_SET_MODE


#if PMEM_WRITE_ENABLED == 1
    case IOCTL_WRITE_ENABLE: // Actually this is a switch. You can turn write support off/on again.
    {
        // No in/outbuffers needed.
        ext->WriteEnabled = !ext->WriteEnabled;
        DbgPrint("Write mode is %u. Do you know what you are doing?\n", ext->WriteEnabled);
        status = STATUS_SUCCESS;

    }; break;  // end of IOCTL_WRITE_ENABLE
#endif

    case IOCTL_REVERSE_SEARCH_QUERY:
    {
        #if defined(_WIN64)

        PTE_STATUS pte_status = PTE_SUCCESS;
        VIRT_ADDR In_VA;
        volatile PPTE pPTE;
        PHYS_ADDR Out_PhysAddr = 0;
        ULONG page_offset;

        if (!ext->pte_data.pte_method_is_ready_to_use)
        {
            DbgPrint("Error: the acquisition mode PTE is not available for your system.\n");
            status = STATUS_NOT_SUPPORTED;
        }
		
		if ((!mdl_inbuffer) || (InputLen < sizeof(UINT64)))
        {
            DbgPrint("Error: no (adequate) inbuffer in IOCTL_REVERSE_SEARCH_QUERY.\n");
            status = STATUS_INFO_LENGTH_MISMATCH;
            goto exit;
        }
		
		if ((!mdl_outbuffer) || (OutputLen < sizeof(UINT64)))
        {
            DbgPrint("Error: no (adequate) outbuffer in IOCTL_REVERSE_SEARCH_QUERY.\n");
            status = STATUS_INFO_LENGTH_MISMATCH;
            goto exit;
        }
		
		In_VA.value = *(PUINT64) mdl_inbuffer;

		WinDbgPrint("REVERSE SEARCH QUERY for: VA %llx.\n", In_VA.value);

		page_offset = (ULONG) In_VA.offset;
		In_VA.value -= page_offset;

		ASSERT(!In_VA.offset);

		// At least one sanity check.
		if (!In_VA.value)
		{
			DbgPrint("Error: invoker specified 0 as virtual address. Mistake?\n");
			status = STATUS_ACCESS_DENIED;
			goto exit;
		}

		pte_status = virt_find_pte(In_VA, &pPTE);

		if (pte_status != PTE_SUCCESS)
		{
			// Remember todo: reverse search currently returns error on large pages (because not implemented).
			WinDbgPrint("Reverse search found nothing: no present page for %llx. Sorry.\n", In_VA.value);
			Out_PhysAddr = 0;
		}
		else
		{
			if (pPTE->present) // But virt_find_pte checked that already.
			{
				if (!pPTE->large_page)
				{
					Out_PhysAddr = ( PFN_TO_PAGE(pPTE->page_frame) ) + page_offset;  // normal calculation.
				}
				else
				{
					Out_PhysAddr = ( PFN_TO_PAGE(( pPTE->page_frame +  In_VA.pt_index)) ) + page_offset; // Large page calculation.
				}
			}
			else
			{
				WinDbgPrint("Valid bit not set in PTE. Sorry.\n");
				Out_PhysAddr = 0;
			}
		}

		*(PUINT64) mdl_outbuffer = Out_PhysAddr;

		Irp->IoStatus.Information = sizeof(UINT64);
		status = STATUS_SUCCESS;

        #else

        WinDbgPrint("Not implemented on 32 bit OS.\n");
        status = STATUS_NOT_IMPLEMENTED;

        #endif

    } ; break; // IOCTL_REVERSE_SEARCH_QUERY

    default:
    {
        WinDbgPrint("Invalid IOCTRL %u\n", IoControlCode);
        status = STATUS_INVALID_PARAMETER;
    };
  }

 exit:
 
// unlock & free all mdls we might have.
// According to current MSDN, when MmGetSystemAddressForMdlSafe was used, 
// unlock & freeing is not explicitly required. 
// Not trusting this and prefer doing it explicitly.

	if (mdl_outbuffer)
	{
		MmUnlockPages(mdl_out);
		IoFreeMdl(mdl_out);
	}
	
	if (mdl_inbuffer)
	{
		MmUnlockPages(mdl_in);
		IoFreeMdl(mdl_in);
	}
 
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp,IO_NO_INCREMENT);
	return status;
}



NTSTATUS DriverEntry (IN PDRIVER_OBJECT DriverObject,
                      IN PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName, DeviceLink;
    NTSTATUS ntstatus;
    PDEVICE_OBJECT DeviceObject = NULL;
    PDEVICE_EXTENSION extension;
    ULONG FastioTag = 0x54505346;
	UINT64 cr4 = 0;  // for level 5 check.

    UNREFERENCED_PARAMETER(RegistryPath);

    WinDbgPrint("WinPMEM - " PMEM_DRIVER_VERSION " \n");

    #if PMEM_WRITE_ENABLED == 1
    WinDbgPrint("WinPMEM write support available!\n");
    #endif

    RtlInitUnicodeString (&DeviceName, L"\\Device\\" PMEM_DEVICE_NAME);

    // We create our secure device.
    // http://msdn.microsoft.com/en-us/library/aa490540.aspx
    ntstatus = IoCreateDeviceSecure(DriverObject,
                                  sizeof(DEVICE_EXTENSION),
                                  &DeviceName,
                                  FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN,
                                  FALSE,
                                  &SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
                                  &GUID_DEVCLASS_PMEM_DUMPER,
                                  &DeviceObject);

    if (!NT_SUCCESS(ntstatus))
    {
        DbgPrint ("IoCreateDevice failed. => %08X\n", ntstatus);
        // nothing to free until here.
        return ntstatus;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = wddCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = wddCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = wddDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = PmemRead; // copies tons of data, always in the range of Gigabytes.

    DriverObject->FastIoDispatch = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(FAST_IO_DISPATCH), FastioTag);
    // In contrast to the MSDN documentation, Using 512/NonPagedPoolNx for ExAllocate will work on Win7 just fine -- although it will be treated as 0/nonpaged (rwx) nonetheless. There is no NX pool in Win7.

    if (NULL == DriverObject->FastIoDispatch)
    {
        // This branch will never be taken unless the OS is about to die.
        DbgPrint("Error: allocation failed (Fast I/O table)!\n\n");

        // The unload routine will take care of deleting things, but we must call it here if we return error in DriverEntry.
        IoUnload(DriverObject); // Calling the Unload Routine or freeing all things that happened until here is actually required.

        ntstatus = STATUS_INSUFFICIENT_RESOURCES;
        goto error;
    }

    RtlZeroMemory(DriverObject->FastIoDispatch, sizeof(FAST_IO_DISPATCH));
    DriverObject->FastIoDispatch->SizeOfFastIoDispatch = sizeof(FAST_IO_DISPATCH);
    DriverObject->FastIoDispatch->FastIoRead = pmemFastIoRead;

    #if PMEM_WRITE_ENABLED == 1
    {
    // Make sure that the drivers with write support are clearly marked as such.
    static char TAG[] = "Write Supported";
    }

    // Support writing.
    DriverObject->MajorFunction[IRP_MJ_WRITE] = PmemWrite;

    #endif

    DriverObject->DriverUnload = IoUnload;

    DeviceObject->Flags &= ~DO_DIRECT_IO;
    DeviceObject->Flags &= ~DO_BUFFERED_IO;

    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING; // I/O manager will do that even if we don't because it's in the driver entry.

    RtlInitUnicodeString(&DeviceLink, L"\\??\\" PMEM_DEVICE_NAME);

    ntstatus = IoCreateSymbolicLink(&DeviceLink, &DeviceName);

    if (!NT_SUCCESS(ntstatus))
    {
        DbgPrint("IoCreateSymbolicLink failed. => %08X\n", ntstatus);
        // The unload routine will take care of deleting things and we should not double free things.
        IoUnload(DriverObject); // Calling the Unload Routine or freeing all things that happened until here is actually required.

        goto error;
    }

    // Initialize the device extension with safe defaults.
    extension = DeviceObject->DeviceExtension;

    RtlZeroMemory(extension, sizeof(DEVICE_EXTENSION)); // ensure device extension is really zeroed out.

        // Populate globals in kernel context.
    // Used when virtual addressing is enabled, hence when the PG bit is set in CR0.
    // CR3 enables the processor to translate linear addresses into physical addresses by locating the page directory and page tables for the current task.
    // Typically, the upper 20 bits of CR3 become the page directory base register (PDBR), which stores the physical address of the first page directory entry.
    // If the PCIDE bit in CR4 is set, the lowest 12 bits are used for the process-context identifier (PCID)
    extension->CR3.QuadPart = __readcr3();

    extension->kernelbase.QuadPart = KernelGetModuleBaseByPtr();

    // Setup physical memory device handle from Windows.
    if (!setupPhysMemSectionHandle(&extension->MemoryHandle))
    {
        DbgPrint("Warning: physical device handle not available! (You will not be able to use this method).\n");
        // This method is deactivated. (handle is zero).
    }

    #if defined(_WIN64)
    // setup PTE mode part.
	
	// Check if level 5 present (la57).
	// We only support level 4.
	cr4 = __readcr4();
	if (cr4 & (1 << 12)) // la57 bit set?
	{
		DbgPrint("Warning: level 5 paging system found.\n");
		DbgPrint("You can try the physical memory device method, but the PTE method is not implemented for level 5.\n");
		DbgPrint("Warning: Winpmem has never been tested on level 5 paging systems.\n");
		extension->pte_data.pte_method_is_ready_to_use = FALSE;
	}
	else // PTE method setup.
	{
		if (!setupBackupForOriginalRoguePage(&extension->pte_data))
		{
			extension->pte_data.pte_method_is_ready_to_use = FALSE;
			DbgPrint("Warning: PTE method failed (unknown reason)!\n(You will not be able to use this method).\n");
		}
		else
		{
			// Indicate it is usable now.
			extension->pte_data.pte_method_is_ready_to_use = TRUE;
		}
	}
    #endif

    ExInitializeFastMutex(&extension->mu);

    WinDbgPrint("Driver initialization completed.\n");
    return ntstatus;

    error:
    return STATUS_UNSUCCESSFUL;
}

