#ifndef _PRECOMPILER_H_
#define _PRECOMPILER_H_


// Important switches for compilation flavors.

// Control whether there should be DbgPrint or not.
#define DBGPRINT (1)   // Set to 1 for DbgPrints!

// beware, if reading the whole RAM this will spam you!
#define PRINT_PTE_REMAP_ACTIONS  (0)

// Eventlog writing? You will need to do "mc log_message.mc" before compilation. Adapt eventlog messages in log_message.mc too your liking.
#define EVENTLOG_WRITING   (1)

//When we are silent we do not emit any debug messages.
#if DBGPRINT == 1
#define WinDbgPrint(...) \
			DbgPrint(__VA_ARGS__);		
#else
#define WinDbgPrint(...)
#endif
#endif