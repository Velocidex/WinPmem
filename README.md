# Linpmem

Linpmem is a Linux x64-only tool for reading physical memory.

As with Winpmem, it is published under Apache 2.0 license.
It is a bit different from the current Windows version.

Similar to Winpmem, this is not so much a traditional memory dumper (but
is totally capable of memory dumping).
However, Linpmem can read from just *any* physical address, explicitly
including *reserved memory* and *memory holes* and more generally, all
physical addresses that have no virtual addresses associated (with no existing PTE).

Linpmem offers a variety of possible access modes to read physical memory,
such as byte, word, dword, qword and buffer access mode (where buffer access mode is suggested in most standard cases).
If reading requires an aligned byte/word/dword/qword read, Linpmem will precisely do that.

The user must compile the Linpmem driver for its own kernel.
(As much as we would like to provide precompiled driver binaries, this is not how Linux works.)
The Linpmem user interface features:

1. CR3 read
2. Read physical address (accessmode byte,word,dword,qword, or buffer).
3. VTOP translation service.

### Content

* The root folder contains the drivers source code. Use `make` to compile it.
* In 'demo', find `test.c` which demonstrates and explains the user interface of Linpmem.

### Compiling

There is no other way than to compile it yourself.

#### Short:

1. `make`
2. `(sudo) insmod linpmem.ko`
3. `(sudo) mknod /dev/linpmem c 64 0`
4. `cd demo`
5. `gcc -o test test.c`
6. `sudo ./test`
7. `optional: (sudo) rmmod linpmem.ko`

Don't execute test without having read `test.c`!

#### Driver compilation (detailed):

1. Download linux-headers (best from your repo)
2. Make sure you have gcc, make, ..., and a decent text-viewer/source code editor/IDE.
3. `precompiler.h`: check if everything is at your liking.
4. `make`
5. `sudo insmod linpmem.ko`, or root: `/usr/sbin/insmod pathTo-Linpmem.ko`
6. `sudo dmesg`. Be sure to check for any error print. Please report if you see any!
7. On success: `configured successfully` message. It's ready to use.

The insmod way is the currently recommended way to load Linpmem.


#### Usermode program (detailed)

1. Go to demo folder
2. (Read `test.c` carefully, you cannot use Linpmem with zero knowledge.)
3. Go to main(), un/comment the tests as you like.
4. `gcc -o test test.c`, or your own program.
5. `sudo mknod /dev/linpmem c 64 0` (in case you aren't familiar: you only need to create this 1x time.)
6. `sudo ./test`, or your own program.
7. Additionally, watch dmesg output. Please report errors if you see any!

Warning: if there is a dmesg error print from Linpmem telling to reboot, better do it immediately.

Warning: this is an early version.


### Tested

* Debian self-compiled, Qemu/kvm, not paravirtualized.
    * pti: off
* Debian 12: Qemu/kvm, fully paravirtualized.
    * pti: on
* Ubuntu server, Qemu/kvm, not paravirtualized.
    * pti: on
* Fedora 38, Qemu/kvm, fully paravirtualized.
    * pti: on
* Baremetal Linux test, AMI BIOS: Linux 6.4.4, pti on.
* Baremetal Linux test, HP: Linux 6.4.4, pti on.


### Issues

* Large page read: check current logic if it is really allowing to read all the large page.
* Huge page read: not implemented. Linpmem recognizes a huge page and rejects the read, for now.
* No idea how to control the processor caching attribute on Linux. Warning: reading from mapped io and DMA space will be done with caching enabled.

### Future work

* Force ignore page boundary for reading. If you know it's contiguous memory, read across as many pages as you like.

* Easier foreign CR3 reading. Specify a foreign process you want the CR3 from in the CR3 query IOCTL and it will be returned.

* Control of the processor cache attribute for reading. For uncached reading of mapped I/O and DMA space.

* More differentiated status/error states (driver status and request status).

### Potential Incompabilities

Not tested, but these may potentially cause problems:

* Secure Boot (Ubuntu): the driver will not load without signing.
* AMD SME
* Intel TDX
* Perhaps Pluton chips?

Although they are very rare to encounter.
It needs to be enabled by the system owner, so you should know if you are using it.

(Please report potential issues on this.)
