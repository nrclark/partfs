## Introduction ##

PartFS is a FUSE-based tool for accessing a chunk of a SOURCE file as if it
were a separate file at MOUNTPOINT. It's intended for use in
creating/modifying disk image files.

Say that SOURCE was a 600000-byte file with the following layout:

    [Region 1] Bytes 1 to 5000: Bootloader region
    [Region 2] Bytes 5001 to 105000: System data
    [Region 3] Bytes 105001 to 205000: System Partition
    [Region 4] Bytes 205001 to 600000: User Data

PartFS would allow you to mount any of these regions and access them as if
they were standalone files. In the example given above, the correct mounting
parameters would be:

    partfs SOURCE MOUNTPOINT -o offset=0,sizelimit=5000
    partfs SOURCE MOUNTPOINT -o offset=5000,sizelimit=100000
    partfs SOURCE MOUNTPOINT -o offset=105000,sizelimit=100000
    partfs SOURCE MOUNTPOINT -o offset=205000,sizelimit=395000

Note that region 1 could also be mounted with no offset because it starts at
byte 0. Similarly, Region 4 could be mounted with no size-limit because it
goes to the end of the file.

PartFS is logically similar to a loopback-mounted image under Linux. It has
two key differences though. First, it can be done as an unprivileged user.
And second, it doesn't offer direct access to any filesystems stored 
within the mounted image. Consider using with fuse2fs or a similar tool 
or unprivileged filesystem access.

## Current Status ##

At the time of this writing, PartFS is released as version 1.2.0 with a
functional test suite. There are no currently-known bugs. Use with pride.

## License ##

PartFS is proudly released under GPL 2.0.
