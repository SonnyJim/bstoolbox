# BlueSCSI toolbox for SGI IRIX and Linux
Download files and run 'make', it should spit out a bstoolbox binary.  Currently tested on the following platforms:

Linux Mint
NixOS
IRIX 6.5 (writing to shared directory from host currently broken)

**Treat this software as ALPHA, back up any important data before using it!**

## Usage
```
Please specify device ("/dev/scsi/..."

Usage:   bstoolbox [options] [device]

Example: bstoolbox -s /dev/scsi/sc0d1l0

Options:
        -h      : display this help message and exit
        -v      : be verbose
        -i      : interrogate BlueSCSI and return version
        -l      : list available CDs
        -s      : List /shared directory
        -c num  : change to CD number (1, 2, etc)
        -g num  : get file from shared directory (1, 2, etc)
        -p file : put file to shared directory
        -o dir  : set output directory, defaults to current
        -d num  : set debug mode (0 = off, 1 - on)


Please make sure you run the program as root.
```
