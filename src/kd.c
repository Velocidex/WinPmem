/*
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

#include "kd.h"
#include "SystemModInfoInvocation.h"
#include "winpmem.h"

// Rather default queryinfo routine., requires passive level.
ULONG_PTR KernelGetModuleBaseByPtr()
{
    NTSTATUS status = STATUS_SUCCESS;

    ULONG ModuleCount = 0;
    ULONG i = 0, j=0;

    ULONG NeedSize = 0;
    ULONG preAllocateSize = 0x1000;
    PVOID pBuffer = NULL;

    ULONG_PTR imagebase_of_nt = 0;

    PSYSTEM_MODULE_INFORMATION pSystemModuleInformation;

    PAGED_CODE();

    // Preallocate 0x1000 bytes and try.
    pBuffer = ExAllocatePoolWithTag( NonPagedPoolNx, preAllocateSize, PMEM_POOL_TAG );
    if (!pBuffer)
    {
        return 0;
    }

    status = ZwQuerySystemInformation( SystemModuleInformation, pBuffer, preAllocateSize, &NeedSize );

    if (( status == STATUS_INFO_LENGTH_MISMATCH ) || (status == STATUS_BUFFER_OVERFLOW))
    {
        ExFreePool( pBuffer );
        pBuffer = ExAllocatePoolWithTag( NonPagedPoolNx, NeedSize , PMEM_POOL_TAG );
        if (!pBuffer)
        {
            return 0;
        }
        status = ZwQuerySystemInformation( SystemModuleInformation, pBuffer, NeedSize, &NeedSize );
    }

    if( !NT_SUCCESS(status) )
    {
        ExFreePool( pBuffer );
        DbgPrint("KernelGetModuleBaseByPtr() failed with %08x.\n",status);
        return 0;
    }

    pSystemModuleInformation = (PSYSTEM_MODULE_INFORMATION) pBuffer;

    ModuleCount = pSystemModuleInformation->Count;

    for( i = 0; i < ModuleCount; i++ )
    {
        for (j=0;j<250;j++)
        {
            // There are so many names for NT kernels: ntoskrnl, ntkrpamp, ...
            if (
                    ((pSystemModuleInformation->Module[i].ImageName[j+0] | 0x20) == 'n') &&
                    ((pSystemModuleInformation->Module[i].ImageName[j+1] | 0x20) == 't') &&

                    (((pSystemModuleInformation->Module[i].ImageName[j+2] | 0x20) == 'o') &&
                    ((pSystemModuleInformation->Module[i].ImageName[j+3] | 0x20) == 's'))
                    ||
                    (((pSystemModuleInformation->Module[i].ImageName[j+2] | 0x20) == 'k') &&
                    ((pSystemModuleInformation->Module[i].ImageName[j+3] | 0x20) == 'r'))
                ) // end of nt kernel name check.
                {
                    imagebase_of_nt = (ULONG_PTR) pSystemModuleInformation->Module[i].Base;
                    ExFreePool( pBuffer );
                    return imagebase_of_nt;
                }

        }
    }

    ExFreePool(pBuffer);
    return 0;
}

// Enumerate the KPCR blocks from all CPUs.

void GetKPCR(_Inout_ PWINPMEM_MEMORY_INFO info)
{
    // a bitmask of the currently active processors
    SIZE_T active_processors = KeQueryActiveProcessors();
    SIZE_T i=0;
    #if defined(_WIN64)
    SIZE_T maxProcessors=64; // xxx: 64 processors possible on 64 bit os
    #else
    SIZE_T maxProcessors=32;  // but only 32 on 32 bit, meaning that only half so much KPCR slots.
    #endif
    SIZE_T one = 1; // xxx: for correct bitshifting depending on runtime OS bitness.

    PAGED_CODE();

    RtlZeroMemory(info->KPCR, sizeof(info->KPCR) );

    for (i=0; i<maxProcessors; i++)
    {
        if (active_processors & (one << i))
        {
          KeSetSystemAffinityThread(one << i);
        #if _WIN64
              //64 bit uses gs and _KPCR.Self is at 0x18:
              info->KPCR[i].QuadPart = (uintptr_t)__readgsqword(0x18);
        #else
              //32 bit uses fs and _KPCR.SelfPcr is at 0x1c:
              info->KPCR[i].QuadPart = (uintptr_t)__readfsword(0x1c);
        #endif

        }
    }

    KeRevertToUserAffinityThread();
    // This old design would even be WinXP compatible.
    // xxx: I don't think this works for reverting to original affinity but the alternative requires Vista or higher.

    return;
}
