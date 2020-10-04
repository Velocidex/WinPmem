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

// xxx: 
// This function's secret unspoken true name is "openPhysMemSectionHandle".
// It opens a section handle to the physicalMemory device and quicksaves the handle in the device extension for mapViewOfFile usage.
// It was named MemoryHandle, because we do not need to let everybody know more than that.
static int EnsureExtensionHandle(PDEVICE_EXTENSION extension) 
{
  NTSTATUS NtStatus;
  UNICODE_STRING PhysicalMemoryPath;
  OBJECT_ATTRIBUTES MemoryAttributes;

  /* Make sure we have a valid handle now. */
  if(!extension->MemoryHandle) 
  {
    RtlInitUnicodeString(&PhysicalMemoryPath, L"\\Device\\PhysicalMemory");

    InitializeObjectAttributes(&MemoryAttributes,
                               &PhysicalMemoryPath,
                               OBJ_KERNEL_HANDLE,
                               (HANDLE) NULL,
                               (PSECURITY_DESCRIPTOR) NULL);

    NtStatus = ZwOpenSection(&extension->MemoryHandle,
                             SECTION_MAP_READ, &MemoryAttributes);

    if (!NT_SUCCESS(NtStatus)) {
      WinDbgPrint("Failed ZwOpenSection(MemoryHandle) => %08X\n", NtStatus);
      return 0;
    }
  }

  return 1;
}


ULONG PhysicalMemoryPartialRead(IN PDEVICE_EXTENSION extension,
                                      LARGE_INTEGER offset, unsigned char * buf,
                                      ULONG count) 
{
  ULONG page_offset = offset.QuadPart % PAGE_SIZE;
  ULONG to_read = min(PAGE_SIZE - page_offset, count);
  PUCHAR mapped_buffer = NULL;
  SIZE_T ViewSize = PAGE_SIZE;
  NTSTATUS NtStatus;

  if (EnsureExtensionHandle(extension)) // xxx: Of course! Totally ensuring the device extension handle. ;-p
  {
    /* Map page into the Kernel AS */
    NtStatus = ZwMapViewOfSection(extension->MemoryHandle, (HANDLE) -1,
				  &mapped_buffer, 0L, PAGE_SIZE, &offset,
				  &ViewSize, ViewUnmap, 0, PAGE_READONLY);

    if (NT_SUCCESS(NtStatus)) {
      RtlCopyMemory(buf, mapped_buffer + page_offset, to_read);
      ZwUnmapViewOfSection((HANDLE)-1, mapped_buffer);

    } else {
      WinDbgPrint("Failed to Map page at 0x%llX\n", offset.QuadPart);
      RtlZeroMemory(buf, to_read);
    };
  };

  return to_read;
};


// Read a single page using MmMapIoSpace.
static ULONG MapIOPagePartialRead(IN PDEVICE_EXTENSION extension,
                                 LARGE_INTEGER offset, unsigned char * buf,
                                 ULONG count) 
{
  UNREFERENCED_PARAMETER(extension);
  ULONG page_offset = offset.QuadPart % PAGE_SIZE;
  ULONG to_read = min(PAGE_SIZE - page_offset, count);
  PUCHAR mapped_buffer = NULL;
  LARGE_INTEGER ViewBase;

  // Round to page size
  ViewBase.QuadPart = offset.QuadPart - page_offset;

  // Map exactly one page.
  mapped_buffer = MmMapIoSpace(ViewBase, PAGE_SIZE, MmCached);  
  // xxx: This will BSOD on HV with a KD attached or return Null if no KD is attached. It can't be helped.
  // xxx: I chose the MmCached because rumor has it that the cached property is more common on RAM-backed memory, and the non-cached property is more common for BARs.
  //      it's unsafe per definitionem, because this reverse way is not the intended usage.

	if (mapped_buffer) 
	{
		try 
		{
		  // Be extra careful here to not produce a BSOD.
		  RtlCopyMemory(buf, mapped_buffer+page_offset, to_read);
		} 
		except(EXCEPTION_EXECUTE_HANDLER) 
		{
		  WinDbgPrintDebug("Unable to read %d bytes from %p for %p\n", to_read, source, offset.QuadPart - page_offset);
		  MmUnmapIoSpace(mapped_buffer, PAGE_SIZE);
		  return 0;
		}

		MmUnmapIoSpace(mapped_buffer, PAGE_SIZE);
		return to_read;
		} 
	else 
	{
		// Failed to map page, return 0, to match it with the other functions (physical device).
		return 0;
	}
}


