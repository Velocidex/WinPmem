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
// the handle in the device extension for mapViewOfFile usage.
static int openPhysMemSectionHandle(PDEVICE_EXTENSION extension) 
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

    if (!NT_SUCCESS(NtStatus)) 
	{
      DbgPrint("Error: failed ZwOpenSection(MemoryHandle) => %08X\n", NtStatus);
      return 0;
    }
  }

  return 1;
}


// Method I.
ULONG PhysicalMemoryPartialRead(IN PDEVICE_EXTENSION extension,
                                      LARGE_INTEGER offset, unsigned char * buf,
                                      ULONG count) 
{
	ULONG page_offset = offset.QuadPart % PAGE_SIZE;
	ULONG to_read = min(PAGE_SIZE - page_offset, count);
	PUCHAR mapped_buffer = NULL;
	SIZE_T ViewSize = PAGE_SIZE;
	NTSTATUS NtStatus;
	ULONG result = 0;

	if (!(openPhysMemSectionHandle(extension)))
	{
		DbgPrint("Error: physical device handle not available!\n");
		return 0;
	}
		
	// The mapview should never fail...
	NtStatus = ZwMapViewOfSection(extension->MemoryHandle, (HANDLE) -1,
				  &mapped_buffer, 0L, PAGE_SIZE, &offset,
				  &ViewSize, ViewUnmap, 0, PAGE_READONLY);
				  
	if (NtStatus != STATUS_SUCCESS)
	{
		DbgPrint("Error: ZwMapViewOfSection failed. Offset 0x%llX, status %08x.\n", offset.QuadPart,NtStatus);
		return 0;
	}
	
	// ... but reading from it may.
	
	
	// By the way, is that right that we create a mapview section for each tiny read?
	// Only a performance problem in any case.
	
	
	// =warning=
	// On a Windows with Hyper-V layer/VSM  (or whatever you want to name it, for me it's simply the HV layer beneath the OS.)
	// the host OS is above the Hyper-V layer. The HV (which is below the OS) can 
	// and will block certain reads from certain memory locations if "it" does not want it.
	// It is happening outside of the "OS". As a rather profane kernel driver, we must live with it 
	// and be prepared to be unable to read from a memory location, without any "sane" reason. (like, "out of nothing")
	// The approach: we very carefully check if it's readable and if yes, we return the bytes. 
	// Otherwise we are already prepared to return zeros instead.
	
	try
	{
		// "be super extra careful here." 
		RtlCopyMemory(buf, mapped_buffer + page_offset, to_read);  // ProbeForRead would not help here. This is kernel VA already. We must face the fact that we might fail the read.
		ZwUnmapViewOfSection((HANDLE)-1, mapped_buffer);
		result = to_read;
	} 
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		RtlZeroMemory(buf, to_read); // return zeros instead
		DbgPrint("Warning: unable to read %d bytes from %p, doing padding instead.\n", to_read, mapped_buffer+page_offset);
		result = to_read;
	}

	return result;
}


// Method II.
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
  ULONG result = 0;

  // Round to page size
  ViewBase.QuadPart = offset.QuadPart - page_offset;
  
  // =warning=
	// On a Windows with Hyper-V layer/VSM  (or whatever you want to name it, for me it's simply the HV layer beneath the OS.)
	// the host OS is above the Hyper-V layer. The HV (which is below the OS) can 
	// and will block certain reads from certain memory locations if "it" does not want it.
	// It is happening outside of the "OS". As a rather profane kernel driver, we must live with it 
	// and be prepared to be unable to read from a memory location, without any "sane" reason. (like, "out of nothing")
	// The approach: we very carefully check if it's readable and if yes, we return the bytes. 
	// Otherwise we are already prepared to return zeros instead.

	try 
	{
		// "be super extra careful here." 
		
		mapped_buffer = MmMapIoSpace(ViewBase, PAGE_SIZE, MmCached);  // <= may fail and BSOD on a machine with Hyper-V layer / "VSM".
		// xxx: This will BSOD on HV with a KD attached or return Null if no KD is attached. It can't be helped. It will even fail in the try & except statement!! (Bug of Microsoft?)
		// xxx: I chose the MmCached because rumor has it that the cached property is more common on RAM-backed memory, and the non-cached property is more common for BARs.

		if (mapped_buffer) 
		{
			RtlCopyMemory(buf, mapped_buffer+page_offset, to_read);
			MmUnmapIoSpace(mapped_buffer, PAGE_SIZE);
			result = to_read;
		}
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		RtlZeroMemory(buf, to_read); // return zeros instead
		DbgPrint("Warning: unable to read %d bytes from %p, doing padding instead.\n", to_read, mapped_buffer+page_offset);
		result = to_read;
		// No unmapping of the buffer: it did not go well.
	}
	
	return result;
}


