---
title: "Acquiring Files"
draft: true
weight: 3
---

The WinPmem imager can also acquire multiple files into the AFF4
volume. These can be devices (such as disks using /dev/sda) or logical
files.

## Acquiring a disk image

   ```
     WinPmem.exe -i \\.\c: -o output.aff4 -dd
   ```

   Note that by default the aff4 imager will append new streams to the
   output volume if it already exists. This is useful for appending
   more relevant evidence after the initial acquisition is
   completed. If you dont want this behaviour you can specify the -t
   (--truncate) flag to truncate the output volume before adding the
   new stream.


## Acquiring multiple logical files

   ```
     WinPmem -i C:/Windows/*.exe -o output.aff4
   ```

   Using glob expressions as input files will be expanded as required
   to include all filenames matching the globs. This works also on
   Windows which does not expand globs on the shell.

## Acquiring files newer than 30 days can be done by using the find
   command on Linux or Powershell script on Windows.

   ```
     find /usr/bin/ -ctime -30 | WinPmem -i @ -o /tmp/output.aff4
   ```

   Or on Windows:

   ```
     powershell -Command "Get-ChildItem f:\ | Where{$_.LastWriteTime -gt
        (Get-Date).AddDays(-7)} | SELECT Name | ft -hidetableheaders" |
        WinPmem -i @ -o /tmp/output.aff4
   ```

   Using a single @ as the input filename, makes the aff4 imager read
   the list of files to acqiure from stdin. This allows for more
   sophisticated pre-processing and makes it easier to acquire files
   with spaces or special characters in their names (without having to
   worry about shell escapes). In the above example we use the find
   unix command to identify files newer than 30 days and also add them
   to the image.

## Enabling multiple threads

   ```
     WinPmem.exe -o /tmp/output.aff4 --threads 6
   ```

   The aff4 imager uses a single thread by default, but if your
   machine has more cores, then you will see vastly better performance
   by allowing more threads to run. This is particularly important
   when using the default compression of the zlib compressor which
   needs more CPU resources.

## Enabling snappy compression

   ```
     WinPmem.exe -o output.aff4 --compression snappy
   ```

   The Snappy compression engine is much faster than the default zlib
   but trades off compression size. Enabling snappy compression will
   result in slightly larger images but should complete faster.


## Splitting an image into multiple volumes

   ```
     WinPmem.exe -o output.aff4 --split 650m
   ```

   Some images are very large. By enabling splitting images it is
   possible to restrict the maximum size of each volume. The imager
   will close off each volume as it is done with it, and so you can
   start uploading, archiving each volume as soon as it is
   finished. Note that the same stream may be split across one or more
   volumes so you will need all volumes to properly extract the
   stream.

## Acquiring into standard output

   ```
     WinPmem.exe -o - | gsutil cp - gs://rekall-test/test.aff4
   ```

   If the output filename is specifies as a single dash ("-"), the
   imager writes the AFF4 volume to stdout. This allows the image to
   be piped to a different tool. The example above streams the image
   directly to a cloud storage bucket without needing to write a
   temporary local copy.
