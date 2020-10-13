---
title: "Extracting streams from AFF4 volumes"
draft: true
weight: 5
---

Analysis tools which already support AFF4 will be able to use the AFF4
image directly, but sometimes you may come across a tool which does
not. In this case you will want to extract the RAW streams out of the
AFF4 volume.

To extract streams from an AFF4 volume we use the *-e* flag.

## Extract streams by using wild cards

   ```
     WinPmem.exe -e '*/kallsyms' --export_dir /tmp/export/ /tmp/test.aff4
   ```

   By default export directory is the current directory. The imager
   will create a directory structure under the export directory which
   contains all the matched files.

## Extract streams from stdin

   ```
     WinPmem.exe -l test.aff4 | grep exe | \
          WinPmem.exe -e @ --export_dir export/ test.aff4
   ```

   In the above example, we list the streams in the volume, then grep
   only the executable files, and extract them into the export
   directory.
