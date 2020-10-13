---
title: "Acquiring Memory"
draft: true
weight: 2
---

Acquiring memory using WinPmem is a very simple process:


```
F:\>winpmem.exe -o test.aff4 -dd
2019-05-17 02:26:22 I This is The WinPmem memory imager. version 3.3rc1
2019-05-17 02:26:22 I Extracted 45368 bytes into C:\Users\test\AppData\Local\Temp\pmeB5CC.tmp
2019-05-17 02:26:22 I Driver Unloaded.
2019-05-17 02:26:22 I Loaded Driver C:\Users\test\AppData\Local\Temp\pmeB5CC.tmp
2019-05-17 02:26:22 I Setting acquisition mode 2
2019-05-17 02:26:22 I CR3: 0x00001AA000
 5 memory ranges:
 Start 0x00001000 - Length 0x0009E000
 Start 0x00100000 - Length 0x00002000
 Start 0x00103000 - Length 0xBFDDD000
 Start 0xBFF00000 - Length 0x00100000
 Start 0x100000000 - Length 0x52400000

2019-05-17 02:26:22 W Output file test.aff4 will be truncated.
2019-05-17 02:26:22 I Setting acquisition mode 2
2019-05-17 02:26:22 I Setting acquisition mode 2
2019-05-17 02:26:22 I Dumping Range 0 (Starts at 0x001000, length 0x09e000
2019-05-17 02:26:22 I Dumping Range 1 (Starts at 0x100000, length 0x002000
2019-05-17 02:26:22 I Dumping Range 2 (Starts at 0x103000, length 0xbfddd000
2019-05-17 02:26:23 I  Reading 2103000 32 MiB / 4387 (25 MiB/s)
2019-05-17 02:26:24 I  Reading 4103000 64 MiB / 4387 (26 MiB/s)
2019-05-17 02:26:25 I  Reading 6103000 96 MiB / 4387 (28 MiB/s)
```

* The `-o` flag instructs WinPmem to create a new AFF4 volume with the
  name `test.aff4`.

* The -d flag instructs WinPmem to produce more vebose output (twice
  for progress reporting).

* We see that WinPmem extracts the kernel driver into the temporary
  directory and loads it into the kernel. The driver provides access
  to raw memory via a number of acquisition methods but the default is
  usually the best.

* WinPmem then displays the detected physical memory ranges and
  continues to dump each range.


* By default WinPmem uses 2 threads to compress the image, however
  most machines can use multiple cores. Using the `--thread` flag you
  may increase this number. Using more threads will result in quicker
  compression, and therefore quicker imaging.

* By default WinPmem will use an AFF4 image and will store the memory
  image using an AFF4 Map object with a compressed backing stream. If
  you want to produce a raw file format or an elf file, you may
  specify the `--format elf` or `--format raw` to produce these image
  formats. Note that due to limitations in these image formats it is
  not possible to include additional streams. Neither of these formats
  support compression either (but ELF format supports sparse data
  runs).
