---
title: "Winpmem Documentation"
draft: false
weight: 1
---

## Documentation

This is the winpmem documentation.

We currently release a simple imager which can only write RAW
images. Simple run it with the name of the image file:

`winpmem_mini_x64.exe physmem.raw`

# Acquisition modes.

Three acquisition modes are implemented:

1. PTE remapping mode - this is the default and is the most stable
2. MMMapIoSpace mode - uses the MMMapIoSpace kernel API
3. PhysicalMemory mode - passes a handle to the tradition `\\.\PhysicalMemory` device.

# Reporting issues

There is an issue board at https://github.com/Velocidex/WinPmem/issues