// Read a single page using direct PTE mapping.
static ULONG PTEMmapPartialRead(IN PDEVICE_EXTENSION extension,
			       LARGE_INTEGER offset, unsigned char * buf,
			       ULONG count) 
{
  ULONG page_offset = offset.QuadPart % PAGE_SIZE;
  ULONG to_read = min(PAGE_SIZE - page_offset, count);
  LARGE_INTEGER ViewBase;
  ULONG result = 0;

  // Round to page size
  ViewBase.QuadPart = offset.QuadPart - page_offset;

  // Map exactly one page.
  if(extension->pte_mmapper &&
     extension->pte_mmapper->remap_page(extension->pte_mmapper,
					offset.QuadPart - page_offset) ==
     PTE_SUCCESS) {
    char *source = (char *)(extension->pte_mmapper->rogue_page.value + page_offset);
    try {
      // Be extra careful here to not produce a BSOD. 
      // We would rather return an error than a BSOD.
      RtlCopyMemory(buf, source, to_read);
	  result = to_read;

    } except(EXCEPTION_EXECUTE_HANDLER) {
      WinDbgPrintDebug("Unable to read %d bytes from %p for %p\n", to_read, source, offset.QuadPart - page_offset);
	}
  }
  // Failed to map page, or an exception occured - error out.
  return result;
};


