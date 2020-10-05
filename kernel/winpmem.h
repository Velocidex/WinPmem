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

#ifndef _WINPMEM_H_
#define _WINPMEM_H_

// Supposed to control whether there should be DbgPrint or not. (?)
#define SILENT_OPERATION 0

// Really verbose debugging.
#define VERBOSE_DEBUG 0

#define PMEM_DEVICE_NAME L"pmem"
#define PMEM_POOL_TAG 0x4d454d50

// In order to enable writing this must be set to 1 and the
// appropriate IOCTL must be sent to switch the driver to write mode.
// #define PMEM_WRITE_ENABLED 1


#include <ntifs.h>
#include <wdmsec.h>
#include <initguid.h>
#include <stdarg.h>
#include <stdio.h>

#include "userspace_interface\ctl_codes.h"
#include "userspace_interface\winpmem_shared.h"

#include "pte_mmap.h"

#define MI_CONVERT_PHYSICAL_TO_PFN(Pa) (Pa >> 12)

extern PUSHORT NtBuildNumber;  // (pre-existing build number.)

/*
struct PmemMemoryControl {
  u32 mode;    //really: enum PMEM_ACQUISITION_MODE mode but we want to enforce
               //standard struct sizes.;
};
*/

/* When we are silent we do not emit any debug messages. */
#if SILENT_OPERATION == 1
#define WinDbgPrint(fmt, ...)
#define vWinDbgPrintEx(x, ...)
#else
#define WinDbgPrint DbgPrint
#define vWinDbgPrintEx vDbgPrintEx
#endif

# if VERBOSE_DEBUG == 1
#define WinDbgPrintDebug DbgPrint
# else
#define WinDbgPrintDebug(fmt, ...)
#endif

// Add verbose debugging to PCI code.
#define WINPMEM_PCI_DEBUG 0


/*
  Our Device Extension Structure.
*/
typedef struct _DEVICE_EXTENSION
{
  /* If we read from \\Device\\PhysicalMemory, this is the handle to that. */
  HANDLE MemoryHandle;

  /* How we should acquire memory. */
  enum WDD_ACQUISITION_MODE mode;

  int WriteEnabled;

  /* Hold a handle to the pte_mmap object. */
  PTE_MMAP_OBJ *pte_mmapper;

  FAST_MUTEX mu;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// 5e1ce668-47cb-410e-a664-5c705ae4d71b
DEFINE_GUID(GUID_DEVCLASS_PMEM_DUMPER,
            0x5e1ce668L,
            0x47cb,
            0x410e,
            0xa6, 0x64, 0x5c, 0x70, 0x5a, 0xe4, 0xd7, 0x1b);

#endif
