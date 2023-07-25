#ifndef _PRECOMPILER_H_
#define _PRECOMPILER_H_

#define SANE_KERNEL_VA  (0xffff000000000000)
// for slightly enhanced kernel VA pointer checking.

// Important switches for compilation flavors.

// Control whether there should be DbgPrint or not.
#define DPRINT   // print DbgPrint info.
// If not defined, only error will be printed, as well as
// the initial dbgprint in init/unload

#define LEAK_CR3_INFO   // if defined, dbgprints CR3.
// I don't know if it makes sense to take special care of controlling cr3 dbgprint?
// I will leave that subject to community wishes.

// Beware, if reading the whole RAM this will totally spam you!
// This really needs its own switch.
#define PRINT_PTE_REMAP_ACTIONS

#endif