// xxx: Maybe we should have some device state that can be set to "DUMP_IN_PROGRESS". 
// It should be avoided in first place to do more than one DUMP acquisition at the same time simultaneously.
NTSTATUS DeviceRead(IN PDEVICE_EXTENSION extension, 
					LARGE_INTEGER offset,
                    unsigned char * toxic_buffer, ULONG howMuchToRead, 
					OUT ULONG *total_read,
                    ULONG (*handler)(IN PDEVICE_EXTENSION, LARGE_INTEGER, unsigned char *, ULONG))
{
  ULONG bytes_read = 0;
  ULONG current_read_window = 0;
  unsigned char * mdl_buffer = NULL;
  PMDL mdl = NULL;
  NTSTATUS status = STATUS_SUCCESS;
  
  ASSERT(current_read_window <= howMuchToRead);

  *total_read = 0;

  ExAcquireFastMutex(&extension->mu);
  
  while (*total_read < howMuchToRead) 
  {
	current_read_window =  min(PAGE_SIZE, howMuchToRead - *total_read);
	
	// Scudette does not maintain an offset to the (toxic) buffer.
	// Instead he was increasing the buffer address itself. 
	// We will overtake this behavior for the sake of keeping the 'hacky style' of the original code. ;-p
	
	// Allocate an mdl. Must be freed afterwards (if the call succeeds).
	mdl = IoAllocateMdl(toxic_buffer, current_read_window,  FALSE, TRUE, NULL); // <= toxic buffer address increases each time in the loop.
	if (!mdl)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	
	try 
	{
		MmProbeAndLockPages(mdl, UserMode, IoWriteAccess); 
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{

		DbgPrint("Exception while probe-writing and locking NEITHER I/O buffer 0x%08x.\n", status);
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
	
    bytes_read = handler(extension, offset, mdl_buffer, current_read_window);
	
    if (bytes_read==0) 
	{
		DbgPrint("An error occurred: no bytes read.\n");
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		status = STATUS_IO_DEVICE_ERROR;
		goto end;
	}
	
	MmUnlockPages(mdl);
	IoFreeMdl(mdl);

    offset.QuadPart += bytes_read;
    toxic_buffer += bytes_read;
    *total_read += bytes_read;
	
  }

end:
  ExReleaseFastMutex(&extension->mu);
  return status;
}



NTSTATUS PmemRead(IN PDEVICE_OBJECT  DeviceObject, IN PIRP  Irp) 
{
	PVOID toxic_buffer;       // Buffer provided by user space. Don't touch it. 
	ULONG BufLen;    //Buffer length for user provided buffer.
	LARGE_INTEGER BufOffset; // The file offset requested from userspace.
	PIO_STACK_LOCATION pIoStackIrp;
	PDEVICE_EXTENSION extension;
	NTSTATUS status = STATUS_SUCCESS;
	ULONG total_read = 0;

	PAGED_CODE();

	// xxx: no way this could possibly run on irql in this driver. 
	// On second thought, there might be a malicious driver trying to kill winpmem 
	// with a created IRP of IRQL > 0 (paranoid, I know). In this case ...
	
	if(KeGetCurrentIrql() != PASSIVE_LEVEL) 
	{
		status = STATUS_SUCCESS;
		goto exit;
	}

	extension = DeviceObject->DeviceExtension;

	pIoStackIrp = IoGetCurrentIrpStackLocation(Irp);
	BufLen = pIoStackIrp->Parameters.Read.Length;
	BufOffset = pIoStackIrp->Parameters.Read.ByteOffset;
	toxic_buffer =  Irp->UserBuffer; 
	// xxx: what we get from userspace is inside here for the NEITHER I/O type. 
	
	// xxx:
	// maybe it's even a kernel driver or rootkit calling us for help.
	// As long as it's coming on irql 0, we will gladly help out.
	// I will be referring to it mostly as the "usermode program", but also keep in mind it could be a kernelmode system thread.
	
	// Do security checkings now. 
	// The usermode program is not allowed to order more than ONE PAGE_SIZE at a time.
	// But the arbitrary read/write allows up to 1-PAGESIZE bytes. So...
	
	if (!(toxic_buffer))
	{
		DbgPrint("error: provided buffer in read request was invalid.\n");
		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}
	
	// xxx: Those usermode programs should disturb us only with serious requests. We are a busy driver.
	
	if (!(BufLen))
	{
		DbgPrint("Complain: the caller  wants to read less than one byte.\n");
		// xxx: What would be good? "Not implemented"? Nah. Better STATUS_INVALID_PARAMETER.
		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}
	
	// xxx: might look into ntstatus.h for nicer NTSTATUS codes.
	
	// Check if the usermode program spoke the truth.
	try 
    {
		ProbeForWrite( toxic_buffer, BufLen, 1 ); 
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{

		status = GetExceptionCode();
		DbgPrint("Error: 0x%08x, probe in PmemRead. A naughty process sent us a bad/nonexisting buffer.\n", status);
		// Of course now we don't continue. 
		
		status = STATUS_SUCCESS; // to the I/O manager: everything's under control. Nothing to see here.
		goto exit;
	}
	
	// All things considered, the buffer looked good, of right size, and was accessible for write.
	// It's accepted for the next stage. Don't touch, it's still considered poisonous.

	//DbgPrint("PmemRead\n");
	//DbgPrint(" - BufLen: 0x%x\r\n", BufLen);
	//DbgPrint(" - BufOffset: 0x%llx\r\n", BufOffset.QuadPart);

	if ((extension->mode) == ACQUISITION_MODE_PHYSICAL_MEMORY)
	{
		status = DeviceRead(extension, BufOffset, toxic_buffer, BufLen, &total_read, PhysicalMemoryPartialRead);
	}
	else if ((extension->mode) == ACQUISITION_MODE_MAP_IO_SPACE)
	{
		status = DeviceRead(extension, BufOffset, toxic_buffer, BufLen, &total_read, MapIOPagePartialRead);
	}
	else if ((extension->mode) == ACQUISITION_MODE_PTE_MMAP)
	{
		status = DeviceRead(extension, BufOffset, toxic_buffer, BufLen, &total_read, PTEMmapPartialRead);
	}
	else
	{
		DbgPrint("This acquisition mode is not supported!\n");
		status = STATUS_NOT_IMPLEMENTED;
		BufLen = 0;
	}

	exit:
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = total_read;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

#if PMEM_WRITE_ENABLED == 1

// TODO: securing for large writes.
// WARNING: not fully secured yet! Writing more than a PAGE can fail!
// That's why I limited it to PAGE_SIZE. Until I finish with that, you are safe, but restricted. (relatively spoken)

NTSTATUS PmemWrite(IN PDEVICE_OBJECT  DeviceObject, IN PIRP  Irp) 
{
	PVOID toxic_buffer;       //Buffer provided by user space.
	ULONG BufLen;    //Buffer length for user provided buffer.
	LARGE_INTEGER BufOffset; // The file offset requested from userspace.
	PIO_STACK_LOCATION pIoStackIrp;
	PDEVICE_EXTENSION extension;
	NTSTATUS status = STATUS_SUCCESS;
	SIZE_T ViewSize = PAGE_SIZE;
	PUCHAR mapped_buffer = NULL;
	ULONG page_offset = 0;
	LARGE_INTEGER offset;

	PAGED_CODE();

	// xxx: no way this could possibly run on irql in this driver. 
	// On second thought, there might be a malicious driver trying to kill winpmem 
	// with a created IRP of IRQL > 0 (paranoid, I know). In this case ...

	if (KeGetCurrentIrql() != PASSIVE_LEVEL) 
	{
		status = STATUS_SUCCESS;
		goto exit;
	}

	extension = DeviceObject->DeviceExtension;

	if (!extension->WriteEnabled) 
	{
		status = STATUS_ACCESS_DENIED;
		WinDbgPrint("Write mode not enabled.\n");
		goto exit;
	}

	pIoStackIrp = IoGetCurrentIrpStackLocation(Irp);
	
	// buffer length is going to be checked for being between 1-PAGESIZE.
	BufLen = pIoStackIrp->Parameters.Write.Length;

	// Where to write exactly.
	BufOffset = pIoStackIrp->Parameters.Write.ByteOffset;
	
	toxic_buffer = pIoStackIrp->Parameters.DeviceIoControl.Type3InputBuffer;
	// Buf = (PCHAR)(Irp->AssociatedIrp.SystemBuffer);
	
	// xxx: hope the caller knows what he's doing here. That's an arbitrary read to anywhere from usermode.
	// Maybe we should insert some kind of quick puzzle to ensure the caller is sane.
	
	// Security checks now.
	
	if (!(toxic_buffer))
	{
		DbgPrint("error: provided buffer in write request was invalid.\n");
		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}
	
	if (BufLen > PAGE_SIZE)
	{
		DbgPrint("Complain: the caller wants to write more than a PAGE_SIZE!\n");
		// xxx: We cannot clamp it to PAGE_SIZE, that might lead to corrupt data for the usermode program.
		// Better directly reject this request then. 
		// What would be good? "Not implemented"?
		status = STATUS_NOT_IMPLEMENTED;
		goto exit;
	}
	
	// xxx: Those usermode programs should disturb us only with serious requests. We are a busy driver.
	
	if (!(BufLen))
	{
		DbgPrint("Complain: the caller  wants to write less than one byte.\n");
		// xxx: What would be good? "Not implemented"?
		status = STATUS_NOT_IMPLEMENTED;
		goto exit;
	}
	
	// xxx: might look into ntstatus.h for a nicer NTSTATUS code.
	
	// Now check if the usermode program spoke the truth.
	try 
    {
		// xxx: jep. that's a read from our point of view, though we are implementing a WriteFile.
		ProbeForRead( toxic_buffer, BufLen, 1 ); // xxx: If we would drop the arbitrary read/write primitive from usermode, 
		// we could put something else then 1 here.
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{

		status = GetExceptionCode();
		DbgPrint("Error: 0x%08x, probe in PmemWrite. A naughty process sent us a bad/nonexisting buffer.\n", status);
		// Of course now we don't continue. 
		
		status = STATUS_SUCCESS; // to the I/O manager: everything's under control. Nothing to see.
		goto exit;
	}
	
	// xxx: All things considered, the buffer looked good, of right size, and was accessible for reading.
	
	// We will use it to write #somewhere# #something# given by #somebody#. Incredible. I like the 'solve puzzle first' idea.

	page_offset = BufOffset.QuadPart % PAGE_SIZE;
	offset.QuadPart = BufOffset.QuadPart - page_offset;  // Page aligned.

	// How much we need to write rounded up to the next page.
	ViewSize = BufLen + page_offset;
	ViewSize += PAGE_SIZE - (ViewSize % PAGE_SIZE);

	/* Map memory into the Kernel AS */
	if (EnsureExtensionHandle(extension)) // xxx: totally ensuring the device extension handle. *nods*
	{   								  // xxx: I like that we use this method for the arbitrary write, it's the most safest (and slowest).
		status = ZwMapViewOfSection(extension->MemoryHandle, (HANDLE) -1,
				&mapped_buffer, 0L, PAGE_SIZE, &offset,
				&ViewSize, ViewUnmap, 0, PAGE_READWRITE);

		if (NT_SUCCESS(status)) 
		{
		  RtlCopyMemory(mapped_buffer + page_offset, toxic_buffer, BufLen);
		  ZwUnmapViewOfSection((HANDLE)-1, mapped_buffer);
		}
		else 
		{
		  WinDbgPrint("Failed to map view %lld %ld (%ld).\n", offset, ViewSize, status);
		}
	}

	exit:
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

#endif
