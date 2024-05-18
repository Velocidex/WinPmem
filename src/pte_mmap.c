// This module contains helper functions to directly edit the page tables of a
// process, enabling it to map physical memory independent of the operating
// system.
//
// Warning: This code directly writes to the kernel page tables and executes
// priviledged instructions as invlpg. It will only run in ring 0.
//
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

#include <sal.h>
#include "pte_mmap.h"
#include "winpmem.h"

#if defined(_WIN64)

__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
PTE_STATUS pte_remap_rogue_page(_Inout_ PPTE_METHOD_DATA pPtedata, _In_ PHYS_ADDR Phys_addr);

__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
void print_pte_contents(_In_ PTE * pte);

__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
PTE_STATUS virt_find_pte(_In_ VIRT_ADDR vaddr, _Out_  volatile PPTE * pPTE);

__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
BOOLEAN setupBackupForOriginalRoguePage(_Inout_ PPTE_METHOD_DATA pPtedata);

__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
VOID restoreOriginalRoguePage(_Inout_ PPTE_METHOD_DATA pPtedata);


#ifdef ALLOC_PRAGMA
#pragma alloc_text( NONPAGED , pte_remap_rogue_page )
#pragma alloc_text( NONPAGED , print_pte_contents )
#pragma alloc_text( NONPAGED , virt_find_pte )
#pragma alloc_text( NONPAGED , setupBackupForOriginalRoguePage )
#pragma alloc_text( NONPAGED , restoreOriginalRoguePage)
#endif


// Edit the page tables to relink a virtual address to a specific physical page.
//
// Argument 1: the physical address to map to.
// The intrinsic rogue page section (nonpaged, 4096-aligned, RW, cached) will be used for this.
//
// Returns:
//  PTE_SUCCESS or PTE_ERROR
//

__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
PTE_STATUS pte_remap_rogue_page(_Inout_ PPTE_METHOD_DATA pPtedata, _In_ PHYS_ADDR Phys_addr)
{
    if (!(Phys_addr && pPtedata)) return PTE_ERROR;

    if (!pPtedata->page_aligned_rogue_ptr.pointer) return PTE_ERROR;

    // Can only remap pages, addresses must be page aligned. No offset allowed.
    if (((!Phys_addr) & PAGE_MASK) || pPtedata->page_aligned_rogue_ptr.offset)
    {
        WinDbgPrint("Failed to map %llx, "
                    "only page aligned remapping is supported!\n",
                    Phys_addr);

        return PTE_ERROR;
    }

    #if PRINT_PTE_REMAP_ACTIONS == 1
    // Careful, this can be preeetty noisy if reading the whole RAM.
    WinDbgPrint("Remapping va %p to %llx\n", pPtedata->page_aligned_rogue_ptr.pointer, Phys_addr);
    #endif

    // __debugbreak();

    // Change the pte to point to the new offset.
    pPtedata->rogue_pte->page_frame = PAGE_TO_PFN(Phys_addr);

    // Flush the old pte from the tlbs in the system.
    __invlpg(pPtedata->page_aligned_rogue_ptr.pointer);

    return PTE_SUCCESS;
}

// Parse a 64 bit page table entry and print it.
__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
void print_pte_contents(_In_ PTE * pte)
{
  WinDbgPrint("Virtual Address:%#016llx\n"
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
              (unsigned __int64)pte,
              (unsigned __int64)pte->present,
              (unsigned __int64)pte->rw,
              (unsigned __int64)pte->user,
              (unsigned __int64)pte->write_through,
              (unsigned __int64)pte->cache_disable,
              (unsigned __int64)pte->accessed,
              (unsigned __int64)pte->dirty,
              (unsigned __int64)pte->large_page,
              (unsigned __int64)pte->global,
              (unsigned __int64)pte->xd,
              (unsigned __int64)pte->page_frame);
}

