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

// linpmem.c contains:
// * the main driver init and unload routines
// * as well as the I/O dispatch routines (open/close and device io)

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>

#include "precompiler.h"
#include "linpmem.h"


// Called when the Device is opened. Dummy stub.
static int OPEN_Dispatch(struct inode *device_file, struct file *instance)
{
    #ifdef DPRINT
    printk("Pmem open.\n");
    #endif

    return 0;
}

// Called when the Device is closed. Dummy stub.
static int CLOSE_Dispatch(struct inode *device_file, struct file *instance)
{
    #ifdef DPRINT
    printk("Pmem close.\n");
    #endif

    return 0;
}


// Returns number of bytes that could be read without crossing page boundary.
// Remarks: this function will not read more than PAGE_SIZE bytes in any case.
//          It might read less than specified, if the address is very
//          much at the end of a page and/or is crossing the page boundary.
//          So be sure to check the return value!
static uint64_t PTEMmapPartialRead( PPTE_METHOD_DATA pPtedata,
                                    uint64_t physAddr,
                                    void * targetbuffer,
                                    uint64_t count,
                                    PHYS_ACCESS_MODE accessMode)
{
    PTE_STATUS pte_status = PTE_SUCCESS;
    uint64_t page_offset = 0;
    uint64_t to_read = 0;
    uint64_t pageAlignedPage = 0;
    uint64_t result = 0;
    unsigned long bytesMissedOnUserCopy = 0;

    if (!(pPtedata && physAddr && targetbuffer && count))
    {
        return 0;
    }

    page_offset = physAddr % PAGE_SIZE;
    to_read = min(PAGE_SIZE - page_offset, count);
    pageAlignedPage = physAddr - page_offset;

    pte_status = pte_remap_rogue_page(pPtedata, pageAlignedPage);

    if (pte_status != PTE_SUCCESS) return result;

    switch (accessMode)
    {
        case PHYS_BYTE_READ:
        {
            // kernelmode "buffer"
            *((uint8_t *) targetbuffer) =  ((uint8_t *) (((uint64_t) pPtedata->page_aligned_rogue_ptr.value) + page_offset))[0];

            break;
        }

        case PHYS_WORD_READ:
        {
            // kernelmode "buffer"
            *((uint16_t *) targetbuffer) =  ((uint16_t *) (((uint64_t) pPtedata->page_aligned_rogue_ptr.value) + page_offset))[0];

            break;
        }

        case PHYS_DWORD_READ:
        {
            // kernelmode "buffer"
            *((uint32_t *) targetbuffer) =  ((uint32_t *) (((uint64_t) pPtedata->page_aligned_rogue_ptr.value) + page_offset))[0];

            break;
        }

        case PHYS_QWORD_READ:
        {
            // kernelmode "buffer"
            *((uint64_t *) targetbuffer) =  ((uint64_t *) (((uint64_t) pPtedata->page_aligned_rogue_ptr.value) + page_offset))[0];

            break;
        }

        case PHYS_BUFFER_READ:
        {
            // this is a usermode buffer.
            bytesMissedOnUserCopy = copy_to_user(   targetbuffer,
                                    (void *) (((uint64_t) pPtedata->page_aligned_rogue_ptr.value) + page_offset),
                                    to_read);

            if (bytesMissedOnUserCopy)
            {
                printk("Pmem driver: Error on PHYS_BUFFER_READ.\n Unfortunately, the usermode buffer wasn't entirely valid!\n Wanted to transfer %llx bytes, missed %llx bytes. Returning zero.\n",
                    to_read,
                    (long long unsigned int) bytesMissedOnUserCopy
                    );

                to_read = 0;
            }

            break;
        }

    }

    result = to_read;

    return result;
}

