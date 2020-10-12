---
title: "Inspecting AFF4 volumes"
draft: true
weight: 4
---

AFF4 volumes are based around the common Zip compression standard (for
large volumes we use Zip64 extensions). Therefore it is possible to
examine AFF4 volumes using common zip utilities.

```
  ⟫ unzip -l /tmp/test.aff4
    Archive:  /tmp/test.aff4
      aff4://c7c60030-cc3e-43a6-b5d1-1551b29c9918
      Length      Date    Time    Name
      ---------  ---------- -----   ----
      189432     2018-01-15 12:50   usr/bin/mksquashfs
      951952     2018-01-15 12:50   usr/bin/x86_64-w64-mingw32-cpp
      ...
      50929      2018-01-15 12:50   information.turtle
      ...
      ---------                     -------
      315748394                     327 files
```

We can see that each volume has a unique URN, and it contains a file
called "information.turtle". This file contains the AFF4 metadata for
the volume as an RDF turtle file.

We can get the aff4 imager to display the metadata in the volume using
the -V flag::

```
  ⟫ aff4imager -V /tmp/test.aff4
    @prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .
    @prefix aff4: <http://aff4.org/Schema#> .
    @prefix xsd: <http://www.w3.org/2001/XMLSchema#> .

    <aff4://2d874721-267b-40fb-ac20-7bc22a8af883/proc/kallsyms>
      aff4:original_filename "/proc/kallsyms"^^xsd:string .

    <aff4://2d874721-267b-40fb-ac20-7bc22a8af883/proc/kcore>
      aff4:category <http://aff4.org/Schema#memory/physical> ;
      aff4:stored <aff4://2d874721-267b-40fb-ac20-7bc22a8af883> ;
      a aff4:Image, aff4:Map .

  <aff4://2d874721-267b-40fb-ac20-7bc22a8af883/proc/kcore/data>
      aff4:chunkSize 32768 ;
      aff4:chunksInSegment 1024 ;
      aff4:compressionMethod <https://www.ietf.org/rfc/rfc1950.txt> ;
      aff4:size 8531652608 ;
      aff4:stored <aff4://2d874721-267b-40fb-ac20-7bc22a8af883> ;
      a aff4:ImageStream .
```

Note that if we have multiple volumes (as in a split volume set) we
should list all volumes as parameters to -V.

In the above output we see some interesting artifacts of the AFF4 format:

1. All streams within the AFF4 volume have a unique URN. The imager
   creates the URNs based on their original filename, but this is just
   a convenience. The imager also stores the original filename (which
   might contain backslashes on windows).

2. Smaller files (e.g. */proc/kallsyms*) are stored as AFF4 Segments
   which are just regular zip archive members. This means you can
   extract Smaller files using normal zip tools.

3. Larger files are stored as AFF4 ImageStream. This type of storage
   chunks the file data into 32kb chunks, and stores groups of chunks
   in their own zip segment.

4. Finally sparse images (such as memory images) are stores as an AFF4
   Map. The map does not actually store any data itself (the data is
   stored by the stream */proc/kcore/data*) but it specifies a
   transformation of its underlying stream.


Finally using the -l flag enables a listing of all Image streams from the volume.
