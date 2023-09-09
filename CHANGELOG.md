### 27. Nov 2022, 3.0.3 *alpha*

No bugs detected anymore, only small improvements, testing (and knowledge increase). 
Tried to resolve an issue with not being able to read certain pages inside the normal physical ranges, KD fails at these, too. It's not a Winpmem error.

* **Important note**: the iospace method cannot be used as general purpose physical reading method. (For details, please refer to my comments in read.c.) In particular, it should urgently be thrown out of the mini tool. (Applicable also for the 2.0.1 version in the master branch.) It can cause hazard to use it in this purpose.
* 3.0.3: the iospace method has been removed from the usermode mini tool, because it is not applicable for the creation of complete memory dumps.
* Added another test example code.
* DbgPrint minor updates.
* Reading some pages fail when reading for example a complete memory dump (while staying within the normal physical ranges, not talking about reserved memory). The PFN is all that is known and STATUS_ACCESS_DENIED is returned when trying to read. How odd. Catched one of these 'resisting' nt!ZwMapViewOfSection nonzero mapped buffers in flagranti, with PTE indicating that it's totally valid, while unreadable. Not even KD is able to read from that (example below). There ought to be a reason, which currently escapes me, why nt!ZwMapViewOfSection is returning a seemingly valid address with sane looking PTE. It happens on almost all machines. It is not bound to any particular machine or OS. It's normal memory, and not reserved memory. In the exaple below, it's a usermode (read-only, present/valid) page. I must be overlooking something. It's not a fault of Winpmem, as KD fails equally. Current best bet is that *credential guard* was enabled on these machines (I tend to do that).

```
0: kd> !pte 248`9ac2b000
                                           VA 000002489ac2b000
