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

/* Read a page through the PhysicalMemory device. */
ULONG PhysicalMemoryPartialRead(IN PDEVICE_EXTENSION extension, LARGE_INTEGER offset, unsigned char * buf, ULONG count);

NTSTATUS DeviceRead(	IN PDEVICE_EXTENSION extension, 
						LARGE_INTEGER offset,
						unsigned char * toxic_buffer, ULONG howMuchToRead, 
						OUT ULONG *total_read,
						ULONG (*handler)(IN PDEVICE_EXTENSION, LARGE_INTEGER, unsigned char *, ULONG)
                    );


/* Actual read handler. */
__drv_dispatchType(IRP_MJ_READ) DRIVER_DISPATCH PmemRead;

NTSTATUS PmemRead(IN PDEVICE_OBJECT  DeviceObject, IN PIRP  Irp);

__drv_dispatchType(IRP_MJ_WRITE) DRIVER_DISPATCH PmemWrite;

NTSTATUS PmemWrite(IN PDEVICE_OBJECT  DeviceObject, IN PIRP  Irp);

#endif