// Called on Device Io Control.
static long int IOCTL_Dispatch(struct file *file, unsigned ioctl, unsigned long userbuffer)
{
    unsigned long bytesMissed = 0;
    uint64_t readBytes = 0;

    switch (ioctl)
    {

        case IOCTL_LINPMEM_READ_PHYSADDR:
        {
            LINPMEM_DATA_TRANSFER dataTransfer;

            bytesMissed = copy_from_user(&dataTransfer, (PLINPMEM_DATA_TRANSFER) userbuffer, sizeof(LINPMEM_DATA_TRANSFER));

            if (bytesMissed)
            {
                printk("Pmem IOCTL_Dispatch - Error: copying LINPMEM_DATA_TRANSFER from user! Missed %lu bytes. \n", bytesMissed);
                goto IOCTL_Dispatch_end;
            }

            if (!dataTransfer.physAddress)
            {
                printk("Pmem IOCTL_Dispatch - Error: no address provided!\n");
                goto IOCTL_Dispatch_end;
            }

            if (!dataTransfer.accessType)
            {
                printk("Pmem IOCTL_Dispatch - Error: no access type set!\n");
                goto IOCTL_Dispatch_end;
            }

            switch (dataTransfer.accessType)
            {
                case PHYS_BYTE_READ:
                {
                    uint8_t physbyte = 1; // dummy value

                    #ifdef DPRINT
                    printk("Pmem IOCTL_Dispatch - Reading 1 byte from %llx.\n", (long long unsigned int) dataTransfer.physAddress);
                    #endif

                    // Use rogue page for 1 byte read.

                    readBytes = PTEMmapPartialRead( &g_DeviceExtension.pte_data,
                                            dataTransfer.physAddress,
                                            &physbyte,
                                            1,
                                            PHYS_BYTE_READ
                                    );

                    if (readBytes == 1) // bytes read expected
                    {
                        dataTransfer.outValue = physbyte;
                    }
                    else
                    {
                        dataTransfer.outValue = 0; // failed read due to (probably) page boundary.
                    }

                    break;
                }

                case PHYS_WORD_READ:
                {
                    uint16_t physword = 2; // dummy value

                    #ifdef DPRINT
                    printk("Pmem IOCTL_Dispatch - Reading 2 bytes from %llx.\n", (long long unsigned int) dataTransfer.physAddress);
                    #endif

                    // Use rogue page for 2 byte read.

                    readBytes = PTEMmapPartialRead( &g_DeviceExtension.pte_data,
                                            dataTransfer.physAddress,
                                            &physword,
                                            2,
                                            PHYS_WORD_READ
                                    );

                    if (readBytes == 2) // bytes read expected
                    {
                        dataTransfer.outValue = physword;
                    }
                    else
                    {
                        dataTransfer.outValue = 0; // failed read due to (probably) page boundary.
                    }

                    break;
                }

                case PHYS_DWORD_READ:
                {
                    uint32_t physdword = 4;  // dummy value for testing

                    #ifdef DPRINT
                    printk("Pmem IOCTL_Dispatch - Reading 4 bytes from %llx.\n", (long long unsigned int) dataTransfer.physAddress);
                    #endif

                    // Use rogue page for 4 byte read.

                    readBytes = PTEMmapPartialRead( &g_DeviceExtension.pte_data,
                                            dataTransfer.physAddress,
                                            &physdword,
                                            4,
                                            PHYS_DWORD_READ
                                    );

                    if (readBytes == 4) // bytes read expected
                    {
                        dataTransfer.outValue = physdword;
                    }
                    else
                    {
                        dataTransfer.outValue = 0; // failed read due to (probably) page boundary.
                    }

                    break;
                }

                case PHYS_QWORD_READ:
                {
                    uint64_t physqword = 8; // dummy value for testing

                    #ifdef DPRINT
                    printk("Pmem IOCTL_Dispatch - Reading 8 bytes from %llx.\n", (long long unsigned int) dataTransfer.physAddress);
                    #endif

                    // Use rogue page for 8 byte read.

                    readBytes = PTEMmapPartialRead( &g_DeviceExtension.pte_data,
                                            dataTransfer.physAddress,
                                            &physqword,
                                            8,
                                            PHYS_QWORD_READ
                                    );

                    if (readBytes == 8) // bytes read expected
                    {
                        dataTransfer.outValue = physqword;
                    }
                    else
                    {
                        dataTransfer.outValue = 0; // failed read due to (probably) page boundary.
                    }

                    break;
                }

                case PHYS_BUFFER_READ:
                {
                    // Sanity checks. Usermode programs are considered nasty and faulty.

                    if (!dataTransfer.readbufferSize)
                    {
                        printk("Pmem IOCTL BUFFER_READ - Error: 0 byte read size specified.\n");
                        goto IOCTL_Dispatch_end;
                    }

                    if (dataTransfer.readbufferSize > PAGE_SIZE )
                    {
                        printk("Pmem IOCTL BUFFER_READ - Error: provided usermode buffer too large.\nDriver maximum per read capability is PAGE_SIZE (4096 bytes decimal).\n");
                        goto IOCTL_Dispatch_end;
                    }

                    if (!dataTransfer.readbuffer)
                    {
                        printk("Pmem IOCTL BUFFER_READ - Error: provided usermode buffer is null. Not wasting time on that.\n");
                        goto IOCTL_Dispatch_end;
                    }

                    #ifdef DPRINT
                    printk("Pmem IOCTL BUFFER_READ - trying to read %llx bytes from %llx, as wanted.\n",
                            dataTransfer.readbufferSize,
                            (long long unsigned int) dataTransfer.physAddress);
                    #endif

                    // This is a read directly from our rogue page.

                    // Remarks: for PHYS_BUFFER_READ, the partial read function will use copy_to_user.
                    readBytes = PTEMmapPartialRead( &g_DeviceExtension.pte_data, // _in_ rogue page data
                                            dataTransfer.physAddress, // _in_ physical address
                                            dataTransfer.readbuffer,  // <= _out_ usermode buffer.
                                            dataTransfer.readbufferSize, // <= out_ usermode buffer size, as told by the usermode program.
                                            PHYS_BUFFER_READ
                                    );
                    // I'm a little bit nervous here about if the Linux kernel copy_to_user is robust.
                    // But then again, either the Linux kernel authors did it right or not. At some point, I have to trust them that they do it right.

                    if (readBytes != dataTransfer.readbufferSize) // amount of read bytes expected. Ideally. If not crossing a page boundary.
                    {
                         dataTransfer.readbufferSize = 0;
                         // We return 0 if we were unable to read all bytes. (Most likely because of hitting the page boundary.)
                    }

                    break;
                }

            } // end of switch (dataTransfer.accessType)


            // copy read bytes back to user.

            bytesMissed = copy_to_user((PLINPMEM_DATA_TRANSFER) userbuffer, &dataTransfer, sizeof(LINPMEM_DATA_TRANSFER));

            if (bytesMissed)
            {
                 printk("Pmem driver - Error copying LINPMEM_DATA_TRANSFER back to user! Missed %lu bytes. \n", bytesMissed);
                 goto IOCTL_Dispatch_end;
            }

            break;

        } // end of case IOCTL_LINPMEM_READ_PHYSADDR

        case IOCTL_LINPMEM_VTOP_TRANSLATION_SERVICE:
        {
            LINPMEM_VTOP_INFO VTOP_Info;
            VIRT_ADDR In_VA = {0};
            uint64_t page_offset = 0;
            volatile PPTE pPTE;
            PTE_STATUS pte_status = PTE_SUCCESS;

            // Copy all the struct *now*. Avoid failing very late because a user provided a too small incorrect buffer.
            // This is because our operation is somewhat expensive.
            // Totally let's check first if the user made everything according to protocol.
            // So that's why we copy the complete struct right at the beginning.
            // (It could still fail very late due to mischieveous usermode programs, but less likely.)

            bytesMissed = copy_from_user(&VTOP_Info, (PLINPMEM_VTOP_INFO) userbuffer, sizeof(LINPMEM_VTOP_INFO));

            if (bytesMissed)
            {
                 printk("Pmem driver - Error copying LINPMEM_VTOP_INFO from user! Missed %lu bytes. \n", bytesMissed);
                 goto IOCTL_Dispatch_end;
            }

            #ifdef DPRINT
                printk("Pmem driver - IOCTL_LINPMEM_VTOP_TRANSLATION_SERVICE.\n");
                printk("VTOP translation wanted for: VA %llx, associated CR3: %llx.\n", VTOP_Info.virtAddress, VTOP_Info.associatedCR3);
            #endif

            if (!VTOP_Info.virtAddress)
            {
                printk("Pmem driver - Error: no virtual address specified for vtop.\n");
                goto IOCTL_Dispatch_end;
            }

            In_VA.value = VTOP_Info.virtAddress;
            page_offset = In_VA.offset;
            In_VA.value -= page_offset;

            if (In_VA.offset)
            {
                printk("Pmem driver - Error: unexpected page offset left over, this is programming error.\n");
                goto IOCTL_Dispatch_end;
            }

            pte_status = virt_find_pte(In_VA, &pPTE);

            if (pte_status != PTE_SUCCESS)
            {
                // Remember todo: reverse search currently returns error on large pages (because not implemented).
                printk("No translation possible: no present page for %llx. Sorry.\n", In_VA.value);
                VTOP_Info.physAddress = 0;
                VTOP_Info.PTEAddress = NULL;
            }
            else
            {
                if (pPTE->present) // But virt_find_pte checked that already.
                {
                    if (!pPTE->large_page)
                    {
                        VTOP_Info.physAddress = ( PFN_TO_PAGE(pPTE->page_frame) ) + page_offset;  // normal calculation. 4096 page size.
                        VTOP_Info.PTEAddress = (void *) pPTE;
                    }
                    else
                    {
                        VTOP_Info.physAddress = ( PFN_TO_PAGE(( pPTE->page_frame +  In_VA.pt_index)) ) + page_offset; // Large page calculation.
                        VTOP_Info.PTEAddress = (void *) pPTE;
                    }
                }
                else
                {
                    printk("Valid bit not set in PTE.\n");
                    VTOP_Info.physAddress = 0;
                    VTOP_Info.PTEAddress = NULL;
                }
            }

            #ifdef DPRINT
            printk("Pmem driver - vtop translation success. Physical address: %llx. PTE address: %llx\n",
                    (long long unsigned int) VTOP_Info.physAddress,
                    (long long unsigned int) VTOP_Info.PTEAddress);

            print_pte_contents(pPTE);
            #endif

            bytesMissed = copy_to_user((PLINPMEM_VTOP_INFO) userbuffer, &VTOP_Info, sizeof(LINPMEM_VTOP_INFO));

            if (bytesMissed)
            {
                printk("Pmem driver - Error copying LINPMEM_VTOP_INFO back to user! Missed %lu bytes. \n", bytesMissed);
                goto IOCTL_Dispatch_end;
            }

            break;

        } // end of IOCTL_LINPMEM_VTOP_TRANSLATION_SERVICE

        case IOCTL_LINPMEM_QUERY_CR3:
        {
            LINPMEM_CR3_INFO cr3Info;
            CR3 cr3;

            // Copy all the struct *now*. Avoid failing very late because a user provided a too small incorrect buffer.
            // This is because our operation is somewhat expensive.
            // Totally let's check first if the user made everything according to protocol.
            // So that's why we copy the complete struct right at the beginning.
            // (It could still fail very late due to mischieveous usermode programs, but less likely.)

            bytesMissed = copy_from_user(&cr3Info, (PLINPMEM_CR3_INFO) userbuffer, sizeof(LINPMEM_CR3_INFO));

            if (bytesMissed)
            {
                 printk("Pmem driver - Error copying LINPMEM_CR3_INFO from user! Missed %lu bytes. \n", bytesMissed);
                 goto IOCTL_Dispatch_end;
            }

            #ifdef DPRINT
                printk("Pmem driver - IOCTL_LINPMEM_QUERY_CR3.\n");
            #endif

            // currently, there is no choice. You simply get the CR3 associated with this process context.
            // Target process you specify is ignored because it's not implemented.
            cr3 = readcr3();

            // Sanity check. You know, these VMs nowadays... and TDX, and SME bla... actually just checking against zero might not be enough.
            // CR3 can return as zero in very rare and anomalous circumstances.
            if (!cr3.value)
            {
                printk("Pmem driver: warning: user requested cr3 read is zero! This should NOT happen.\nYou can't use Linpmem for physical reading on this OS.\n");
                // the only action is to warn the user about using Linpmem on his anomalous OS.
            }

            cr3Info.resultCR3 = cr3.value; // write it into return package.

            bytesMissed = copy_to_user((PLINPMEM_CR3_INFO) userbuffer, &cr3Info, sizeof(LINPMEM_CR3_INFO));

            if (bytesMissed)
            {
                printk("Pmem driver - Error copying LINPMEM_CR3_INFO to user! Missed %lu bytes. \n", bytesMissed);
                goto IOCTL_Dispatch_end;
            }

            break;

        } // end of case IOCTL_LINPMEM_QUERY_CR3

    } // end of switch ioctl

IOCTL_Dispatch_end:

    return bytesMissed;
}

