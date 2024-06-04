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


#define PMEM_DEVICE_NAME L"pmem"
#define PMEM_POOL_TAG 0x4d454d50

// In order to enable writing this must be set to 1 and the
// appropriate IOCTL must be sent to switch the driver to write mode.

#include <ntifs.h>
#include <wdmsec.h>
#include <initguid.h>
#include <stdarg.h>
#include <stdio.h>
#include <sal.h>

#include "precompiler.h"
#include "warnings.h"

#include "userspace_interface\ctl_codes.h"
#include "userspace_interface\winpmem_shared.h"

#include "pte_mmap.h"

// slightly enhanced non-null pointer checking / kernel address space sanity checking.
#if defined(_WIN64)
SIZE_T ValidKernel = 0xffff000000000000;
#else
SIZE_T ValidKernel = 0x80000000;
#endif

#define DEFAULT_SIZE_STR (250)
DECLARE_UNICODE_STRING_SIZE(eventLogKeyEntry, DEFAULT_SIZE_STR);

#ifndef NonPagedPoolNx
#define   NonPagedPoolNx    (512)
#endif


extern PUSHORT NtBuildNumber;  // (pre-existing build number.)

/*
  Our Device Extension Structure.
*/
typedef struct _DEVICE_EXTENSION
{
  /* If we read from \\Device\\PhysicalMemory, this is the handle to that. */
  HANDLE MemoryHandle;

  #if defined(_WIN64)
  /* If we read by using the PTE method  */
  PTE_METHOD_DATA  pte_data;
  #endif

  /* How we should acquire memory. */
  ULONG mode;

  ULONG WriteEnabled;

  LARGE_INTEGER CR3;  // Kernel CR3, for user info

  LARGE_INTEGER kernelbase;  // Kernelbase, for user info

  FAST_MUTEX mu;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

// 5e1ce668-47cb-410e-a664-5c705ae4d71b
DEFINE_GUID(GUID_DEVCLASS_PMEM_DUMPER,
            0x5e1ce668L,
            0x47cb,
            0x410e,
            0xa6, 0x64, 0x5c, 0x70, 0x5a, 0xe4, 0xd7, 0x1b);

#endif