// Method III.
// Read a single page using direct PTE mapping.
static ULONG PTEMmapPartialRead(IN PDEVICE_EXTENSION extension,
			       LARGE_INTEGER offset, unsigned char * buf,
			       ULONG count) 
{
	ULONG page_offset = offset.QuadPart % PAGE_SIZE;
	ULONG to_read = min(PAGE_SIZE - page_offset, count);
	LARGE_INTEGER ViewBase;
	ULONG result = 0;
	unsigned char * toxic_source = NULL;

	// Round to page size
	ViewBase.QuadPart = offset.QuadPart - page_offset;

	// Map exactly one page.
	if(extension->pte_mmapper && extension->pte_mmapper->remap_page(extension->pte_mmapper, offset.QuadPart - page_offset) == PTE_SUCCESS)
	{
		toxic_source = (unsigned char *) (extension->pte_mmapper->rogue_page.value + page_offset); // toxic, but not the userspace buffer this time.
		
		// =warning=
		// On a Windows with Hyper-V layer/VSM  (or whatever you want to name it, for me it's simply the HV layer beneath the OS.)
		// the host OS is above the Hyper-V layer. The HV (which is below the OS) can 
		// and will block certain reads from certain memory locations if "it" does not want it.
		// It is happening outside of the "OS". As a rather profane kernel driver, we must live with it 
		// and be prepared to be unable to read from a memory location, without any "sane" reason. (like, "out of nothing")
		// The approach: we very carefully check if it's readable and if yes, we return the bytes. 
		// Otherwise we are already prepared to return zeros instead.
		
		try 
		{ // "be super extra careful here." 
			
			ProbeForRead( toxic_source, to_read, 1 ); // <= Does NOT really help, (but also does not harm),
			// because ProbeForRead only checks whether the numerical address is not within userspace range. That's all it does. 
			// ProbeForWrite in contrast really probes the address and literally simulates writing a byte to it.
			// Still ProbeForRead could be considered a minor sanity check. 
			// References:
			// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-probeforread
			// https://community.osr.com/discussion/271471/why-probeforwrite-probeforread 
			
			RtlCopyMemory(buf, toxic_source, to_read);
			result = to_read;

		} except(EXCEPTION_EXECUTE_HANDLER) 
		{
			RtlZeroMemory(buf, to_read); // return zeros instead
			DbgPrint("Warning: unable to read %d bytes from %p for %p, doing padding instead.\n", to_read, toxic_source, offset.QuadPart - page_offset);
			result = to_read;
		}
	}
	// Failed to map page, or an exception occured - error out.
	return result;
}


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
	
    bytes_read = handler(extension, offset, mdl_buffer, current_read_window);
	
    if (bytes_read==0) 
	{
		DbgPrint("An error occurred: no bytes read.\n");
		MmUnlockPages(mdl);
		IoFreeMdl(mdl);
		status = STATUS_IO_DEVICE_ERROR; // That's a immediate fatal error. The userspace part should really stop ASAP on this particular read error.
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


// FAST I/O read

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
	
	UNREFERENCED_PARAMETER(FileObject);
	UNREFERENCED_PARAMETER(Wait);
	UNREFERENCED_PARAMETER(LockKey);

	PAGED_CODE();
	
	extension = DeviceObject->DeviceExtension;
	
	if(KeGetCurrentIrql() != PASSIVE_LEVEL) // Does not happen.
	{
		status = STATUS_SUCCESS;
		goto bail_out;
	}
	
	// DbgPrint("pmemFastIoReadn");
	
	
	// Do security checkings now. 
	// The usermode program is not allowed to order more than ONE PAGE_SIZE at a time.
	// But the arbitrary read/write allows up to 1-PAGESIZE bytes. So...
	
	if (!(toxic_buffer))
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
	
	// xxx: might look into ntstatus.h for nicer NTSTATUS codes.
	
	// 'Probe' if the usermode program spoke the truth.
	try 
    {
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
		DbgPrint("Error: 0x%08x, write-probe in pmemFastIoRead. Bad/nonexisting buffer.\n", status);
		// Of course now we don't continue. 
		goto bail_out;
	}
	
	// All things considered, the buffer looked good, of right size, and was accessible for write.
	// It's accepted for the next stage. Don't touch, it's still considered poisonous.

	//DbgPrint("PmemRead\n");
	//DbgPrint(" - BufLen: 0x%x\r\n", BufLen);
	//DbgPrint(" - BufOffset: 0x%llx\r\n", BufOffset.QuadPart);

	if ((extension->mode) == PMEM_MODE_PHYSICAL)
	{
		status = DeviceRead(extension, *BufOffset, toxic_buffer, BufLen, &total_read, PhysicalMemoryPartialRead);
	}
	else if ((extension->mode) == PMEM_MODE_IOSPACE)
	{
		status = DeviceRead(extension, *BufOffset, toxic_buffer, BufLen, &total_read, MapIOPagePartialRead);
	}
	else if ((extension->mode) == PMEM_MODE_PTE)
	{
		status = DeviceRead(extension, *BufOffset, toxic_buffer, BufLen, &total_read, PTEMmapPartialRead);
	}
	else
	{
		DbgPrint("Error: this acquisition mode is not supported!\n");
		IoStatus->Status = STATUS_NOT_IMPLEMENTED;
		IoStatus->Information = 0;
		return TRUE;
	}
	
	// Also check the return of Device Read. Do not simply return.
	if ((status != STATUS_SUCCESS) || (total_read == 0))
	{
		DbgPrint("Error: a fatal fast I/O read error occurred: no bytes read.\n");
		// set it to STATUS_IO_DEVICE_ERROR:
		status = STATUS_IO_DEVICE_ERROR; // The userspace part should really stop ASAP on this particular read error.
		goto bail_out;
	}
	
	// DbgPrint("Fast I/O read status on return: %08x.\n",status);
	
	IoStatus->Status = status;
	IoStatus->Information = total_read;
	return TRUE;
	
	bail_out:
	
	IoStatus->Status = status;
	IoStatus->Information = 0;
	return FALSE;
	
}