const static struct file_operations DriverDispatch = {
    .owner = THIS_MODULE,
    .open = OPEN_Dispatch,
    .release = CLOSE_Dispatch,
    .unlocked_ioctl = IOCTL_Dispatch
};


/**
 * @brief This function is called, when the module is loaded into the kernel
 */
static int __init ModuleInit(void)
{
    int status = 0;
    int major = 0;

    _Bool setupDone = FALSE;

    printk("Pmem driver - init.\n");

    memset(&g_DeviceExtension, 0, sizeof(DEVICE_EXTENSION)); // It should already be zeroed out, but, hey.

    major = register_chrdev(LINPMEM_IOCTL_MAJOR, "linpmem_ioctl_interface", &DriverDispatch);

    if (major)
    {
        // Want my major number.
        printk("Pmem driver - Error/exiting: could not register standard Linpmem Major: %d, got: %d.\n", LINPMEM_IOCTL_MAJOR, major);
        status = -1;
        goto InitEnd;
    }

    #ifdef DPRINT

        // no need to be careful when printing this. No page sacrifice has happened (yet).

        printk("Identifier string on sacrifice page: %s, %llx\n",
                g_magicmarker_sacrifice,
                (long long unsigned int) &g_magicmarker_sacrifice);

        printk("Guardian pages on both sides: \n Before: %s, %llx.\n After: %s, %llx.\n",
                g_guardingPage_before,
                (long long unsigned int) &g_guardingPage_before,
                g_guardingPage_after,
                (long long unsigned int) &g_guardingPage_after
                );

    #endif

    setupDone = setupRoguePageAndBackup(&g_DeviceExtension.pte_data);

    // check if everything went smoothly.

    if (setupDone == FALSE)
    {
        printk("Pmem driver: error: setting up the rogue page failed terribly.\nIt might be advisable to reboot right now for safety.\n");
        status = -1;
        goto InitEnd;
    }

    printk("Pmem driver - configured successfully. You can now use it.\n");

InitEnd:

    return 0;
}


