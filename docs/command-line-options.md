# Command line options

Every option `nd100x` accepts, with examples. Moved out of `README.md` so the
README stays readable: it is reference material a user returns to, not something
anybody reads once to decide whether they want the emulator.

Generated from `--help`. If you change an option, change it here too.

The emulator supports the following command line options:

```bash
Usage: build/bin/nd100x [options]

Options:
  -b,      --boot=TYPE    Boot type (bp, bpun, tape, aout, prog, floppy,
                          smd[0-3], wd[0-1], scsi[0-6], cdc)
                          smd/wd/scsi take an optional boot unit digit,
                          e.g. --boot=smd1 or --boot=scsi2 (default: unit 0)
                          See "Boot types" below for bpun vs tape vs cdc.
  -i,      --image=FILE   Image file to load (bpun, tape, aout, prog, floppy;
                          --boot=cdc also demands one but never reads it)
           --smd0=FILE    SMD unit 0 disk image (default: SMD0.IMG)
           --smd1=FILE    SMD unit 1 disk image (default: SMD1.IMG)
           --smd2=FILE    SMD unit 2 disk image (default: SMD2.IMG)
           --smd3=FILE    SMD unit 3 disk image (default: SMD3.IMG)
           --wd0=FILE     Winchester unit 0 disk image (default: WD0.IMG)
           --wd1=FILE     Winchester unit 1 disk image (default: WD1.IMG)
                          Giving --wd0/--wd1 adds the ST506/8" Winchester
                          controller (cards 3041/3038). It answers IOX 500-507 -
                          the same block as the CDC system disc, so only one of
                          the two can be used. Also settable via the .ini
                          [controller.wd.0] section (disk0/disk1 keys; boot
                          with [boot] device = wd.0.0).
           --scsi0=[TYPE:]FILE  SCSI ID 0 target image (adds the ND-3201 controller)
           --scsi1=[TYPE:]FILE  SCSI ID 1 target image
           --scsi2=[TYPE:]FILE  SCSI ID 2 target image
           --scsi3=[TYPE:]FILE  SCSI ID 3 target image
           --scsi4=[TYPE:]FILE  SCSI ID 4 target image
           --scsi5=[TYPE:]FILE  SCSI ID 5 target image
           --scsi6=[TYPE:]FILE  SCSI ID 6 target image
                          TYPE is one of:
                            hdd     Micropolis 1375-ND hard disk (default)
                            tape    streamer tape           (not implemented yet)
                            cdrom   CD-ROM                  (not implemented yet)
                            floppy  SCSI floppy             (not implemented yet)
                          SCSI ID 7 is the controller itself and cannot be a target.
                          Example: --scsi0=hdd:SCSI-K.image
  -s,      --start=ADDR   Start address (default: 0)
  -a,      --disasm       Enable disassembly output
  -d,      --debugger     Enable DAP debugger
  -p PORT, --port=PORT    Set debugger port (default: 4711)
  -S,      --smd-debug    Enable SMD disk controller debug log (stderr)
           --scsi-debug   Enable SCSI disk controller debug log (stderr)
  -t,      --trace        Enable CPU execution trace to stderr
  -n N,    --max-instr=N  Stop after N instructions
  -B ADDR, --breakpoint=ADDR  Stop at address (octal/hex/decimal)
  -W SPEC, --watch=SPEC   Stop on memory access at full native speed (repeatable, max 32)
                          SPEC = [phys:]ADDR[:r|w|rw]  (default rw, virtual)
  -T ADDR, --text-start=ADDR  Text segment load address for a.out (default: 0)
  -v,      --verbose      Enable verbose output
  -P DIR,  --printdir=DIR  Printer output directory (default: ./prints/)
  -D DIR,  --tapedir=DIR   Paper tape output directory (default: ./tapes/)
  -e FILE, --tape=FILE     Paper tape reader input file (.bpun)
  -N[PORT],--telnet[=PORT] Enable telnet server (default port: 9000)
  -r TYPE, --printer=TYPE  Printer emulation: text (default), escp, laser
  -f FMT,  --printformat=FMT  Output format: txt (default), pdf
  -L CS,   --charset=CS    Local-console national 7-bit charset (telnet/TCP unaffected):
                          off (default), norwegian, swedish, german
  -H CFG,  --hdlc=CFG     Enable HDLC controller (up to 4x)
                          Server: --hdlc=N:PORT  (N=1-4)
                          Client: --hdlc=N:HOST:PORT
  -O,      --overlay-deposit Deposit data_click at phys word 1 for kernel boot-info
  -R[N],   --ring-dump[=N]  Dump last N instructions on halt/crash (default: 50, max: 512)
  -Z[MHZ], --throttle[=MHZ] Throttle CPU to real-time speed (default: 0.5275 MHz)
           --mms=N        MMU paging-system type: 1=MMS1 (NORD-10 / 4 page tables,
                          for NORD TSS), 2=MMS2 (16 page tables, default).
                          --mms1 / --mms2 are shorthands.
           --fpp=BITS     Installed floating point unit width: 32 or 48
                          (default: 48, the standard FPP). 32 selects the optional
                          single-precision FPP: FAD/FSB/FMU/FDV operate on the A,D
                          register pair with 2-word memory operands, and NLZ/DNZ
                          leave the T register untouched (which is how software
                          detects the installed unit). NOTE: LDF/STF always move
                          3 words (T,A,D) in both modes, matching the real
                          microcode (ND-110 RASK / ND-120 DELILAH have no mode
                          branch) - store/load 32-bit floats with STD/LDD, whose
                          2-word A,D layout matches the FAD..FDV memory operand.
                          Also settable via the .ini
                          '[machine] fpp = BITS' key; the CLI flag wins.
           --rtc=MODE     RTC time base: ticks or wall (default: wall).
                          ticks = one clock pulse per 10550 executed instructions
                          (deterministic; the emulated clock follows emulation
                          speed). wall = one pulse per 20 ms of host time, giving
                          a real-time 50 Hz clock regardless of emulation speed.
                          Also settable via the .ini '[machine] rtc = MODE' key;
                          the CLI flag wins.
           --pipe         Read the keyboard from stdin, unbuffered, for scripted
                          automation over pipes (see docs/pipe-automation.md).
                          Desktop builds only (not WASM / RISC-V).
  -h,      --help         Show this help message

Examples:
  build/bin/nd100x --boot=bpun --image=test.bpun
  build/bin/nd100x --boot=floppy --image=disk.img --start=0x1000 --disasm
  build/bin/nd100x --debugger
  build/bin/nd100x --hdlc=1:1362                  # HDLC 1 server on port 1362
  build/bin/nd100x --hdlc=1:192.168.1.10:1362     # HDLC 1 client
  build/bin/nd100x --boot=smd --smd0=myboot.img --smd1=data.img
  build/bin/nd100x --boot=smd1                     # Boot from SMD unit 1
  build/bin/nd100x --boot=scsi0 --scsi0=hdd:SCSI-K.image  # Boot from SCSI ID 0
  build/bin/nd100x --boot=floppy --wd0=WD0.IMG     # Floppy boot with Winchester attached
  build/bin/nd100x --boot=wd --wd0=WD0.IMG         # Boot from Winchester unit 0
  build/bin/nd100x --hdlc=1:5000 --hdlc=2:5001    # Two HDLC devices
  build/bin/nd100x --boot=smd --telnet=9000        # SINTRAN with telnet server
  build/bin/nd100x --boot=smd --throttle           # Real-time CPU speed
  build/bin/nd100x --boot=smd --charset=norwegian  # Norwegian 7-bit local console
```
