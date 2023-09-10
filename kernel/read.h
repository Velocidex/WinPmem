/**
   This file implement methods for reading through the pmem device.

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

#ifndef __READ_H
#define __READ_H

#include "winpmem.h"

_IRQL_requires_max_(PASSIVE_LEVEL)
    BOOLEAN setupPhysMemSectionHandle(_Out_ PHANDLE pMemoryHandle);

_IRQL_requires_max_(PASSIVE_LEVEL)
    BOOLEAN pmemFastIoRead (
    __in PFILE_OBJECT FileObject,
    __in PLARGE_INTEGER BufOffset,
    __in ULONG BufLen,
    __in BOOLEAN Wait,
    __in ULONG LockKey,
    __out_bcount(BufLen) PVOID toxic_buffer,
    __out PIO_STATUS_BLOCK IoStatus,
    __in PDEVICE_OBJECT DeviceObject );

_IRQL_requires_max_(PASSIVE_LEVEL)
    __drv_dispatchType(IRP_MJ_READ) DRIVER_DISPATCH PmemRead;

_IRQL_requires_max_(PASSIVE_LEVEL)
    __drv_dispatchType(IRP_MJ_WRITE) DRIVER_DISPATCH PmemWrite;

_IRQL_requires_max_(APC_LEVEL)
    NTSTATUS DeviceRead(_In_ PDEVICE_EXTENSION extension,
                    _In_ LARGE_INTEGER physAddr_cursor,
                    _Inout_ unsigned char * toxic_buffer_cursor, _In_ ULONG howMuchToRead,
                    _Out_ PULONG total_read);

_IRQL_requires_max_(PASSIVE_LEVEL)
    ULONG PhysicalMemoryPartialRead(_In_ HANDLE memoryHandle, _In_ LARGE_INTEGER physAddr, _Inout_ unsigned char * buf, _In_ ULONG count);

// Capable of working higher than PASSIVE level, but not needed.
ULONG MapIOPagePartialRead(_In_ LARGE_INTEGER physAddr, _Inout_ unsigned char * buf, _In_ ULONG count);

_IRQL_requires_max_(APC_LEVEL)
    ULONG PTEMmapPartialRead(_Inout_ PPTE_METHOD_DATA pPtedata, _In_ LARGE_INTEGER physAddr, _Inout_ unsigned char * buf, _In_ ULONG count);


#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE , setupPhysMemSectionHandle )
#pragma alloc_text( PAGE , pmemFastIoRead )
#pragma alloc_text( PAGE , PmemRead )
#pragma alloc_text( PAGE , PmemWrite )
#pragma alloc_text( NONPAGED , DeviceRead )
#pragma alloc_text( NONPAGED , PhysicalMemoryPartialRead )
#pragma alloc_text( NONPAGED , MapIOPagePartialRead )
#pragma alloc_text( NONPAGED , PTEMmapPartialRead )
#endif

// The very often called routines should be in nonpaged memory, it would waste time if they were paged out.

#endif