PXE at FFFFD2E974BA5020    PPE at FFFFD2E974A04910    PDE at FFFFD2E9409226B0    PTE at FFFFD281244D6158
contains 0A000000047A8867  contains 0A00000004AA9867  contains 0A000000456B1867  contains 8A0000000C3CB025
pfn 47a8      ---DA--UWEV  pfn 4aa9      ---DA--UWEV  pfn 456b1     ---DA--UWEV  pfn c3cb      ----A--UR-V
```

Valid/present? Yes. Readable? **No**.

```
0: kd> !db c3cb000
Physical memory read at c3cb000 failed
```
If it was reserved memory it would be understandable, but against normal usermoge pages? Odd.


### 26. Nov 2022, 3.0.2 *Alpha*

* Small update in example test code: GENERIC_RED/GENERIC_WRITE => FILE_GENERIC_READ/FILE_GENERIC_WRITE. Otherwise won't work on Win11.
* Error DbgPrint output:
    * When handling an exception, always get the NTSTATUS code from the exception for a precise error printout.
    * When handling an exception, use the original exception NTSTATUS code, do not return an own custom NTSTATUS, unless it is a routine answering back to a userspace program.
    * More precise and consistent error printing.
* Feature upgrade: large page support added for PTE parsing.
    * Huge page (1 GB pages) not done yet (never encountered => cannot test).
* Bugfix (important): DbgPrint was badly messed up in the PTE parsing code, corrected.
* Bugfix (important): added more (important) sanity checking in PTE parsing code.
* Bugfix (important): if there is an issue on return of ZwMapViewOfSection, do not try to unmapview the empty pointer.
    * Also check pointer before calling ZwUnmapViewOfSection or MmUnmapIoSpace, even if it seems uneeded.
* Bugfix (important): really check if a reading mode has been set before calling a reading method.

### 20. Nov 2022

#### Summary 3.0.1 *Alpha*:

* Major code refactoring for readability, stability, safety, and performance. PTE remap method rework. Many bugfixes. Security enhanced. Feature upgrade (reverse search). Functionality and correctness testing now much easier, including easier way for users to diagnose and test and show what's going wrong.

#### Important
Do not expect a new compiled release for quite a while. Reasons:
a) This needs more (of your) testing.
b) Signing. Signing is an issue.

Hence, compiling becomes important again, and compile instructions have been added.

#### Detailed changelog

* Changelog is now markdown.
* Major refactoring, increased version to ~~2.0.2~~ 3.0.1.
* Added compile instructions.
* Major refactoring of the code, improvements to readability, stability, safety, and performance. Added many sanity checks that were missing before.
    * Major rework of the PTE remapping code. It has many checks that were missing before and also better self-knowledge of when things go the wrong way.
    * Now Winpmem/the user will know objectively that everything went well with restoring the rogue page. (The rogue identification string is for used for that. It's not just placebo.)
* Every bit of code using 64-bit only technology is encapsuled by precompiler switch now. This makes 32 bit drivers smaller, spares them of unusable code, and prevents OACR/code analysis from getting mad.
* Feature upgrade: reverse search. This is needed for testing and verifying the correctness of all three methods.
* Testing: using the new reverse search, it becomes much easier to quickly check if a method works correct or not. Previously the only way was to fully read the whole RAM (ahh!) and load the dump into volatily, which is cumbersome. Also, users can do it now, and this might help finding and fixing bugs much faster and less painfully.
* Speaking of testing, a testing/demo source code has been added. Use at your own peril. You need an option to load the (development) driver and a msbuild/VS environment support kernel driver building.
* Made Winpmem better readable for Microsofts static code analysis (and possibly, humans too), which will help finding bugs and prevent tired humans from making stupid programming errors.
* Code Analysis is now lvl 4 with OACR fully enabled. I checked compilation for both 32 and 64 bit. OACR is annoying but prevents a good portion of stupid errors. (It already did)).
* Winpmem was actually NOT Windows-XP compatible anymore, due to use of NX allocation somewhere deeper in the code.
However, an upgrade to NX seems reasonable anyway. After several experiments with combining both backward-compability and make NX work, it became apparent that --against the official documentation-- Win7 "supports" NX in terms of interpreting the NonpagedPoolNX (512 or 0x200) as NonPagedPool (0). Don't make any mistake: Win7 has no NX pool. But thanks to a silent update to the kernel(?), the win7 kernel treats NX allocations "as usual" and will not be irritated. For the driver programmer: NonpagedPool allocations can simply be replaced with its NX equivalent without hassle. The deal is, Win7 will not get the benefit, but any modern OS will. Windows XP, however, probably didn't get this silent update, and might perhaps crash. (Not tested.)
* Winpmem should be considered "not supported" for Windows XP now. Archaeologists should use the old rekall version of Winpmem.
* Made Winpmem write an eventlog entry about its start. Future versions of Winpmem will now always announce their presence to the system owner thanks to the event logging system. Just in case.
* (The WDK7600 MAKEFILE and SOURCES are kept to support capable archaelogists to edit the source for Windows XP support for themselves, since Winpmem is still 'almost' compatible. )
* for safety reason, setting the preferred method is now required, not changeable during uptime of the driver. (No more "ok, I'm with method PTE, and currently reading something, but juuuust changed my mind, switching to Physical Memory device method...!".
    * The provided mini exe always sets a method (defaults to PTE), no noticeable change there. Changing the method in plain middle of physical RAM reading always was an unsafe idea and now is prevented hard.
    * If you do not use our mini tool, please don't forget to set your method of choice. (Example can be found in testing.)

* Bugfixes: previously, if there was an error in the DriverEntry, Winpmem would not have cleaned up. This has been corrected!
* Bugfixes: previously, the user might have exited without closing handle. And the driver would never release it, because it would not check in the Unload. This is no longer relevant.
* Bugfixes: the write routine was dysfunctional, although precompiler disabled anyway. Hopefully corrected the write routine (did not test) and added a missing ZwUnmapViewOfSection (in error path of write routine). Write is still precompiler-disabled.
* Bugfixes: not propagating ntstatus correctly.
* Possible unknown behavior fix: reading from physical memory device is passive level file I/O (ZwMapViewOfSection or ZwReadFile requires PASSIVEL LEVEL). However, acquiring a fast mutex will raise IRQL on thread to APC_LEVEL. In particular, the I/O manager will indicate when a ZwMapViewOfSection or ZwReadFile has finished, and this would be blocked if on APC_LEVEL already (because it is an APC). In any case, this has been resolved. (Though wondering how nobody ever complained.) Reading from the high level physical memory device will now be done on passive, as normal file I/O, no mutex is needed for that. Spontaneous method switching has been disabled.
* Bugfixes (neglectable): hopefully fixed optical issues in the usermode "mini tool" with errors not propagated correctly. Not tested, priority too low. ;-)

* Wanted TODO: radically shorten the ioctl get-info struct. However, doing so will break maintained offset stability, e.g., users that use this optional get-info ioctl code must adapt their source code.


### 7. Feb 2022

* Fixes in KernelGetModuleBaseByPtr: on successfull return the buffer is not freed. This is a memory leak, but happens only one time (1).
Second, added a buffer check in KernelGetModuleBaseByPtr (2), and third, removed an unneeded checking in the for loop (3), which PREfast marked as nonsense check.
Impact:
For (1), the allocated memory loss is a one-time loss.
For (2), the added check is only of importance for clean code conduct. An allocation of such a size is always successfull unless the OS is dying, in which case we are screwed no matter what we do.
For (3), checking a field of char[256] in the middle of a flat buffer only wastes time and makes no sense.

* Memory leak fix in kernelmode winpmem.c line 118: `if (number_of_runs > NUMBER_OF_RUNS) return STATUS_INFO_LENGTH_MISMATCH;`
The memory allocated from `MmPhysicalMemoryRange = MmGetPhysicalMemoryRanges();` is not freed in this error branch.
Impact: some memory of varying size will be lost as often as the user launches a memory read ioctl.

* Recommendation 1: Collecting ntoskrnl base address and the KPCR info is historical artefact data and not used anymore. We should remove it for the sake of clean code and simplicity. Especially the KPCR info code causes various compability issues.

* Recommendation 2: both methods MmMapToIoSpace and Device\\PhysicalMemory usage are deprecated. The second could be kept for 32 bit OS users, but the first one is a candidate for removal?


### 6. Oct 2020

New features & major revision (see below).

Changes:

* FAST I/O read. (Who needs IRPs when there is FAST I/O? ;-))

* Fixed errors occurring with x86.

* Unlimited read size support!
  Ever felt you should be able to read a 100 MB in a single read request?
  Or even a Gigabyte, at once? (If malloc allows that?)
  Well, read from memory with unlimited size now!

* FAST. Winpmem.exe took 15 seconds to read a whole 8 GB RAM.

* Safer than the old buffered version.
  The old buffered version allowed to basically kill the system
  by specifying a too large read sizes. (E.g., starting with 100 MB +)
  No more: read from Winpmem like from a normal filesystem driver!

* winpmem.exe now features a provisoric cute icon.

* Current usermode part rewritten to issue "large" (16 MB) reads (faster).
  Formerly it was reading in tiny 4096 byte units.

* CTL codes and struct data shared between driver and usermode is kept in 'userspace_interface'.
  1. To know which data is shared across kernelspace and userspace, and there is only one place now.
  2. It cannot happen that one side changes shared data and the other does not get updated.

* Cleaned code, code refactoring, removed antique files.

* Removed 'fcat.exe' (netcat), which was causing Antivirus to go wild.
  (Probably due to its long history of having been used as malware intel tool.)
  It would be better to rewrite this using a clean-room approach.
  AVs going wild is very cumbersome, to say the least.

* Removed the elf coredump option as it was causing issues.
  It could be taken in again, if needed, but would probably
  require some maintenance.

* Updated documentation still mentioning rekall.

* Updated author info (not changing the copyright).

