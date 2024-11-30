#ifndef _WINPMEM_SHARED_H_
#define _WINPMEM_SHARED_H_

// Contains all the data shared between the driver and the usermode part.


#define PMEM_DRIVER_VERSION "4.1"
#define PMEM_DEVICE_NAME_ASCII "pmem"  // the name for normal userspace usage.
#define PMEM_DEVICE_NAME L"pmem"       // preferred by the driver.
#define PMEM_SERVICE_NAME TEXT("winpmem") // and this is finally the service/display name.

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

#define NUMBER_OF_RUNS   (300)  // increased allowed size. Backward compability should be given, since the array is at the end. 

#pragma pack(push, 2)


// For programmers, please do not rely on unchangeability of this struct and be prepared that this struct might change.
// It contains too much deprecated data.

typedef struct _WINPMEM_MEMORY_INFO
{
  LARGE_INTEGER CR3;  // System process Cr3.
  LARGE_INTEGER NtBuildNumber; // Version of this kernel.

  LARGE_INTEGER KernBase;  // The base of the kernel image.


  // The following are deprecated and will not be set by the driver. 
  
  LARGE_INTEGER KDBG;  // Deprecated since a long time now. 
  
  #if defined(_WIN64)
  LARGE_INTEGER KPCR[64]; // still filled out pro forma
  #else
  LARGE_INTEGER KPCR[32];  // still filled out pro forma
  #endif

  LARGE_INTEGER PfnDataBase;  // Deprecated since a long time now. 
  LARGE_INTEGER PsLoadedModuleList;  // Deprecated since a long time now. 
  LARGE_INTEGER PsActiveProcessHead;  // Deprecated since a long time now. 

  // END DEPRECATED.

  LARGE_INTEGER NtBuildNumberAddr;  // still filled out pro forma.

  // As the driver is extended we can add fields here maintaining
  // driver alignment..
  LARGE_INTEGER Padding[0xfe];

  LARGE_INTEGER NumberOfRuns;

  // A Null terminated array of ranges.
  PHYSICAL_MEMORY_RANGE Run[NUMBER_OF_RUNS];

} WINPMEM_MEMORY_INFO, *PWINPMEM_MEMORY_INFO;

#pragma pack(pop)

#endif
