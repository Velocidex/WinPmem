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

#ifndef _PTEMAP_C_
#define _PTEMAP_C_

#include <asm/io.h>

#include "pte_mmap.h"
#include "linpmem.h"


/* __attribute__ ((noinline)) */ PTE_STATUS pte_remap_rogue_page(PPTE_METHOD_DATA pPtedata, uint64_t Phys_addr);

/* __attribute__ ((noinline)) */ void print_pte_contents(volatile PPTE ppte);

/* __attribute__ ((noinline)) */ PTE_STATUS virt_find_pte(VIRT_ADDR vaddr, volatile PPTE * pPTE, uint64_t foreign_CR3);

/* __attribute__ ((noinline)) */ _Bool setupRoguePageAndBackup(PPTE_METHOD_DATA pPtedata);

/* __attribute__ ((noinline)) */ void restoreToOriginal(PPTE_METHOD_DATA pPtedata);


// Flushes the tlb entry for a specific page.
inline static void invlpg(uint64_t addr)
{
  __asm__ __volatile__("invlpg (%0);"
                       :
                       : "r"(addr)
                       : );
}

/* Get the contents of the CR3 register. */
inline static CR3 readcr3(void)
{
    CR3 cr3;

    // __asm__ __volatile__("mov %%cr3, %0;": "=r"(cr3.value));

    cr3.value = read_cr3_pa();

    #ifdef LEAK_CR3_INFO
    printk("Kernel CR3 (read_cr3_pa): %llx\n", cr3.value);
    #endif

  return cr3;
}

// Edit the page tables to relink a virtual address to a specific physical page.
//
// Argument 1: a PTE data struct, filled with information about the rogue page to be used.
// Argument 2: the physical address to re-map to.
//
// Returns:
//  PTE_SUCCESS or PTE_ERROR
//
PTE_STATUS pte_remap_rogue_page(PPTE_METHOD_DATA pPtedata, uint64_t Phys_addr)
{
    if (!(Phys_addr && pPtedata)) return PTE_ERROR;

    if (!pPtedata->page_aligned_rogue_ptr.pointer) return PTE_ERROR;

    // Can only remap pages, addresses must be page aligned. No offset allowed.
    if (((!Phys_addr) & PAGE_MASK) || pPtedata->page_aligned_rogue_ptr.offset)
    {
        printk("Failed to map %llx, "
                    "only page aligned remapping is supported!\n",
                    Phys_addr);

        return PTE_ERROR;
    }

    #ifdef PRINT_PTE_REMAP_ACTIONS
    // Careful, this can be preeetty noisy if reading the whole RAM.
    printk("Remapping va %llx to %llx\n",
            (long long unsigned int) pPtedata->page_aligned_rogue_ptr.pointer,
            Phys_addr);
    #endif

    // Change the pte to point to the new offset.
    pPtedata->rogue_pte->page_frame = PAGE_TO_PFN(Phys_addr);

    // Flush the old pte from the tlbs in the system.
    invlpg((uint64_t) pPtedata->page_aligned_rogue_ptr.pointer);

    return PTE_SUCCESS;
}

// Parse a 64 bit page table entry and print it.
void print_pte_contents(volatile PPTE ppte)
{
  printk("Page information: %#016llx\n"
              "\tpresent:      %llx\n"
              "\trw:           %llx\n"
              "\tuser:         %llx\n"
              "\twrite_through:%llx\n"
              "\tcache_disable:%llx\n"
              "\taccessed:     %llx\n"
              "\tdirty:        %llx\n"
              "\tpat/ps:       %llx\n"
              "\tglobal:       %llx\n"
              "\txd:           %llx\n"
              "\tpfn: %010llx",
              (long long unsigned int)ppte,
              (long long unsigned int)ppte->present,
              (long long unsigned int)ppte->rw,
              (long long unsigned int)ppte->user,
              (long long unsigned int)ppte->write_through,
              (long long unsigned int)ppte->cache_disable,
              (long long unsigned int)ppte->accessed,
              (long long unsigned int)ppte->dirty,
              (long long unsigned int)ppte->large_page,
              (long long unsigned int)ppte->global,
              (long long unsigned int)ppte->xd,
              (long long unsigned int)ppte->page_frame);
}


