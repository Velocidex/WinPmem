#ifndef _PRECOMPILER_H_
#define _PRECOMPILER_H_

// Important switches for compilation flavors.

// DBGPRINT 1/0: Controls the DbgPrint verbosity level.
// DBGPRINT (0) is commonly the most useful and prints *only* unexpected critical/important DbgPrints.
// Not being able to read from a (VSM protected) physical address is not considered a real error, and not printed with "DBGPRINT (0)" setting. 
// These are technically read errors, but uncritical expected ones.
// DBGPRINT (1) might be a bit spammy.
#define DBGPRINT (1)   // Set to 1 for generic DbgPrints!

// Beware, if reading the whole RAM this will spam you!
#define PRINT_PTE_REMAP_ACTIONS  (0)

// Write enabled Winpmem. The code has not been used in a while and
// might be dysfunctional.
#define PMEM_WRITE_ENABLED (0)

//When we are silent we do not emit any debug messages.
#if DBGPRINT == 1
#define WinDbgPrint(...) \
            DbgPrint(__VA_ARGS__);
#else
#define WinDbgPrint(...)
#endif
#endif
