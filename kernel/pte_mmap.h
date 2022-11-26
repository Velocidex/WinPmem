// Directly manipulates the page tables to map physical memory into the kernel.
// Contains everything for accessing physical memory content by direct use of PTE's.
//
// Copyright 2018 Velocidex Innovations <mike@velocidex.com>
// Copyright 2014 - 2017 Google Inc.
// Copyright 2012 Google Inc. All Rights Reserved.
// Authors: Johannes Stüttgen (johannes.stuettgen@gmail.com)
//          Michael Cohen <mike@velocidex.com>
//          Viviane Zwanger
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _PTE_MMAP_H_
#define _PTE_MMAP_H_

#if defined(_WIN64)

// Section for PTE remapping maneuvers. Fulfills special requirements.
#pragma section(".roguepage",read,write,nopage)

#endif


#define PAGE_MASK (~(PAGE_SIZE-1))

#define PFN_TO_PAGE(pfn) (pfn << PAGE_SHIFT)
#define PAGE_TO_PFN(pfn) (pfn >> PAGE_SHIFT)

#if defined(_WIN64)

__declspec(allocate(".roguepage")) unsigned char ROGUE_PAGE_MAGICMARKER[] = "GiveSectionToWinpmem=1\n";
// This needed to make PTE method work. ;-)

#endif

typedef union _VIRT_ADDR
{
  unsigned __int64 value;
  void * pointer;
  struct {
    unsigned __int64 offset        : 12;
    unsigned __int64 pt_index      :  9;
    unsigned __int64 pd_index      :  9;
    unsigned __int64 pdpt_index    :  9;
    unsigned __int64 pml4_index    :  9;
    unsigned __int64 reserved      : 16;
  };
} VIRT_ADDR, *PVIRT_ADDR;

typedef unsigned __int64 PHYS_ADDR;


#pragma pack(push, 1)
typedef union _CR3
{
  unsigned __int64 value;
  struct {
    unsigned __int64 ignored_1     : 3;
    unsigned __int64 write_through : 1;
    unsigned __int64 cache_disable : 1;
    unsigned __int64 ignored_2     : 7;
    unsigned __int64 pml4_p        :40;
    unsigned __int64 reserved      :12;
  };
} CR3, *PCR3;

typedef union _PML4E
{
  unsigned __int64 value;
  struct {
    unsigned __int64 present        : 1;
    unsigned __int64 rw             : 1;
    unsigned __int64 user           : 1;
    unsigned __int64 write_through  : 1;
    unsigned __int64 cache_disable  : 1;
    unsigned __int64 accessed       : 1;
    unsigned __int64 ignored_1      : 1;
    unsigned __int64 reserved_1     : 1;
    unsigned __int64 ignored_2      : 4;
    unsigned __int64 pdpt_p         :40;
    unsigned __int64 ignored_3      :11;
    unsigned __int64 xd             : 1;
  };
} PML4E, *PPML4E;

typedef union _PDPTE
{
  unsigned __int64 value;
  struct {
    unsigned __int64 present        : 1;
    unsigned __int64 rw             : 1;
    unsigned __int64 user           : 1;
    unsigned __int64 write_through  : 1;
    unsigned __int64 cache_disable  : 1;
    unsigned __int64 accessed       : 1;
    unsigned __int64 dirty          : 1;
    unsigned __int64 large_page      : 1;
    unsigned __int64 ignored_2      : 4;
    unsigned __int64 pd_p           :40;
    unsigned __int64 ignored_3      :11;
    unsigned __int64 xd             : 1;
  };
} PDPTE, *PPDPTE;

typedef union _PDE
{
  unsigned __int64 value;
  struct {
    unsigned __int64 present        : 1;
    unsigned __int64 rw             : 1;
    unsigned __int64 user           : 1;
    unsigned __int64 write_through  : 1;
    unsigned __int64 cache_disable  : 1;
    unsigned __int64 accessed       : 1;
    unsigned __int64 dirty          : 1;
    unsigned __int64 large_page      : 1;
    unsigned __int64 ignored_2      : 4;
    unsigned __int64 pt_p           :40;
    unsigned __int64 ignored_3      :11;
    unsigned __int64 xd             : 1;
  };
} PDE, *PPDE;

typedef union _PTE
{
  unsigned __int64 value;
  VIRT_ADDR vaddr;
  struct {
    unsigned __int64 present        : 1;
    unsigned __int64 rw             : 1;
    unsigned __int64 user           : 1;
    unsigned __int64 write_through  : 1;
    unsigned __int64 cache_disable  : 1;
    unsigned __int64 accessed       : 1;
    unsigned __int64 dirty          : 1;
    unsigned __int64 large_page     : 1;    // PAT/PS
    unsigned __int64 global         : 1;
    unsigned __int64 ignored_1      : 3;
    unsigned __int64 page_frame     :40;
    unsigned __int64 ignored_3      :11;
    unsigned __int64 xd             : 1;
  };
} PTE, *PPTE;
#pragma pack(pop)

// Loglevels to exclude debug messages from production builds.
typedef enum PTE_LOGLEVEL_
{
  PTE_ERR = 0,
  PTE_LOG,
  PTE_DEBUG
} PTE_LOGLEVEL;


// Operating system independent error checking.
typedef enum PTE_STATUS_
{
  PTE_SUCCESS = 0,
  PTE_ERROR,
  PTE_ERROR_HUGE_PAGE,
  PTE_ERROR_RO_PTE
} PTE_STATUS;


typedef struct _PTE_METHOD_DATA
{
    BOOLEAN pte_method_is_ready_to_use;
    VIRT_ADDR page_aligned_rogue_ptr;
    volatile PPTE rogue_pte;
    PHYS_ADDR original_addr;
    PTE_LOGLEVEL loglevel;
} PTE_METHOD_DATA, *PPTE_METHOD_DATA;


#endif
