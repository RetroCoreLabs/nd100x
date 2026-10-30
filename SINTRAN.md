# SINTRAN DISK IMAGE

Several machines that talk XMSG or COSMOS need one CPU number each: see [docs/CPU-NUMBER-DISK-IMAGES.md](docs/CPU-NUMBER-DISK-IMAGES.md).

To boot SINTRAN you need a SMD disk with SINTRAN installed. 
In the 'images' folder there is a compressed image of SMD0.IMG.

Uncompress and copy to root folder.

```bash
cd images
bunzip2 SMD0.IMG.bz2
cp SMD0.IMG ..
cd ..
```

Boot with 

```bash
build/bin/nd100x --boot=smd
```

Or - if you want to boot from floppy

```bash
build/bin/nd100x --boot=floppy
```

## Login

Push ESC to activate the login after boot

log in with user "SYSTEM", no password.

## Commands 

* HELP (list all commands)
* WHO
* LIST-USERS
* LIST-FILES
* LIST-REENTRANT
* ...

## Installed reentrant programs
* NRL (RELOCATING LOADER LDR-1935I)
* BACKUP-SYSTEM-B
* DITAP
* FMAC
* MAC (Macro Assembler)
* QED
* NPL (Nord PL)
* GPM 
* FIL-EXTR
* BRF (BRF-EDITOR 81.07.02)
* LOOK-FILE
* PERFORM-E
* FTN (NORD-10/ND-100 FORTRAN COMPILER FTN-2090I)
* ASSEMBLER-500 (NORD-500 ASSEMBLER 2.13, 10 JUNE 1981.)
* PED


## Other programs

 * PLANC - ND-100 PLANC COMPILER - JANUARY 20, 1984   VERSION E
 * basic - BASIC COMPILER, JANUARY 85.

## Character Devices

SINTRAN supports the following character I/O devices:

| Logical Device | Device | Description |
|---------------|--------|-------------|
| 1 | Console Terminal | Main terminal (I/O 0300-0307) |
| 2 | Paper Tape Reader | Reads BPUN tape files (I/O 0400-0403) |
| 3 | Paper Tape Punch | Punches output to tape files (I/O 0410-0413) |
| 5 | Line Printer | CDC 9380 line printer (I/O 0430-0433) |

To print to the line printer:
```sintran
@LIST-FILE filename,5
```

To load a paper tape file, use the `--tape` CLI option:
```bash
build/bin/nd100x --boot=smd --tape=images/test.bpun
```

## Read files

Use command COPY TERM <filename>

copy term dump-1:mode


## Compile a C program


```sintran
@CC-100 HELLO:C
@NRL
IMAGE 100
PROG-FILE "HELLO"
LOAD CC-2HEADER
LOAD HELLO
LOAD CC-2BANK
LOAD CC-2TRAILER
EXIT
```

You can find a sampele program (CAT:C) and a compile script (CSESSION:MODE) in the user RONNY


## Shutdown

Log in as system, enter command "STOP-SYSTEM"