// Traverses the page tables to find the pte for a given virtual address.
//
// Args:
//  _In_ VIRT_ADDR vaddr: The virtual address to resolve the pte for.
//  _Out_  PPTE * pPTE: A pointer to receive the PTE virtual address.
//                      ... if found.
//  _In_Optional   uint64_t foreign_CR3. Another CR3 (not yours) can be used instead. Hopefully you know that it's valid!
//
// Returns:
//  PTE_SUCCESS or PTE_ERROR
//
// Remarks: Supports only virtual addresses that are *not* using huge pages. It will return PTE_ERROR upon finding use of a huge page.
//          Large pages are supported.
//
//
PTE_STATUS virt_find_pte(VIRT_ADDR vaddr, volatile PPTE * pPTE, uint64_t foreign_CR3)
{
    CR3 cr3;
    PPML4E pml4;
    PPML4E pml4e;
    PPDPTE pdpt;
    PPDPTE pdpte;
    PPDE pd;
    PPDE pde;
    PPTE pt;
    PPTE final_pPTE = 0;
    PTE_STATUS status = PTE_ERROR;
    uint64_t physAddr;

    if (pPTE) *pPTE = 0; // Initialize _Out_ variable with zero. This guarantees there is no arbitrary value on it if we later take the error path.

    if (!(vaddr.pointer && pPTE)) goto error;

    #ifdef DPRINT
    printk("Resolving PTE for Address: %llx.\nPrinting ambiguous names: WinDbg terminus(first)/normal terminus(second).\n", vaddr.value);
    #endif

    // Get CR3 to get to the PML4
    if (foreign_CR3 == 0)
    {
        cr3 = readcr3();
    }
    else if ((foreign_CR3 > 0x10000) && (foreign_CR3 < SANE_KERNEL_VA )) // this is just a very primitive numeric sanity check.
    {
        cr3.value = foreign_CR3; // take over the special CR3.
    }
    else // A visibly wrong and invalid foreign cr3 was specified.
    {
        printk("Error: a custom CR3 was specified for vtop, but it is clearly wrong and invalid. Caller: please check your code.\n");
        goto error;
    }

    #ifdef DPRINT
    #ifdef LEAK_CR3_DEBUG_INFO
    printk("CR3 is %llx.\n", cr3.value);
    printk("CR3.pml4_p: %llx.\n", cr3.pml4_p);
    #endif
    #endif

    // I don't know how this could fail, but...
    if (!cr3.value) goto error;

    // Resolve the PML4
    physAddr = PFN_TO_PAGE(cr3.pml4_p);
    pml4 = phys_to_virt(physAddr);

    #ifdef DPRINT
    printk("kernel PX/PML4 base is at %llx physical, and %llx virtual.\n", (long long unsigned int) (PFN_TO_PAGE(cr3.pml4_p)), (long long unsigned int) pml4);
    #endif

    if (!pml4) goto error;

    // Resolve the PDPT
    pml4e = (pml4 + vaddr.pml4_index);

    if (!pml4e->present)
    {
        printk("Error, address %llx has no valid mapping in PML4:\n", vaddr.value);
        print_pte_contents((PPTE) pml4e);
        goto error;
    }

    #ifdef DPRINT
    printk("PXE/PML4[%llx] (at %llx): %llx\n",
            (long long unsigned int) vaddr.pml4_index,
            (long long unsigned int) pml4e,
            (long long unsigned int) pml4e->value);
    #endif

    physAddr = PFN_TO_PAGE(pml4e->pdpt_p);
    pdpt = phys_to_virt(physAddr);

    #ifdef DPRINT
    printk("Points to PP/PDPT base: %llx.\n",
            (long long unsigned int) pdpt);
    #endif

    if (!pdpt) goto error;

    // Resolve the PDT
    pdpte = (pdpt + vaddr.pdpt_index);

    if (!pdpte->present)
    {
        printk("Error, address %llx has no valid mapping in PDPT:\n", vaddr.value);
        print_pte_contents((PPTE) pdpte);
        goto error;
    }

    if (pdpte->large_page)
    {
        printk("Error, address %llx belongs to a 1GB huge page:\n", vaddr.value);
        print_pte_contents((PPTE) pdpte);
        goto error;
    }

    #ifdef DPRINT
    printk("PPE/PDPT[%llx] (at %llx): %llx.\n",
            (long long unsigned int) vaddr.pdpt_index,
            (long long unsigned int) pdpte, pdpte->value);
    #endif

    physAddr = PFN_TO_PAGE(pdpte->pd_p);
    pd = phys_to_virt(physAddr);

    #ifdef DPRINT
    printk("Points to PD base: %llx.\n", (long long unsigned int) pd);
    #endif

    if (!pd) goto error;

    // Resolve the PT
    pde = (pd + vaddr.pd_index);

    if (!pde->present)
    {
        printk("Error, address %llx has no valid mapping in PD:\n", vaddr.value);
        print_pte_contents((PPTE) pde);
        goto error;
    }

    if (pde->large_page)
    {
        final_pPTE = (PPTE) pde; // this is basically like a PTE, just like one tier level above. Though not 100%.
        *pPTE = final_pPTE;

        #ifdef DPRINT
        printk("Final 'PTE' --large page PDE-- (at %llx) : %llx.\n",
                (long long unsigned int) final_pPTE,
                (long long unsigned int) final_pPTE->value);
        #endif

        return PTE_SUCCESS;
    }

    #ifdef DPRINT
    printk("PDE/PD[%llx] (at %llx): %llx.\n",
            (long long unsigned int) vaddr.pd_index,
            (long long unsigned int) pde,
            (long long unsigned int) pde->value);
    #endif

    physAddr = PFN_TO_PAGE(pde->pt_p);
    pt = phys_to_virt(physAddr);

    #ifdef DPRINT
    printk("Points to PT base: %llx.\n", (long long unsigned int) pt);
    #endif

    if (!pt) goto error;

    // Get the PTE and Page Frame
    final_pPTE = (pt + vaddr.pt_index);

    if (!final_pPTE) goto error;

    if (!final_pPTE->present)
    {
        printk("Error, address %llx has no valid mapping in PT:\n", vaddr.value);
        print_pte_contents( final_pPTE );
        goto error;
    }

    #ifdef DPRINT
    printk("final PTE [%llx] (at %llx): %llx.\n",
            (long long unsigned int) vaddr.pt_index,
            (long long unsigned int) final_pPTE,
            (long long unsigned int) final_pPTE->value);
    #endif

    *pPTE = final_pPTE;

    // Everything went well, set PTE_SUCCESS
    status = PTE_SUCCESS;

error:
    return status;

}