// Traverses the page tables to find the pte for a given virtual address.
//
// Args:
//  _In_ VIRT_ADDR vaddr: The virtual address to resolve the pte for.
//  _Out_  PPTE * pPTE: A pointer to receive the PTE pointer. (The PTE pointer belongs to the memory manager.)
//                      ... if found.
//
// Returns:
//  PTE_SUCCESS or PTE_ERROR
//
// Remarks: Supports only virtual addresses that are *not* using large pages. It will return PTE_ERROR upon finding use of a large page.
//
//          This routine is *only* used initially to setup the rogue page and backup the original PFN/Physical address of the rogue page.
//          This is because we only ever use one single PTE that will remap constantly the PFN.
//
//          Alternatives: we could 'import' MiGetPteAddress, which does pretty much the same and could be used in place of self-made virt_find_pte, sparing lots of code.
//                        However, this self-made routine is very robust and standalone, and locating MiGetPteAddress needs code, too.
//                        This routine is therefore considered superior to using MiGetPteAddress.
//
__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
PTE_STATUS virt_find_pte(_In_ VIRT_ADDR vaddr, _Out_  volatile PPTE * pPTE)
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
    PHYSICAL_ADDRESS physAddr;

    if (pPTE) *pPTE = 0; // Initialize _Out_ variable with zero. This guarantees there is no arbitrary value on it if we later take the error path.

    if (!(vaddr.pointer && pPTE)) goto error;

    WinDbgPrint("Resolving PTE for Address: %llx.\nPrinting ambiguous names: WinDbg terminus(first)/normal terminus(second).\n", vaddr.value);

    // Get contents of cr3 register to get to the PML4
    cr3.value = __readcr3();

    WinDbgPrint("Kernel CR3 is %llx.\n", cr3.value);
    WinDbgPrint("Kernel CR3.pml4_p: %llx.\n", cr3.pml4_p);

    // I don't know how this could fail, but...
    if (!cr3.value) goto error;

    // Resolve the PML4
    physAddr.QuadPart = PFN_TO_PAGE(cr3.pml4_p);
    pml4 = MmGetVirtualForPhysical(physAddr);
    WinDbgPrint("kernel PX/PML4 base is at %llx physical, and %p virtual.\n", PFN_TO_PAGE(cr3.pml4_p), pml4);

    if (!pml4) goto error;

    // Resolve the PDPT
    pml4e = (pml4 + vaddr.pml4_index);

    if (!pml4e->present)
    {
        WinDbgPrint("Error, address %llx has no valid mapping in PML4:\n", vaddr.value);
        print_pte_contents((PPTE) pml4e);
        goto error;
    }
    WinDbgPrint("PXE/PML4[%llx] (at %p): %llx\n", vaddr.pml4_index, pml4e, pml4e->value);

    physAddr.QuadPart = PFN_TO_PAGE(pml4e->pdpt_p);
    pdpt = MmGetVirtualForPhysical(physAddr);
    WinDbgPrint("Points to PP/PDPT base: %p.\n", pdpt);

    if (!pdpt) goto error;

    // Resolve the PDT
    pdpte = (pdpt + vaddr.pdpt_index);

    if (!pdpte->present)
    {
        WinDbgPrint("Error, address %llx has no valid mapping in PDPT:\n", vaddr.value);
        print_pte_contents((PPTE) pdpte);
        goto error;
    }

    if (pdpte->large_page)
    {
        WinDbgPrint("Error, address %llx belongs to a 1GB huge page:\n", vaddr.value);
        print_pte_contents((PPTE) pdpte);
        goto error;
    }
    WinDbgPrint("PPE/PDPT[%llx] (at %p): %llx.\n", vaddr.pdpt_index, pdpte, pdpte->value);

    physAddr.QuadPart = PFN_TO_PAGE(pdpte->pd_p);
    pd = MmGetVirtualForPhysical(physAddr);
    WinDbgPrint("Points to PD base: %p.\n", pd);

    if (!pd) goto error;

    // Resolve the PT
    pde = (pd + vaddr.pd_index);

    if (!pde->present)
    {
        WinDbgPrint("Error, address %llx has no valid mapping in PD:\n", vaddr.value);
        print_pte_contents((PPTE) pde);
        goto error;
    }

    if (pde->large_page)
    {
        final_pPTE = (PPTE) pde; // this is basically like a PTE, just like one tier level above. Though not 100%.
        *pPTE = final_pPTE;
        WinDbgPrint("Final 'PTE' --large page PDE-- (at %p) : %llx.\n", final_pPTE, final_pPTE->value);

        return PTE_SUCCESS;
    }

    WinDbgPrint("PDE/PD[%llx] (at %p): %llx.\n", vaddr.pd_index, pde, pde->value);

    physAddr.QuadPart = PFN_TO_PAGE(pde->pt_p);
    pt = MmGetVirtualForPhysical(physAddr);
    WinDbgPrint("Points to PT base: %p.\n", pt);

    if (!pt) goto error;

    // Get the PTE and Page Frame
    final_pPTE = (pt + vaddr.pt_index);

    if (!final_pPTE) goto error;

    if (!final_pPTE->present)
    {
        WinDbgPrint("Error, address %llx has no valid mapping in PT:\n", vaddr.value);
        print_pte_contents( final_pPTE );
        goto error;
    }

    WinDbgPrint("final PTE [%llx] (at %p): %llx.\n", vaddr.pt_index, final_pPTE, final_pPTE->value);

    *pPTE = final_pPTE;

    // Everything went well, set PTE_SUCCESS
    status = PTE_SUCCESS;

