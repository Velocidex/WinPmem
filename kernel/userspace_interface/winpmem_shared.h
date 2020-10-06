#ifndef _WINPMEM_SHARED_H_
#define _WINPMEM_SHARED_H_

// Contains all the data shared between the driver and the usermode part.


#define PMEM_DRIVER_VERSION "2.0.1"
#define PMEM_DEVICE_NAME_ASCII "pmem"  // the name for normal userspace usage.
#define PMEM_DEVICE_NAME L"pmem"       // preferred by the driver.
#define PMEM_SERVICE_NAME TEXT("pmem") // and this is finally the service name.

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

// Some standard integer sizes.
typedef unsigned __int64 u64;
typedef unsigned __int32 u32;
typedef unsigned __int16 u16;
typedef unsigned __int8 u8;


// Available modes
#define PMEM_MODE_IOSPACE 0
#define PMEM_MODE_PHYSICAL 1
#define PMEM_MODE_PTE 2
// #define PMEM_MODE_PTE_PCI 3 // deprecated

#define NUMBER_OF_RUNS   (20)

#pragma pack(push, 2)


typedef struct _WINPMEM_MEMORY_INFO
{
  LARGE_INTEGER CR3;
  LARGE_INTEGER NtBuildNumber; // Version of this kernel.

  LARGE_INTEGER KernBase;  // The base of the kernel image.


  // The following are deprecated and will not be set by the driver. It is safer
  // to get these during analysis from NtBuildNumberAddr below.
  LARGE_INTEGER KDBG;  // xxx: I want that. Can I have it?

  // xxx: Support up to 32/64  processors for KPCR. Depending on OS bitness
  #if defined(_WIN64)
  LARGE_INTEGER KPCR[64];
  #else
  LARGE_INTEGER KPCR[32];
  #endif
  // xxx: For what exactly do we need all those KPCRs, anyway? Sure, they look nice.

  LARGE_INTEGER PfnDataBase;
  LARGE_INTEGER PsLoadedModuleList;
  LARGE_INTEGER PsActiveProcessHead;

  // END DEPRECATED.

  // The address of the NtBuildNumber integer - could be used to find the kernel base. 
  LARGE_INTEGER NtBuildNumberAddr;

  // As the driver is extended we can add fields here maintaining
  // driver alignment..
  LARGE_INTEGER Padding[0xfe]; 

  LARGE_INTEGER NumberOfRuns;

  // A Null terminated array of ranges.
  PHYSICAL_MEMORY_RANGE Run[NUMBER_OF_RUNS];
  
} WINPMEM_MEMORY_INFO, *PWINPMEM_MEMORY_INFO;

#pragma pack(pop)

#endif