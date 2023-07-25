// Copyright 2018 Velocidex Innovations <mike@velocidex.com>
// Copyright 2014 - 2017 Google Inc.
// Copyright 2012 Google Inc. All Rights Reserved.
// Author: Viviane Zwanger,
// derived from Rekall/WinPmem by Mike Cohen and Johannes Stüttgen.
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

#ifndef PAGE_SIZE
#define PAGE_SIZE (4096)
#endif

// not used / not needed.
#ifndef LARGE_PAGE_SIZE
#define LARGE_PAGE_SIZE (2097152)
#endif

#ifndef PAGE_MASK
#define PAGE_MASK (~(PAGE_SIZE-1))
#endif

#ifndef PFN_TO_PAGE
#define PFN_TO_PAGE(pfn) (pfn << PAGE_SHIFT)
#define PAGE_TO_PFN(pfn) (pfn >> PAGE_SHIFT)
#endif

#ifndef FALSE
#define  FALSE    (0)
#define  TRUE     (1)
#endif

// # Critical #
// Do not remove decorations.
// YOU MUST NOT remove the guardian pages on both sides of the sacrifice area. It leads to certain death. Tested.

static char g_guardingPage_after[] __attribute__((section("roguepage"))) __attribute__((aligned(4096))) = "This marks the guard area after the sacrifice page.";

static char g_magicmarker_sacrifice[] __attribute__((section("roguepage"))) __attribute__((aligned(4096))) = "SacrificePhysicalPage=1;";
// You are going to sacrifice a physical page in exchange for the ability to read from anywhere.
// After you do this, this sacrifice area has to be treated as a black hole during the lifetime of the driver.
// You will get the page back intact at driver unload and/or reboot.
// For this reason, it carries an identifier string to know that everything is back at it's place.

static char g_guardingPage_before[] __attribute__((section("roguepage"))) __attribute__((aligned(4096))) = "This marks the guard area before the sacrifice page.";

// Remarks: the compiler seems to place first things last in a section.



#pragma pack(push, 1)
typedef union _VIRT_ADDR
{
  uint64_t value;
  void * pointer;
  struct {
    uint64_t offset        : 12;
    uint64_t pt_index      :  9;
    uint64_t pd_index      :  9;
    uint64_t pdpt_index    :  9;
    uint64_t pml4_index    :  9;
    uint64_t reserved      : 16;
  };
} VIRT_ADDR, *PVIRT_ADDR;



typedef union _CR3
{
  uint64_t value;
  struct {
    uint64_t ignored_1     : 3;
    uint64_t write_through : 1;
    uint64_t cache_disable : 1;
    uint64_t ignored_2     : 7;
    uint64_t pml4_p        :40;
    uint64_t reserved      :12;
  };
} CR3, *PCR3;

typedef union _PML4E
{
  uint64_t value;
  struct {
    uint64_t present        : 1;
    uint64_t rw             : 1;
    uint64_t user           : 1;
    uint64_t write_through  : 1;
    uint64_t cache_disable  : 1;
    uint64_t accessed       : 1;
    uint64_t ignored_1      : 1;
    uint64_t reserved_1     : 1;
    uint64_t ignored_2      : 4;
    uint64_t pdpt_p         :40;
    uint64_t ignored_3      :11;
    uint64_t xd             : 1;
  };
} PML4E, *PPML4E;

typedef union _PDPTE
{
  uint64_t value;
  struct {
    uint64_t present        : 1;
    uint64_t rw             : 1;
    uint64_t user           : 1;
    uint64_t write_through  : 1;
    uint64_t cache_disable  : 1;
    uint64_t accessed       : 1;
    uint64_t dirty          : 1;
    uint64_t large_page      : 1;
    uint64_t ignored_2      : 4;
    uint64_t pd_p           :40;
    uint64_t ignored_3      :11;
    uint64_t xd             : 1;
  };
} PDPTE, *PPDPTE;

typedef union _PDE
{
  uint64_t value;
  struct {
    uint64_t present        : 1;
    uint64_t rw             : 1;
    uint64_t user           : 1;
    uint64_t write_through  : 1;
    uint64_t cache_disable  : 1;
    uint64_t accessed       : 1;
    uint64_t dirty          : 1;
    uint64_t large_page      : 1;
    uint64_t ignored_2      : 4;
    uint64_t pt_p           :40;
    uint64_t ignored_3      :11;
    uint64_t xd             : 1;
  };
} PDE, *PPDE;

typedef union _PTE
{
  uint64_t value;
  VIRT_ADDR vaddr;
  struct {
    uint64_t present        : 1;
    uint64_t rw             : 1;
    uint64_t user           : 1;
    uint64_t write_through  : 1;
    uint64_t cache_disable  : 1;
    uint64_t accessed       : 1;
    uint64_t dirty          : 1;
    uint64_t large_page     : 1;    // PAT/PS
    uint64_t global         : 1;
    uint64_t ignored_1      : 3;
    uint64_t page_frame     :40;
    uint64_t ignored_3      :11;
    uint64_t xd             : 1;
  };
} PTE, *PPTE;
#pragma pack(pop)

// Operating system independent error checking.
typedef enum _PTE_STATUS
{
  PTE_SUCCESS = 0,
  PTE_ERROR,
  PTE_ERROR_HUGE_PAGE,
  PTE_ERROR_RO_PTE
} PTE_STATUS;


typedef struct _PTE_METHOD_DATA
{
    _Bool pte_method_is_ready_to_use;
    VIRT_ADDR page_aligned_rogue_ptr;
    volatile PPTE rogue_pte;
    uint64_t original_addr;
} PTE_METHOD_DATA, *PPTE_METHOD_DATA;


#endif
