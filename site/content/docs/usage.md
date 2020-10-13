---
title: "Overview"
draft: true
weight: 1
---

WinPmem is developed as part of the AFF4 imager project. AFF4 is an
advanced, open forensic imaging format. The format has been
standardized in the [AFF4 Standard Specification](https://github.com/aff4/Standard), and it is increasing more supported by other tools.

WinPmem by default will use AFF4 to store the memory image, but it is
also possible to write the image in RAW format or ELF format. You
might want to do this if you are going to use the image with tools
that can not read AFF4 natively. However, note that using these
imaging format will cause WinPmem to skip acquisition of multiple
files and metadata making the image harder to use.


Why use AFF4?
-------------

The AFF4 format is specifically designed to support a wide range of
use cases in forensic evidence capture and preservation. Some of the
benefits of using AFF4 over another proprietary format are

- The AFF4 format is an opened standard with many open source
  implementations in a variery of languages. The format itself is very
  simple and so it will always be possible to extract data even
  without an proper implementation.

- AFF4 storage is designed around the widely used Zip file
  format. This means that you can use typical Zip recovery utilities
  to repair corrupted images, inspect the content of images etc.

- AFF4 supports multiple streams within the same volume. This allows
  multiple sources of evidence to be captured into the same case
  file. For example, related memory, disk and logical file streams can
  be handled simultaneously. WinPmem uses this property to store
  memory images in the same volume as important files like drivers and
  kernel image, thus assisting the analysis phase.

- AFF4 supports sparse streams (using the Map stream type). This is
  useful for representing memory images (which might have gaps in
  them) as well as simply representing read errors (rather than simply
  zero padding them).

- AFF4 supports arbitrary metadata as RDF statements.


The Pmem memory acquisition suite includes the *WinPmem*, *OSXPmem*
and *LinPmem* tool. Each tool implements memory acquisition for its
respective OS but all use largely the same options. All the below
commands should also work on any tools in the pmem suite. For
simplicity we demonstrate with WinPmem.

You can download_ the latest release of the aff4 imager through the
[project's release page](https://github.com/Velocidex/c-aff4/releases). Releases are
statically built for their respective platforms in order to ensure
that they can be easily deployed with minimal system requirements -
even on very old systems. You do not need to include any runtime
dependencies (like Visual C runtimes).

The following section describes how to use the imager effectively. You
can get some helpful messages from the imager itself when run with the
*--help* flag.