static void __exit ModuleExit(void)
{

    // Undo the sacrifice.
    if (g_DeviceExtension.pte_data.pte_method_is_ready_to_use)
    {
        restoreToOriginal(&g_DeviceExtension.pte_data);
    }

    // Everything should be as it should be, except if we lost control.
    // If there is a programming error, or a tiny little thing which we did not guard against, this might lead to a full loss of control over the rogue page.
    // We won't know until we go looking.
    // So, peek first character carefully. Expected: 'S'.
    if (g_magicmarker_sacrifice[0] != 'S')
    {
        printk("Critical failure! The rogue page is out of control.\nRecommended: immediately reboot. Reboot. now.\n");
        unregister_chrdev(LINPMEM_IOCTL_MAJOR, "linpmem_ioctl_interface");
        return;
    }

    // Turns out fine.

    #ifdef DPRINT
    printk("Identifier string on sacrifice page: %s, %llx\n",
            g_magicmarker_sacrifice,
            (long long unsigned int)&g_magicmarker_sacrifice);
    #endif

    unregister_chrdev(LINPMEM_IOCTL_MAJOR, "linpmem_ioctl_interface");

    printk("Pmem driver - Goodbye, Kernel\n");

    return;
}

module_init(ModuleInit);
module_exit(ModuleExit);