error:
    return status;

}


__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
BOOLEAN setupBackupForOriginalRoguePage(_Inout_ PPTE_METHOD_DATA pPtedata)
{
    // Backup original rogue PTE.

    pPtedata->page_aligned_rogue_ptr.pointer = ROGUE_PAGE_MAGICMARKER; // a pointer that features bit-accessing.
    pPtedata->loglevel = PTE_ERR;
    pPtedata->pte_method_is_ready_to_use = FALSE;

    if (pPtedata->page_aligned_rogue_ptr.offset)
    {
        DbgPrint("Warning: Setup of PTE method failed (rogue map is not pagesize aligned, this is a programming error!)\n");
        pPtedata->pte_method_is_ready_to_use = FALSE;
        return FALSE;
    }

    // We only need one PTE, and just remap the PFN.
    // It will be take from our rogue page (which will be sacrificed temporarily in this process).
    PTE_STATUS pte_status = virt_find_pte(pPtedata->page_aligned_rogue_ptr, &pPtedata->rogue_pte);

    if (pte_status != PTE_SUCCESS)
    {
        DbgPrint("Warning: Setup of PTE method failed (virt_find_pte failed). This method will not be available!\n");
        pPtedata->pte_method_is_ready_to_use = FALSE;
        return FALSE;
    }

    // Backup original rogue PFN.
    pPtedata->original_addr = PFN_TO_PAGE(pPtedata->rogue_pte->page_frame);

    if (!pPtedata->original_addr) // not going to fail until there is some voodoo VSM magic going on. Better be safe than sorry.
    {
        DbgPrint("Warning: Setup of PTE method failed (no rogue page pfn??). This method will not be available!\n");
        pPtedata->pte_method_is_ready_to_use = FALSE;
        return FALSE;
    }

    pPtedata->pte_method_is_ready_to_use = TRUE;

    return TRUE;
}

__declspec(noinline) _IRQL_requires_max_(APC_LEVEL)
VOID restoreOriginalRoguePage(_Inout_ PPTE_METHOD_DATA pPtedata)
{
    // Restore original rogue PTE.

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
        DbgPrint("Restoring rogue page failed horribly. The backup value was null! Please reboot soon.\n");
        return;
    }

    pte_status = pte_remap_rogue_page(pPtedata, pPtedata->original_addr);

    if (pte_status != PTE_SUCCESS)
    {
        DbgPrint("Error: PTE remapping error in restore function.\n");
        goto errorprint;
    }
    else
    {
        if ('G' == ROGUE_PAGE_MAGICMARKER[0])
        {
            DbgPrint("Rogue page restored: %s\n", ROGUE_PAGE_MAGICMARKER);
            return;
        }
        else goto errorprint;
    }

errorprint:

    DbgPrint("Error: uh-oh, restoring the rogue page failed. The (physical) rogue page is lost. Consider rebooting.\n");

    return;
}

#endif
