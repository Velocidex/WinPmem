### 3.0.x *alpha version*

Compile latest source code (3.0.x *alpha* version) in two steps:

1. `mc log_message.mc`

This generates three files needed for eventlog writing: log_message.rc, log_message.h, and MSG00001.bin.
(This step is also described at https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/defining-custom-error-types , "Compiling the Error Message Text File")

Note: eventlog writing can be disabled during development phase. The precompiler switch is found in precompiler.h (all precompiler switches go there now), but you will also need to remove the statement for using the message log ressource file in the vcxproj. Then step one can be skipped.

2. `msbuild /p:configuration=Release/Debug /p:platform=Win32/x64 winpmem.vcxproj`. Or use VS. The build environment must be configured for kernel driver compilation. This is also true for the usermode testing program.

#### Installing:

Do not expect a new compiled release with a signed driver again, although this might be possible in future.
You could use testsigning, and load the driver with any suitable tool.

#### Works:

* Win7, (Win8), Win10, Win10 22H2, Win11.
* Accessing read-only physical pages while under core isolation will make the secure kernel return STATUS_ACCESS_DENIED. Winpmem will handle this gracefully.

#### Testing/using:

This is a devel version. There is simplified usermode code (in 'testing') to directly interface with Winpmem and test its functionality.
Compiling and using the mini tool works, too, but please make sure to replace the two years old original driver binaries (winpmem_x86.sys and winpmem_64.sys) with the new drivers.

Please refer to 3.0.x when reporting bugs and issues for the devel version.

#### Limitations:

* WinXP fell off the support, because of NX allocations (and other pitfalls). Archaeologists will need to adapt the source code first. (The SOURCES and MAKEFILE are kept for this reason.)
* IOspace method **is not** for general purpose reading. Use this method *only* in appropriate scenarios (with locked pages).
* PTE method has been implemented only for x64.
* Microsoft bug/glitch: on Hyper-V, the longer a VM runs with the "Dynamic memory" feature enabled, the more fragmentation occurs on the memory range runs. (Up to a thousands). We cannot fix that.

#### Assumed to work correctly:
* everything else. Report bugs if you find one.
