// Subclass of the pte_mmap module. Contains implementations for the Windows
// specific part of the module.
//
// Copyright 2018 Velocidex Innovations <mike@velocidex.com>
// Copyright 2014 - 2017 Google Inc.
// Copyright 2012 Google Inc. All Rights Reserved.
//
// Author: Johannes Stüttgen (johannes.stuettgen@gmail.com)
// Author: Michael Cohen (scudette@gmail.com)
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

#include "pte_mmap_windows.h"
static char rogue_page[PAGE_SIZE * 3] = "";
static char *page_aligned_space = NULL;
static MDL *rogue_mdl = NULL;

// Get a free, non-paged page of memory. On windows we can not
// allocate from pool because pool storage is controlled by large page
// PTEs. So we just use a static page in the driver executable.
static void *pte_get_rogue_page(void) {
	if (page_aligned_space == NULL) {
		// We would ideally like to just allocate memory from non paged
		// pool but on Windows 7, non paged pool is allocated from large
		// pages and we wont be able to use the page in PTE remapping. So
		// this code creates an MDL from a static buffer within the
		// driver's data section. We must ensure that this buffer is not
		// paged out though so we must call MmGetSystemAddressForMdlSafe
		// to ensure the MDL is locked into memory.

		// page_aligned_space is the first page aligned offset within the
		// buffer.
		page_aligned_space = &rogue_page[0];
		page_aligned_space += PAGE_SIZE - ((__int64)&rogue_page[0]) % PAGE_SIZE;

		// MDL is for a single page.
		rogue_mdl = IoAllocateMdl(page_aligned_space, PAGE_SIZE,
			FALSE, FALSE, NULL);
		if (!rogue_mdl) {
			return NULL;
		}

		try {
			// This locks the physical page into memory.
			MmProbeAndLockPages(rogue_mdl, KernelMode, IoReadAccess);
			page_aligned_space = MmMapLockedPagesSpecifyCache(
				rogue_mdl, KernelMode, MmCached, NULL, 0, NormalPagePriority);
			return page_aligned_space;

		} except(EXCEPTION_EXECUTE_HANDLER) {
			NTSTATUS ntStatus = GetExceptionCode();

			WinDbgPrint("Exception while locking rogue_mdl %#08X\n", ntStatus);
			IoFreeMdl(rogue_mdl);
			rogue_mdl = NULL;
			return NULL;
		}
	}
	return page_aligned_space;
}

// Frees a single page allocated with vmalloc(). Rogue page is static
// we do not free it.
static void pte_free_rogue_page(void *page) {
  if (rogue_mdl) {
    MmUnlockPages(rogue_mdl);
    IoFreeMdl(rogue_mdl);
    rogue_mdl = NULL;
  };
  return;
}

// Makes use of the fact that the page tables are always mapped in the direct
// Kernel memory map.
static void *pte_phys_to_virt(PHYS_ADDR address) {
  PHYSICAL_ADDRESS phys_address;

  phys_address.QuadPart = address;
  //return phys_to_virt(address);
  // TODO(scudette): Use PFNDB here.
  return Pmem_KernelExports.MmGetVirtualForPhysical(phys_address);
}

// Flushes the tlb entry for a specific page.
static void pte_flush_tlb_page(void *addr) {
  // Use compiler instrinsic.
  __invlpg(addr);
}

// Flush a specific page from all cpus by sending an ipi.
static void pte_flush_all_tlbs_page(void *page) {
  // TODO: Make this work.
  pte_flush_tlb_page(page);
}

/* Get the contents of the CR3 register. */
static PTE_CR3 pte_get_cr3(void) {
  PTE_CR3 result;
  result.value = __readcr3();
  return result;
}


// Will reset the page table entry for the rogue page and free the object.
void pte_mmap_windows_delete(PTE_MMAP_OBJ *self) {
  pte_mmap_cleanup(self);
  ExFreePool(self);
}

// Initializer that fills an operating system specific vtable,
// allocates memory, etc.
PTE_MMAP_OBJ *pte_mmap_windows_new(void) {
  PTE_MMAP_OBJ *self = NULL;

  // Allocate the object
  self = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(PTE_MMAP_OBJ),
			       PMEM_POOL_TAG);

  if (!self) return NULL;

  // Let the superconstructor set up the internal stuff
  pte_mmap_init(self);

  // Fill the virtual function into the vtable
  self->get_rogue_page_ = pte_get_rogue_page;

  self->free_rogue_page_ = pte_free_rogue_page;
  self->phys_to_virt_ = pte_phys_to_virt;
  self->flush_tlbs_page_ = pte_flush_all_tlbs_page;
  self->get_cr3_ = pte_get_cr3;


  // Initialize attributes that rely on memory allocation
  self->rogue_page.pointer = self->get_rogue_page_();
  if (self->rogue_page.pointer == NULL) {
	  goto error;
  }
  WinDbgPrintDebug("Looking up PTE for rogue page: %p",
                   self->rogue_page);
  if (self->find_pte_(self, self->rogue_page.pointer, &self->rogue_pte)) {
    WinDbgPrint("Failed to find the PTE for the rogue page, "
                "might be inside huge page, aborting...");
    goto error;
  }
  WinDbgPrintDebug("Found rogue pte at %p", self->rogue_pte);

  // Back up the address this pte points to for cleaning up later.
  self->original_addr = PFN_TO_PAGE(self->rogue_pte->page_frame);

  return self;

 error:
  pte_mmap_windows_delete(self);
  return NULL;
}
