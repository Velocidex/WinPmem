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

#ifndef _WINPMEM_KD_H
#define _WINPMEM_KD_H

#include "winpmem.h"
#include "ntimage.h"

SIZE_T KernelGetModuleBaseByPtr();

PVOID KernelGetProcAddress(void *image_base, char *func_name);

void GetKPCR(PWINPMEM_MEMORY_INFO info);

#ifdef ALLOC_PRAGMA

#pragma alloc_text( PAGE , KernelGetModuleBaseByPtr )
#pragma alloc_text( PAGE , KernelGetProcAddress ) 
#pragma alloc_text( PAGE , GetKPCR ) 

#endif

#endif // end of _WINPMEM_KD_H
