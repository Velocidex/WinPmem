# The Winpmem memory acquisition driver and userspace.

Winpmem has been the default open source memory acquisition driver for
windows for a long time. It used to live in the Rekall project, but
has recently been separated into its own repository.


## Copyright

This code was originally developed within Google but was released
under the Apache License.

### Description

Winpmem is a physical memory imager with the following features:

- Open source

- Support for WinXP - Win 10, x86 + x64. The WDK7600 can be used to 
  include WinXP support.
  As default, the provided Winpmem executables will be compiled with WDK10, 
  supporting Win7 - Win10, and featuring more modern code.

- Three different independent methods to create a memory dump.
  One method should always work even when faced with kernel mode rootkits.

- Raw memory dump image support.

- A read device interface is used instead of writing the image from the kernel
  like some other imagers. This allows us to have complex userspace imager
  (e.g. copy across network, hash etc), as well as run analysis on the live
  system (e.g. can be run directly on the device).

The files in this directory (Including the winpmem sources and signed binaries),
are available under the following license: Apache License, Version 2.0

### How to use

There are two winpmem executables: winpmem.exe and winpmem64.exe.  
Both versions contain both drivers (32 and 64 bit versions). It does 
not matter which version is invoked, it will choose the right driver anyway. 

(Winpmem64.exe can be used in a special environment with no WOW64 emulation 
support (such as WINPE) and/or the need for the native bitness.)

The python program is currently under construction.


### The Python acquitision tool winpmem.py

Under construction by Scudette.



### winpmem.exe (standalone executable)

This program is easiest to use for incident response since it requires no other
dependencies than the executable itself. The program will load the correct
driver (32 bit or 64 bit) automatically and is self-contained.

NOTE: Currently it is testsigned and needs "bcdedit /set testsigning on" (and reboot).

NOTE: Do not forget to invoke winpmem as admin/elevated!

Examples:
winpmem.exe physmem.raw
Writes a raw image to physmem.raw using the default method of acquisition.

winpmem.exe 
Invokes the usage print / short manual.

To acquire a raw image using specifically the MmMapIoSpace method:
c:\..> winpmem.exe -1 myimage.raw

The driver will be automatically unloaded after the image is acquired!



Experimental write support
--------------------------

The winpmem drivers support writing to memory as well as reading. 
This capability is a great learning tool since many rootkit hiding
techniques can be emulated by writing to memory directly. 

This functionality should be used with extreme caution!

NOTE: Since this is a rather dangerous capability, the signed binary drivers
have write support disabled. The unsigned binaries (really self signed with a
test certificate) can not load on a regular system due to them being test self
signed. You can allow the unsigned drivers to be loaded on a test system by
issuing (see
http://msdn.microsoft.com/en-us/library/windows/hardware/ff553484(v=vs.85).aspx):

Bcdedit.exe -set TESTSIGNING ON

and reboot. You will see a small "Test Mode" text on the desktop to remind you
that this machine is configured for test signed drivers.

Additionally, Write support must also be enabled at load time:

winpmem.exe -w -l

This will load the drivers and turn on write support.

