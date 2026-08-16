# BlueSCSI toolbox for SGI IRIX and Linux
Download files and run 'make', it should spit out a bstoolbox and bswifi binary.  Currently tested on the following platforms:

IRIX 6.5 ([Daynaport driver](https://github.com/techomancer/irixdayna))

Linux Mint 22  ([Daynaport driver](https://github.com/jflitton/daynaport-scsilink-linux-driver))

NixOS




## bstoolbox Usage
```
Usage:   bstoolbox <device> [options]

example: bstoolbox /dev/scsi/sc0d1l0 -s

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
        -w      : get current working directory
        -W dir  : set working directory
        -L      : Show BlueSCSI log
        -d num  : set debug mode (0 = off, 1 - on)


Please make sure you run the program as root.
```

## bswifi Usage
```
Usage:
  bswifi [-v] <device> scan
  bswifi [-v] <device> info
  bswifi [-v] <device> join <ssid> <key> [channel]

Example: bswifi dp0 join MYNETWORK MYPASSWORD

Commands:
  scan                  Start Wi-Fi scan
  info                  Get current Wi-Fi information
  join SSID KEY [CHAN]  Join an access point

Options:
  -v                    Verbose/debug output
  -h                    Show this help

Please make sure you run the program as root.
```