_Bool setupRoguePageAndBackup(PPTE_METHOD_DATA pPtedata)
{
    PTE_STATUS pte_status = PTE_SUCCESS;

    pPtedata->page_aligned_rogue_ptr.pointer = g_magicmarker_sacrifice;
    pPtedata->pte_method_is_ready_to_use = FALSE;

    if (pPtedata->page_aligned_rogue_ptr.offset)
    {
        printk("Warning: Setup of PTE method failed (rogue map is not pagesize aligned, this is a programming error!)\n");
        pPtedata->pte_method_is_ready_to_use = FALSE;
        return FALSE;
    }

    // We only need one PTE for the rogue page, and just remap the PFN.
    // A part of the driver's body is sacrificed for this, this part is in a special section.
    // (However, during rest of the life time, this part of the driver must be considered "missing", basically to be treated as a black hole.)
    pte_status = virt_find_pte(pPtedata->page_aligned_rogue_ptr, &pPtedata->rogue_pte, 0);

    if (pte_status != PTE_SUCCESS)
    {
        printk("Warning: Setup of PTE method failed (virt_find_pte failed). This method will not be available!\n");
        pPtedata->pte_method_is_ready_to_use = FALSE;
        return FALSE;
    }

    // Backup original rogue page (physical address).
    pPtedata->original_addr = PFN_TO_PAGE(pPtedata->rogue_pte->page_frame);

    if (!pPtedata->original_addr) // not going to fail until there is some voodoo VSM magic going on. But there are a few anomalous systems.
    {
        printk("Warning: Setup of PTE method failed (no rogue page pfn??). This method will not be available!\n");
        pPtedata->pte_method_is_ready_to_use = FALSE;
        return FALSE;
    }

    pPtedata->pte_method_is_ready_to_use = TRUE;

    return TRUE;
}

void restoreToOriginal(PPTE_METHOD_DATA pPtedata)
{
    PTE_STATUS pte_status = PTE_SUCCESS;

    // if pte method IS ALREADY false, then
    if (pPtedata->pte_method_is_ready_to_use == FALSE)
    {
        return;
    }
    // ... This might for example happen in DriverEntry in the error path.

    // If there is null stored don't even try to restore. null is wrong.
    if (!pPtedata->original_addr)
    {
        printk("Restoring the sacrificed section failed horribly. The backup value was null! Please reboot soon.\n");
        return;
    }

    pte_status = pte_remap_rogue_page(pPtedata, pPtedata->original_addr);

    if (pte_status != PTE_SUCCESS)
    {
        printk("Error: PTE remapping error in restore function.\n");
        goto errorprint;
    }
    else
    {
        if (g_magicmarker_sacrifice[0] == 'S')
        {
            printk("Sacrifice section successfully restored: %s.\n", g_magicmarker_sacrifice);
            return;
        }
        else goto errorprint;
    }

errorprint:

    printk("Error: uh-oh, restoring failed. Consider rebooting. (Right now.)\n");

    return;
}

#endif