NTSTATUS PmemRead(IN PDEVICE_OBJECT  DeviceObject, IN PIRP  Irp) 
{
	PVOID toxic_buffer;       // Buffer provided by user space. Don't touch it. 
	SIZE_T buffer_address;
	ULONG BufLen;    //Buffer length for user provided buffer.
	LARGE_INTEGER BufOffset; // The file offset requested from userspace.
	PIO_STACK_LOCATION pIoStackIrp;
	PDEVICE_EXTENSION extension;
	NTSTATUS status = STATUS_SUCCESS;
	ULONG total_read = 0;

	PAGED_CODE();
	
	if(KeGetCurrentIrql() != PASSIVE_LEVEL) // Does not happen.
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
		// Of course now we don't continue. 
		
		status = STATUS_SUCCESS; // to the I/O manager: everything's under control. Nothing to see here.
		goto exit;
	}
	
	// All things considered, the buffer looked good, of right size, and was accessible for write.
	// It's accepted for the next stage. Don't touch, it's still considered poisonous.

	//DbgPrint("PmemRead\n");
	//DbgPrint(" - BufLen: 0x%x\r\n", BufLen);
	//DbgPrint(" - BufOffset: 0x%llx\r\n", BufOffset.QuadPart);

	if ((extension->mode) == PMEM_MODE_PHYSICAL)
	{
		status = DeviceRead(extension, BufOffset, toxic_buffer, BufLen, &total_read, PhysicalMemoryPartialRead);
	}
	else if ((extension->mode) == PMEM_MODE_IOSPACE)
	{
		status = DeviceRead(extension, BufOffset, toxic_buffer, BufLen, &total_read, MapIOPagePartialRead);
	}
	else if ((extension->mode) == PMEM_MODE_PTE)
	{
		status = DeviceRead(extension, BufOffset, toxic_buffer, BufLen, &total_read, PTEMmapPartialRead);
	}
	else
	{
		DbgPrint("This acquisition mode is not supported!\n");
		status = STATUS_NOT_IMPLEMENTED;
		BufLen = 0;
	}
	
	if (status == STATUS_SUCCESS)
	{
		pIoStackIrp->FileObject->PrivateCacheMap = (PVOID) -1;
	}

	exit:
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = total_read;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return status;
}

#if PMEM_WRITE_ENABLED == 1

// TODO: allowing large write window sizes.
// NOTE: I limited it to PAGE_SIZE. It's safe, but restricted. (relatively spoken)

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
		DbgPrint("Currently not implemented: the caller wants to write more than a PAGE_SIZE!\n");
		// Currently: 
		status = STATUS_NOT_IMPLEMENTED;
		goto exit;
	}
	
	// xxx: Those usermode programs should disturb us only with serious requests. We are a busy driver.
	
	if (!(BufLen))
	{
		DbgPrint("Complain: the caller  wants to write less than one byte.\n");
		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}
	
	// xxx: might look into ntstatus.h for a nicer NTSTATUS code.
	
	// Now check if the usermode program spoke the truth.
	try 
    {
		// xxx: jep. that's a read from our point of view, though we are implementing a 'WriteFile'.
		ProbeForRead( toxic_buffer, BufLen, 1 ); 
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
	
	// We will use it to write #somewhere# #something# given by #somebody#. This is what one calls "dangerous".

	page_offset = BufOffset.QuadPart % PAGE_SIZE;
	offset.QuadPart = BufOffset.QuadPart - page_offset;  // Page aligned.

	// How much we need to write rounded up to the next page.
	ViewSize = BufLen + page_offset;
	ViewSize += PAGE_SIZE - (ViewSize % PAGE_SIZE);

	/* Map memory into the Kernel AS */
	if (openPhysMemSectionHandle(extension))
	{ 
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
