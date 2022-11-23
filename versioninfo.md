### 20. Nov  2022, 3.0.1 *Alpha*

Compile latest source code (3.0.1 *Alpha* version) in two steps: 

1. `mc log_message.mc` 

This generates three files needed for eventlog writing: log_message.rc, log_message.h, and MSG00001.bin.
(This step is also described at https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/defining-custom-error-types , "Compiling the Error Message Text File")

Note: eventlog writing can be disabled during development phase. The precompiler switch is found in precompiler.h (all precompiler switches go there now), but you will also need to remove the statement for using the message log ressource file in the vcxproj. Then step one can be skipped.

2. `msbuild /p:configuration=Release/Debug /p:platform=Win32/x64 winpmem.vcxproj`. Or use VS. Build environment must be configured for kernel driver compilation. This is also true for the usermoder testing program.

#### Installing: 

Do not expect a new compiled release with a signed driver again, although this might be possible in future.
You could use testsigning, and load the driver with any suitable tool.

#### Works: 

* Win7, (Win8), Win10, Win10 22H2, (2.0.2 not tested with core isolation, 2.0.1 worked). Win11 should work (untested).

#### Testing: 

This is a devel version. There a simplified usermode code to interface with Winpmem and test its functionality in the 'testing' folder. Compiling and using the mini tool works, too, but please make sure to replace the two years old original driver binaries (*x86.sys and *64.sys)) with the fresh build. 

Please refer to 3.0.1 when reporting bugs and issues for the devel version.

#### Limitations:

* WinXP fell off the support, because of NX allocations (and other pitfalls). Archaeologists will need to adapt the source code first. (The SOURCES and MAKEFILE are kept for this reason.)
* IOspace method on Hyper-V VM using Win10 *and* KD might fail. There was a bug with KD and Hyper-V.
* PTE method has been implemented only for x64.
* Microsoft bug/glitch: on Hyper-V, the longer a VM runs with the "Dynamic memory" feature enabled, the more fragmentation occurs on the memory range runs. (Up to a thousands). We cannot fix that.

#### Assumed to work correctly:
* everything else. Report bugs if you find one.
