//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-dev-names.js - Device address-to-name lookup tables
// Source: SINTRAN III NPL symbol tables (SYMBOL-2-LIST.SYMB.TXT)
// Verified against: K03, L07, M06 symbol tables
//
// Device names are NOT stored as strings in memory.
// Names exist only in the linker symbol tables. This module provides
// the reverse lookup: I/O Datafield address -> symbol name.
//
// To add a new SINTRAN version (e.g. N):
//   1. Add terminal base address to DT01R_BASE
//   2. Add terminal number sequence to TERM_NUMS
//   3. Add block device base and count to BD01R_BASE / BD_COUNT
//   4. Add an IRREGULAR[N] table with all non-regular device addresses
//   5. The rest (computed terminals, BDs, lookup functions) works automatically.

(function() {
  'use strict';

  var STEP = 11;  // 13 octal = 11 decimal words between R halves

  // =========================================================
  // Terminal numbering sequences (gap at 02-04 in L/M, 53-64 in all)
  // Order matches symbol table layout (contiguous in memory)
  // =========================================================
  var TERM_NUMS = {
    K: [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
        21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,52,
        65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,
        81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,
        100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,
        115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,
        130,131,132,133,134,135,136,137,138,139,140,141,142,143,144],
    L: [1,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
        21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,52,
        65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,
        81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,
        100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,
        115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,
        130,131,132,133,134,135,136,137,138,139,140],
    M: [1,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
        21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,52,
        65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,
        81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,
        100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,
        115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,
        130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,
        145,146,147,148,149]
  };

  // Block device numbering (always contiguous BD01..BDnn)
  var BD_COUNT = { K: 30, L: 50, M: 70 };

  // DT01R base addresses (octal) per version
  var DT01R_BASE = { K: 0o21732, L: 0o53607, M: 0o60023 };

  // BD01R base addresses (octal) per version
  var BD01R_BASE = { K: 0o27564, L: 0o61207, M: 0o67537 };

  // =========================================================
  // Irregular device addresses per version (hardcoded from symbol tables)
  //
  // Each entry: { name, cat, desc }
  //   name: Symbol name from SYMBOL-2-LIST
  //   cat:  Device category (used for tab grouping and type override)
  //   desc: Human-readable description (shown as tooltip)
  //
  // To add version N: create IRREGULAR.N = { ... } with same format.
  // Only include actual I/O datafields - NOT boundary markers (9EDFD etc.)
  // or boot structure tables (SCBDF, SCODF etc.).
  // =========================================================
  var IRREGULAR = {
    K: {
      // --- Disk controllers (SMD) ---
      0o13007: { name:'D1DF0', cat:'Disk', desc:'SMD disk controller 1 unit 0' },
      0o13020: { name:'D1DF1', cat:'Disk', desc:'SMD disk controller 1 unit 1' },
      0o13031: { name:'D1DF2', cat:'Disk', desc:'SMD disk controller 1 unit 2' },
      0o13042: { name:'D1DF3', cat:'Disk', desc:'SMD disk controller 1 unit 3' },
      0o13053: { name:'D2DF0', cat:'Disk', desc:'SMD disk controller 2 unit 0' },
      0o13064: { name:'D2DF1', cat:'Disk', desc:'SMD disk controller 2 unit 1' },
      0o13075: { name:'D2DF2', cat:'Disk', desc:'SMD disk controller 2 unit 2' },
      0o13106: { name:'D2DF3', cat:'Disk', desc:'SMD disk controller 2 unit 3' },
      // --- SCSI controllers ---
      0o31461: { name:'SCSI1', cat:'SCSI', desc:'SCSI controller 1' },
      // --- SCSI disk ---
      0o31506: { name:'SCDDB', cat:'SCSI', desc:'SCSI device database start' },
      0o31600: { name:'SCDI1', cat:'SCSI', desc:'SCSI disk input 1' },
      0o32160: { name:'SCDI2', cat:'SCSI', desc:'SCSI disk input 2' },
      0o32540: { name:'SCDI3', cat:'SCSI', desc:'SCSI disk input 3' },
      0o33120: { name:'SCDI4', cat:'SCSI', desc:'SCSI disk input 4' },
      // --- SCSI status ---
      0o33526: { name:'SCST1', cat:'SCSI', desc:'SCSI status controller 1' },
      0o33557: { name:'SS1I0', cat:'SCSI', desc:'SCSI status 1 input unit 0' },
      0o33621: { name:'SS1O0', cat:'SCSI', desc:'SCSI status 1 output unit 0' },
      0o33662: { name:'S1U0R', cat:'SCSI', desc:'SCSI controller 1 unit 0 read' },
      // --- Magnetic tape (primary) ---
      0o15310: { name:'MTDI1', cat:'Tape', desc:'Magnetic tape drive 1 input' },
      0o15352: { name:'MTDO1', cat:'Tape', desc:'Magnetic tape drive 1 output' },
      0o15423: { name:'MTDI2', cat:'Tape', desc:'Magnetic tape drive 2 input' },
      0o15465: { name:'MTDO2', cat:'Tape', desc:'Magnetic tape drive 2 output' },
      0o15536: { name:'MTDI3', cat:'Tape', desc:'Magnetic tape drive 3 input' },
      0o15600: { name:'MTDO3', cat:'Tape', desc:'Magnetic tape drive 3 output' },
      0o15651: { name:'MTDI4', cat:'Tape', desc:'Magnetic tape drive 4 input' },
      0o15713: { name:'MTDO4', cat:'Tape', desc:'Magnetic tape drive 4 output' },
      // --- Magnetic tape (secondary) ---
      0o15764: { name:'M2DI1', cat:'Tape', desc:'Magnetic tape 2nd drive 1 input' },
      0o16026: { name:'M2DO1', cat:'Tape', desc:'Magnetic tape 2nd drive 1 output' },
      0o16077: { name:'M2DI2', cat:'Tape', desc:'Magnetic tape 2nd drive 2 input' },
      0o16142: { name:'M2DO2', cat:'Tape', desc:'Magnetic tape 2nd drive 2 output' },
      0o16212: { name:'M2DI3', cat:'Tape', desc:'Magnetic tape 2nd drive 3 input' },
      0o16254: { name:'M2DO3', cat:'Tape', desc:'Magnetic tape 2nd drive 3 output' },
      0o16325: { name:'M2DI4', cat:'Tape', desc:'Magnetic tape 2nd drive 4 input' },
      0o16370: { name:'M2DO4', cat:'Tape', desc:'Magnetic tape 2nd drive 4 output' },
      // --- Vector/Event ---
      0o16507: { name:'VEFIE', cat:'Vector', desc:'Vector event FIFO input' },
      0o16541: { name:'VEDO1', cat:'Vector', desc:'Vector event device output 1' },
      0o16660: { name:'VE2FI', cat:'Vector', desc:'Vector event 2 FIFO input' },
      0o16712: { name:'VEDO2', cat:'Vector', desc:'Vector event device output 2' },
      // --- Floppy controllers ---
      0o17346: { name:'FDID1', cat:'Floppy', desc:'Floppy disk controller 1' },
      0o20320: { name:'FDID2', cat:'Floppy', desc:'Floppy disk controller 2' },
      // --- Floppy units ---
      0o17377: { name:'F1U0I', cat:'Floppy', desc:'Floppy 1 unit 0 input' },
      0o17442: { name:'F1U0O', cat:'Floppy', desc:'Floppy 1 unit 0 output' },
      0o17507: { name:'F1U1I', cat:'Floppy', desc:'Floppy 1 unit 1 input' },
      0o17552: { name:'F1U1O', cat:'Floppy', desc:'Floppy 1 unit 1 output' },
      0o17617: { name:'F1U2I', cat:'Floppy', desc:'Floppy 1 unit 2 input' },
      0o17662: { name:'F1U2O', cat:'Floppy', desc:'Floppy 1 unit 2 output' },
      0o20351: { name:'F2U0I', cat:'Floppy', desc:'Floppy 2 unit 0 input' },
      0o20414: { name:'F2U0O', cat:'Floppy', desc:'Floppy 2 unit 0 output' },
      0o20461: { name:'F2U1I', cat:'Floppy', desc:'Floppy 2 unit 1 input' },
      0o20524: { name:'F2U1O', cat:'Floppy', desc:'Floppy 2 unit 1 output' },
      0o20571: { name:'F2U2I', cat:'Floppy', desc:'Floppy 2 unit 2 input' },
      0o20634: { name:'F2U2O', cat:'Floppy', desc:'Floppy 2 unit 2 output' },
      // --- ND-500 ---
      0o20734: { name:'N500D', cat:'ND-500', desc:'ND-500 domain controller' },
      0o21167: { name:'S5CPU', cat:'ND-500', desc:'ND-500 CPU 1' },
      // --- HDLC ---
      0o21516: { name:'HDMI1', cat:'HDLC', desc:'HDLC monitor input 1' },
      0o21607: { name:'HDMO1', cat:'HDLC', desc:'HDLC monitor output 1' },
      0o21635: { name:'HDFI1', cat:'HDLC', desc:'HDLC file input 1' },
      0o21663: { name:'HDFO1', cat:'HDLC', desc:'HDLC file output 1' },
      // --- Multi-Net ---
      0o104020:{ name:'MNDF0', cat:'Multi-Net', desc:'Multi-Net node 0' },
      0o104034:{ name:'MNNA0', cat:'Multi-Net', desc:'Multi-Net node 0 name' },
      0o104047:{ name:'MNID0', cat:'Multi-Net', desc:'Multi-Net node 0 input' },
      0o104107:{ name:'MNOD0', cat:'Multi-Net', desc:'Multi-Net node 0 output' },
      0o104155:{ name:'MNDF1', cat:'Multi-Net', desc:'Multi-Net node 1' },
      0o104171:{ name:'MNNA1', cat:'Multi-Net', desc:'Multi-Net node 1 name' },
      0o104204:{ name:'MNID1', cat:'Multi-Net', desc:'Multi-Net node 1 input' },
      0o104244:{ name:'MNOD1', cat:'Multi-Net', desc:'Multi-Net node 1 output' },
      // --- CDF channels ---
      0o34325: { name:'CDF01', cat:'CDF', desc:'CDF channel 1' },
      0o34340: { name:'CDF02', cat:'CDF', desc:'CDF channel 2' },
      0o34353: { name:'CDF03', cat:'CDF', desc:'CDF channel 3' },
      0o34366: { name:'CDF04', cat:'CDF', desc:'CDF channel 4' },
      0o34401: { name:'CDF05', cat:'CDF', desc:'CDF channel 5' },
      0o34414: { name:'CDF06', cat:'CDF', desc:'CDF channel 6' },
      0o34427: { name:'CDF07', cat:'CDF', desc:'CDF channel 7' },
      0o34442: { name:'CDF08', cat:'CDF', desc:'CDF channel 8' },
      0o34455: { name:'CDF09', cat:'CDF', desc:'CDF channel 9' },
      0o34470: { name:'CDF10', cat:'CDF', desc:'CDF channel 10' },
      // --- Ethernet terminals ---
      0o47307: { name:'ETRN1', cat:'Terminal', desc:'Ethernet terminal 1' },
      0o47424: { name:'ETRN2', cat:'Terminal', desc:'Ethernet terminal 2' }
    },

    L: {
      // --- Disk controllers (SMD) D1-D4 ---
      0o31631: { name:'D1DF0', cat:'Disk', desc:'SMD disk controller 1 unit 0' },
      0o31644: { name:'D1DF1', cat:'Disk', desc:'SMD disk controller 1 unit 1' },
      0o31657: { name:'D1DF2', cat:'Disk', desc:'SMD disk controller 1 unit 2' },
      0o31672: { name:'D1DF3', cat:'Disk', desc:'SMD disk controller 1 unit 3' },
      0o32075: { name:'D2DF0', cat:'Disk', desc:'SMD disk controller 2 unit 0' },
      0o32110: { name:'D2DF1', cat:'Disk', desc:'SMD disk controller 2 unit 1' },
      0o32123: { name:'D2DF2', cat:'Disk', desc:'SMD disk controller 2 unit 2' },
      0o32136: { name:'D2DF3', cat:'Disk', desc:'SMD disk controller 2 unit 3' },
      0o32341: { name:'D3DF0', cat:'Disk', desc:'SMD disk controller 3 unit 0' },
      0o32354: { name:'D3DF1', cat:'Disk', desc:'SMD disk controller 3 unit 1' },
      0o32367: { name:'D3DF2', cat:'Disk', desc:'SMD disk controller 3 unit 2' },
      0o32402: { name:'D3DF3', cat:'Disk', desc:'SMD disk controller 3 unit 3' },
      0o32605: { name:'D4DF0', cat:'Disk', desc:'SMD disk controller 4 unit 0' },
      0o32620: { name:'D4DF1', cat:'Disk', desc:'SMD disk controller 4 unit 1' },
      0o32633: { name:'D4DF2', cat:'Disk', desc:'SMD disk controller 4 unit 2' },
      0o32646: { name:'D4DF3', cat:'Disk', desc:'SMD disk controller 4 unit 3' },
      // --- Winchester disk controllers ---
      0o33051: { name:'W1DF0', cat:'Disk', desc:'Winchester disk controller 1 unit 0' },
      0o33064: { name:'W1DF1', cat:'Disk', desc:'Winchester disk controller 1 unit 1' },
      0o33267: { name:'W2DF0', cat:'Disk', desc:'Winchester disk controller 2 unit 0' },
      0o33302: { name:'W2DF1', cat:'Disk', desc:'Winchester disk controller 2 unit 1' },
      // --- SCSI device database ---
      0o36350: { name:'SCDDB', cat:'SCSI', desc:'SCSI device database start' },
      // --- SCSI disk ---
      0o36442: { name:'SCDI1', cat:'SCSI', desc:'SCSI disk input 1' },
      0o36645: { name:'SCDI2', cat:'SCSI', desc:'SCSI disk input 2' },
      0o37050: { name:'SCDI3', cat:'SCSI', desc:'SCSI disk input 3' },
      0o37253: { name:'SCDI4', cat:'SCSI', desc:'SCSI disk input 4' },
      0o37456: { name:'SCDI5', cat:'SCSI', desc:'SCSI disk input 5' },
      0o37661: { name:'SCDI6', cat:'SCSI', desc:'SCSI disk input 6' },
      0o40064: { name:'SCDI7', cat:'SCSI', desc:'SCSI disk input 7' },
      0o40267: { name:'SCDI8', cat:'SCSI', desc:'SCSI disk input 8' },
      0o40472: { name:'SCOD1', cat:'SCSI', desc:'SCSI disk output 1' },
      0o40675: { name:'SCOD2', cat:'SCSI', desc:'SCSI disk output 2' },
      // --- Domain entries (octal numbering: 01-07, 10-17, 20) ---
      0o41064: { name:'DOMDF', cat:'Domain', desc:'Domain datafield base' },
      0o41340: { name:'DOM01', cat:'Domain', desc:'Domain 1' },
      0o41377: { name:'DOM02', cat:'Domain', desc:'Domain 2' },
      0o41436: { name:'DOM03', cat:'Domain', desc:'Domain 3' },
      0o41475: { name:'DOM04', cat:'Domain', desc:'Domain 4' },
      0o41534: { name:'DOM05', cat:'Domain', desc:'Domain 5' },
      0o41573: { name:'DOM06', cat:'Domain', desc:'Domain 6' },
      0o41632: { name:'DOM07', cat:'Domain', desc:'Domain 7' },
      0o41671: { name:'DOM10', cat:'Domain', desc:'Domain 8' },
      0o41730: { name:'DOM11', cat:'Domain', desc:'Domain 9' },
      0o41767: { name:'DOM12', cat:'Domain', desc:'Domain 10' },
      0o42026: { name:'DOM13', cat:'Domain', desc:'Domain 11' },
      0o42065: { name:'DOM14', cat:'Domain', desc:'Domain 12' },
      0o42124: { name:'DOM15', cat:'Domain', desc:'Domain 13' },
      0o42163: { name:'DOM16', cat:'Domain', desc:'Domain 14' },
      0o42222: { name:'DOM17', cat:'Domain', desc:'Domain 15' },
      0o42261: { name:'DOM20', cat:'Domain', desc:'Domain 16' },
      // --- Magnetic tape (primary) ---
      0o45275: { name:'MTDI1', cat:'Tape', desc:'Magnetic tape drive 1 input' },
      0o45337: { name:'MTDO1', cat:'Tape', desc:'Magnetic tape drive 1 output' },
      0o45410: { name:'MTDI2', cat:'Tape', desc:'Magnetic tape drive 2 input' },
      0o45452: { name:'MTDO2', cat:'Tape', desc:'Magnetic tape drive 2 output' },
      0o45523: { name:'MTDI3', cat:'Tape', desc:'Magnetic tape drive 3 input' },
      0o45565: { name:'MTDO3', cat:'Tape', desc:'Magnetic tape drive 3 output' },
      0o45636: { name:'MTDI4', cat:'Tape', desc:'Magnetic tape drive 4 input' },
      0o45700: { name:'MTDO4', cat:'Tape', desc:'Magnetic tape drive 4 output' },
      // --- Magnetic tape (secondary) ---
      0o45751: { name:'M2DI1', cat:'Tape', desc:'Magnetic tape 2nd drive 1 input' },
      0o46013: { name:'M2DO1', cat:'Tape', desc:'Magnetic tape 2nd drive 1 output' },
      0o46064: { name:'M2DI2', cat:'Tape', desc:'Magnetic tape 2nd drive 2 input' },
      0o46127: { name:'M2DO2', cat:'Tape', desc:'Magnetic tape 2nd drive 2 output' },
      0o46177: { name:'M2DI3', cat:'Tape', desc:'Magnetic tape 2nd drive 3 input' },
      0o46241: { name:'M2DO3', cat:'Tape', desc:'Magnetic tape 2nd drive 3 output' },
      0o46312: { name:'M2DI4', cat:'Tape', desc:'Magnetic tape 2nd drive 4 input' },
      0o46355: { name:'M2DO4', cat:'Tape', desc:'Magnetic tape 2nd drive 4 output' },
      // --- SCSI controllers ---
      0o46530: { name:'SCSI1', cat:'SCSI', desc:'SCSI controller 1' },
      0o46661: { name:'SCSI2', cat:'SCSI', desc:'SCSI controller 2' },
      // --- SCSI status ---
      0o46754: { name:'SCST1', cat:'SCSI', desc:'SCSI status controller 1' },
      0o47005: { name:'SS1I0', cat:'SCSI', desc:'SCSI status 1 input unit 0' },
      0o47047: { name:'SS1O0', cat:'SCSI', desc:'SCSI status 1 output unit 0' },
      0o47110: { name:'S1U0R', cat:'SCSI', desc:'SCSI controller 1 unit 0 read' },
      0o47236: { name:'SCST2', cat:'SCSI', desc:'SCSI status controller 2' },
      0o47267: { name:'SS2I0', cat:'SCSI', desc:'SCSI status 2 input unit 0' },
      0o47331: { name:'SS2O0', cat:'SCSI', desc:'SCSI status 2 output unit 0' },
      0o47372: { name:'S2U0R', cat:'SCSI', desc:'SCSI controller 2 unit 0 read' },
      // --- SCSI tape ---
      0o47514: { name:'SCMT1', cat:'Tape', desc:'SCSI magnetic tape 1' },
      0o47701: { name:'SCMT2', cat:'Tape', desc:'SCSI magnetic tape 2' },
      // --- Vector/Event ---
      0o50172: { name:'VEFIE', cat:'Vector', desc:'Vector event FIFO input' },
      0o50224: { name:'VEDO1', cat:'Vector', desc:'Vector event device output 1' },
      0o50343: { name:'VE2FI', cat:'Vector', desc:'Vector event 2 FIFO input' },
      0o50375: { name:'VEDO2', cat:'Vector', desc:'Vector event device output 2' },
      // --- Floppy controllers ---
      0o50615: { name:'FDID1', cat:'Floppy', desc:'Floppy disk controller 1' },
      0o51353: { name:'FDID2', cat:'Floppy', desc:'Floppy disk controller 2' },
      // --- Floppy units ---
      0o50646: { name:'F1U0I', cat:'Floppy', desc:'Floppy 1 unit 0 input' },
      0o50711: { name:'F1U0O', cat:'Floppy', desc:'Floppy 1 unit 0 output' },
      0o50756: { name:'F1U1I', cat:'Floppy', desc:'Floppy 1 unit 1 input' },
      0o51021: { name:'F1U1O', cat:'Floppy', desc:'Floppy 1 unit 1 output' },
      0o51066: { name:'F1U2I', cat:'Floppy', desc:'Floppy 1 unit 2 input' },
      0o51131: { name:'F1U2O', cat:'Floppy', desc:'Floppy 1 unit 2 output' },
      0o51404: { name:'F2U0I', cat:'Floppy', desc:'Floppy 2 unit 0 input' },
      0o51447: { name:'F2U0O', cat:'Floppy', desc:'Floppy 2 unit 0 output' },
      0o51514: { name:'F2U1I', cat:'Floppy', desc:'Floppy 2 unit 1 input' },
      0o51557: { name:'F2U1O', cat:'Floppy', desc:'Floppy 2 unit 1 output' },
      0o51624: { name:'F2U2I', cat:'Floppy', desc:'Floppy 2 unit 2 input' },
      0o51667: { name:'F2U2O', cat:'Floppy', desc:'Floppy 2 unit 2 output' },
      // --- ND-500 ---
      0o51767: { name:'N500D', cat:'ND-500', desc:'ND-500 domain controller' },
      0o52222: { name:'S5CPU', cat:'ND-500', desc:'ND-500 CPU 1' },
      0o52270: { name:'5CPU2', cat:'ND-500', desc:'ND-500 CPU 2' },
      0o52336: { name:'5CPU3', cat:'ND-500', desc:'ND-500 CPU 3' },
      0o52404: { name:'5CPU4', cat:'ND-500', desc:'ND-500 CPU 4' },
      // --- HDLC ---
      0o52733: { name:'HDMI1', cat:'HDLC', desc:'HDLC monitor input 1' },
      0o53024: { name:'HDMO1', cat:'HDLC', desc:'HDLC monitor output 1' },
      0o53052: { name:'HDFI1', cat:'HDLC', desc:'HDLC file input 1' },
      0o53100: { name:'HDFO1', cat:'HDLC', desc:'HDLC file output 1' },
      // --- Multi-Net ---
      0o53151: { name:'MNDF0', cat:'Multi-Net', desc:'Multi-Net node 0' },
      0o53165: { name:'MNNA0', cat:'Multi-Net', desc:'Multi-Net node 0 name' },
      0o53200: { name:'MNID0', cat:'Multi-Net', desc:'Multi-Net node 0 input' },
      0o53240: { name:'MNOD0', cat:'Multi-Net', desc:'Multi-Net node 0 output' },
      0o53306: { name:'MNDF1', cat:'Multi-Net', desc:'Multi-Net node 1' },
      0o53322: { name:'MNNA1', cat:'Multi-Net', desc:'Multi-Net node 1 name' },
      0o53335: { name:'MNID1', cat:'Multi-Net', desc:'Multi-Net node 1 input' },
      0o53375: { name:'MNOD1', cat:'Multi-Net', desc:'Multi-Net node 1 output' },
      0o53443: { name:'MNDF2', cat:'Multi-Net', desc:'Multi-Net node 2' },
      0o53457: { name:'MNNA2', cat:'Multi-Net', desc:'Multi-Net node 2 name' },
      0o53472: { name:'MNID2', cat:'Multi-Net', desc:'Multi-Net node 2 input' },
      0o53532: { name:'MNOD2', cat:'Multi-Net', desc:'Multi-Net node 2 output' },
      // --- CDF channels ---
      0o64566: { name:'CDF01', cat:'CDF', desc:'CDF channel 1' },
      0o64601: { name:'CDF02', cat:'CDF', desc:'CDF channel 2' },
      0o64614: { name:'CDF03', cat:'CDF', desc:'CDF channel 3' },
      0o64627: { name:'CDF04', cat:'CDF', desc:'CDF channel 4' },
      0o64642: { name:'CDF05', cat:'CDF', desc:'CDF channel 5' },
      0o64655: { name:'CDF06', cat:'CDF', desc:'CDF channel 6' },
      0o64670: { name:'CDF07', cat:'CDF', desc:'CDF channel 7' },
      0o64703: { name:'CDF08', cat:'CDF', desc:'CDF channel 8' },
      0o64716: { name:'CDF09', cat:'CDF', desc:'CDF channel 9' },
      0o64731: { name:'CDF10', cat:'CDF', desc:'CDF channel 10' },
      0o64744: { name:'CDF11', cat:'CDF', desc:'CDF channel 11' },
      0o64757: { name:'CDF12', cat:'CDF', desc:'CDF channel 12' },
      0o64772: { name:'CDF13', cat:'CDF', desc:'CDF channel 13' },
      0o65005: { name:'CDF14', cat:'CDF', desc:'CDF channel 14' },
      0o65020: { name:'CDF15', cat:'CDF', desc:'CDF channel 15' },
      0o65033: { name:'CDF16', cat:'CDF', desc:'CDF channel 16' },
      // --- Ethernet terminals ---
      0o103356:{ name:'ETRN1', cat:'Terminal', desc:'Ethernet terminal 1' },
      0o103503:{ name:'ETRN2', cat:'Terminal', desc:'Ethernet terminal 2' },
      0o103630:{ name:'ETRN3', cat:'Terminal', desc:'Ethernet terminal 3' }
    },

    M: {
      // --- Disk controllers (SMD) D1-D2 ---
      0o36546: { name:'D1DF0', cat:'Disk', desc:'SMD disk controller 1 unit 0' },
      0o36561: { name:'D1DF1', cat:'Disk', desc:'SMD disk controller 1 unit 1' },
      0o36574: { name:'D1DF2', cat:'Disk', desc:'SMD disk controller 1 unit 2' },
      0o36607: { name:'D1DF3', cat:'Disk', desc:'SMD disk controller 1 unit 3' },
      0o37012: { name:'D2DF0', cat:'Disk', desc:'SMD disk controller 2 unit 0' },
      0o37025: { name:'D2DF1', cat:'Disk', desc:'SMD disk controller 2 unit 1' },
      0o37040: { name:'D2DF2', cat:'Disk', desc:'SMD disk controller 2 unit 2' },
      0o37053: { name:'D2DF3', cat:'Disk', desc:'SMD disk controller 2 unit 3' },
      // --- Winchester disk controllers ---
      0o37256: { name:'W1DF0', cat:'Disk', desc:'Winchester disk controller 1 unit 0' },
      0o37271: { name:'W1DF1', cat:'Disk', desc:'Winchester disk controller 1 unit 1' },
      // --- SCSI device database ---
      0o42766: { name:'SCDDB', cat:'SCSI', desc:'SCSI device database start' },
      // --- SCSI disk ---
      0o43060: { name:'SCDI1', cat:'SCSI', desc:'SCSI disk input 1' },
      0o43263: { name:'SCDI2', cat:'SCSI', desc:'SCSI disk input 2' },
      0o43466: { name:'SCOD1', cat:'SCSI', desc:'SCSI disk output 1' },
      0o43671: { name:'SCOD2', cat:'SCSI', desc:'SCSI disk output 2' },
      // --- Domain entries ---
      0o44060: { name:'DOMDF', cat:'Domain', desc:'Domain datafield base' },
      0o44334: { name:'DOM01', cat:'Domain', desc:'Domain 1' },
      0o44373: { name:'DOM02', cat:'Domain', desc:'Domain 2' },
      0o44432: { name:'DOM03', cat:'Domain', desc:'Domain 3' },
      0o44471: { name:'DOM04', cat:'Domain', desc:'Domain 4' },
      0o44530: { name:'DOM05', cat:'Domain', desc:'Domain 5' },
      0o44567: { name:'DOM06', cat:'Domain', desc:'Domain 6' },
      0o44626: { name:'DOM07', cat:'Domain', desc:'Domain 7' },
      0o44665: { name:'DOM10', cat:'Domain', desc:'Domain 8' },
      0o44724: { name:'DOM11', cat:'Domain', desc:'Domain 9' },
      0o44763: { name:'DOM12', cat:'Domain', desc:'Domain 10' },
      0o45022: { name:'DOM13', cat:'Domain', desc:'Domain 11' },
      0o45061: { name:'DOM14', cat:'Domain', desc:'Domain 12' },
      0o45120: { name:'DOM15', cat:'Domain', desc:'Domain 13' },
      0o45157: { name:'DOM16', cat:'Domain', desc:'Domain 14' },
      0o45216: { name:'DOM17', cat:'Domain', desc:'Domain 15' },
      0o45255: { name:'DOM20', cat:'Domain', desc:'Domain 16' },
      // --- Magnetic tape (primary) ---
      0o51731: { name:'MTDI1', cat:'Tape', desc:'Magnetic tape drive 1 input' },
      0o51773: { name:'MTDO1', cat:'Tape', desc:'Magnetic tape drive 1 output' },
      0o52044: { name:'MTDI2', cat:'Tape', desc:'Magnetic tape drive 2 input' },
      0o52106: { name:'MTDO2', cat:'Tape', desc:'Magnetic tape drive 2 output' },
      0o52157: { name:'MTDI3', cat:'Tape', desc:'Magnetic tape drive 3 input' },
      0o52221: { name:'MTDO3', cat:'Tape', desc:'Magnetic tape drive 3 output' },
      0o52272: { name:'MTDI4', cat:'Tape', desc:'Magnetic tape drive 4 input' },
      0o52334: { name:'MTDO4', cat:'Tape', desc:'Magnetic tape drive 4 output' },
      // --- Magnetic tape (secondary) ---
      0o52405: { name:'M2DI1', cat:'Tape', desc:'Magnetic tape 2nd drive 1 input' },
      0o52447: { name:'M2DO1', cat:'Tape', desc:'Magnetic tape 2nd drive 1 output' },
      0o52520: { name:'M2DI2', cat:'Tape', desc:'Magnetic tape 2nd drive 2 input' },
      0o52563: { name:'M2DO2', cat:'Tape', desc:'Magnetic tape 2nd drive 2 output' },
      0o52633: { name:'M2DI3', cat:'Tape', desc:'Magnetic tape 2nd drive 3 input' },
      0o52675: { name:'M2DO3', cat:'Tape', desc:'Magnetic tape 2nd drive 3 output' },
      0o52746: { name:'M2DI4', cat:'Tape', desc:'Magnetic tape 2nd drive 4 input' },
      0o53011: { name:'M2DO4', cat:'Tape', desc:'Magnetic tape 2nd drive 4 output' },
      // --- SCSI controllers ---
      0o53164: { name:'SCSI1', cat:'SCSI', desc:'SCSI controller 1' },
      // --- SCSI status ---
      0o53257: { name:'SCST1', cat:'SCSI', desc:'SCSI status controller 1' },
      0o53310: { name:'SS1I0', cat:'SCSI', desc:'SCSI status 1 input unit 0' },
      0o53352: { name:'SS1O0', cat:'SCSI', desc:'SCSI status 1 output unit 0' },
      0o53413: { name:'S1U0R', cat:'SCSI', desc:'SCSI controller 1 unit 0 read' },
      0o53541: { name:'SCST2', cat:'SCSI', desc:'SCSI status controller 2' },
      0o53572: { name:'SS2I0', cat:'SCSI', desc:'SCSI status 2 input unit 0' },
      0o53634: { name:'SS2O0', cat:'SCSI', desc:'SCSI status 2 output unit 0' },
      0o53675: { name:'S2U0R', cat:'SCSI', desc:'SCSI controller 2 unit 0 read' },
      // --- SCSI tape ---
      0o54017: { name:'SCMT1', cat:'Tape', desc:'SCSI magnetic tape 1' },
      0o54132: { name:'SCMT2', cat:'Tape', desc:'SCSI magnetic tape 2' },
      // --- Vector/Event ---
      0o54673: { name:'VEFIE', cat:'Vector', desc:'Vector event FIFO input' },
      0o54725: { name:'VEDO1', cat:'Vector', desc:'Vector event device output 1' },
      0o55044: { name:'VE2FI', cat:'Vector', desc:'Vector event 2 FIFO input' },
      0o55076: { name:'VEDO2', cat:'Vector', desc:'Vector event device output 2' },
      // --- Floppy controllers ---
      0o55316: { name:'FDID1', cat:'Floppy', desc:'Floppy disk controller 1' },
      0o56054: { name:'FDID2', cat:'Floppy', desc:'Floppy disk controller 2' },
      // --- Floppy units ---
      0o55347: { name:'F1U0I', cat:'Floppy', desc:'Floppy 1 unit 0 input' },
      0o55412: { name:'F1U0O', cat:'Floppy', desc:'Floppy 1 unit 0 output' },
      0o55457: { name:'F1U1I', cat:'Floppy', desc:'Floppy 1 unit 1 input' },
      0o55522: { name:'F1U1O', cat:'Floppy', desc:'Floppy 1 unit 1 output' },
      0o55567: { name:'F1U2I', cat:'Floppy', desc:'Floppy 1 unit 2 input' },
      0o55632: { name:'F1U2O', cat:'Floppy', desc:'Floppy 1 unit 2 output' },
      0o56105: { name:'F2U0I', cat:'Floppy', desc:'Floppy 2 unit 0 input' },
      0o56150: { name:'F2U0O', cat:'Floppy', desc:'Floppy 2 unit 0 output' },
      0o56215: { name:'F2U1I', cat:'Floppy', desc:'Floppy 2 unit 1 input' },
      0o56260: { name:'F2U1O', cat:'Floppy', desc:'Floppy 2 unit 1 output' },
      0o56325: { name:'F2U2I', cat:'Floppy', desc:'Floppy 2 unit 2 input' },
      0o56370: { name:'F2U2O', cat:'Floppy', desc:'Floppy 2 unit 2 output' },
      // --- ND-500 ---
      0o56470: { name:'N500D', cat:'ND-500', desc:'ND-500 domain controller' },
      0o56723: { name:'S5CPU', cat:'ND-500', desc:'ND-500 CPU 1' },
      0o56771: { name:'5CPU2', cat:'ND-500', desc:'ND-500 CPU 2' },
      0o57037: { name:'5CPU3', cat:'ND-500', desc:'ND-500 CPU 3' },
      0o57105: { name:'5CPU4', cat:'ND-500', desc:'ND-500 CPU 4' },
      // --- HDLC ---
      0o57441: { name:'HDMI1', cat:'HDLC', desc:'HDLC monitor input 1' },
      0o57532: { name:'HDMO1', cat:'HDLC', desc:'HDLC monitor output 1' },
      0o57560: { name:'HDFI1', cat:'HDLC', desc:'HDLC file input 1' },
      0o57606: { name:'HDFO1', cat:'HDLC', desc:'HDLC file output 1' },
      // --- Multi-Net ---
      0o57657: { name:'MNDF0', cat:'Multi-Net', desc:'Multi-Net node 0' },
      0o57673: { name:'MNNA0', cat:'Multi-Net', desc:'Multi-Net node 0 name' },
      0o57706: { name:'MNID0', cat:'Multi-Net', desc:'Multi-Net node 0 input' },
      0o57746: { name:'MNOD0', cat:'Multi-Net', desc:'Multi-Net node 0 output' },
      // --- CDF channels ---
      0o73262: { name:'CDF01', cat:'CDF', desc:'CDF channel 1' },
      0o73275: { name:'CDF02', cat:'CDF', desc:'CDF channel 2' },
      // --- Ethernet terminals ---
      0o104262:{ name:'ETRN1', cat:'Terminal', desc:'Ethernet terminal 1' },
      0o104407:{ name:'ETRN2', cat:'Terminal', desc:'Ethernet terminal 2' }
    }
  };

  // =========================================================
  // Build combined lookup maps (computed terminals/BDs + irregular)
  // =========================================================
  var versionMaps = {};  // { K: {addr: {name,cat,desc,pairAddr}}, L: ..., M: ... }

  function buildVersionMap(ver) {
    var map = {};

    // Generate terminal entries
    var dt01r = DT01R_BASE[ver];
    var nums = TERM_NUMS[ver];
    if (dt01r && nums) {
      for (var i = 0; i < nums.length; i++) {
        var num = nums[i];
        var rAddr = dt01r + i * STEP * 2;  // Each pair = 2 halves * STEP
        var wAddr = rAddr + STEP;
        var label = num < 100 ? ('DT' + padNum(num)) : ('T' + num);
        var desc = 'Terminal ' + num;
        map[rAddr] = { name: label + 'R', cat: 'Terminal', desc: desc + ' read half', pairAddr: wAddr };
        map[wAddr] = { name: label + 'W', cat: 'Terminal', desc: desc + ' write half', pairAddr: rAddr };
      }
    }

    // Generate block device entries
    var bd01r = BD01R_BASE[ver];
    var bdCount = BD_COUNT[ver];
    if (bd01r && bdCount) {
      for (var b = 0; b < bdCount; b++) {
        var bNum = b + 1;
        var brAddr = bd01r + b * STEP * 2;
        var bwAddr = brAddr + STEP;
        var bLabel = 'BD' + padNum(bNum);
        var bDesc = 'Block device ' + bNum;
        map[brAddr] = { name: bLabel + 'R', cat: 'Block Device', desc: bDesc + ' read half', pairAddr: bwAddr };
        map[bwAddr] = { name: bLabel + 'W', cat: 'Block Device', desc: bDesc + ' write half', pairAddr: brAddr };
      }
    }

    // Merge irregular devices
    var irreg = IRREGULAR[ver];
    if (irreg) {
      for (var addr in irreg) {
        if (irreg.hasOwnProperty(addr)) {
          var entry = irreg[addr];
          map[parseInt(addr, 10)] = { name: entry.name, cat: entry.cat, desc: entry.desc, pairAddr: null };
        }
      }
    }

    return map;
  }

  function padNum(n) {
    if (n < 10) return '0' + n;
    return '' + n;
  }

  function getVersionKey() {
    if (typeof sintranState === 'undefined' || !sintranState.versionLetter) return null;
    return sintranState.versionLetter.toUpperCase();
  }

  function getMap() {
    var ver = getVersionKey();
    if (!ver) return null;
    if (!versionMaps[ver]) {
      versionMaps[ver] = buildVersionMap(ver);
    }
    return versionMaps[ver];
  }

  // =========================================================
  // Public API
  // =========================================================

  function resolveDeviceName(addr) {
    var map = getMap();
    if (!map) return '';
    var entry = map[addr];
    return entry ? entry.name : '';
  }

  function resolveDeviceCategory(addr) {
    var map = getMap();
    if (!map) return '';
    var entry = map[addr];
    return entry ? entry.cat : '';
  }

  function getDeviceDescription(addr) {
    var map = getMap();
    if (!map) return null;
    var entry = map[addr];
    return entry ? entry.desc : null;
  }

  function getDevicePairAddr(addr) {
    var map = getMap();
    if (!map) return null;
    var entry = map[addr];
    return entry ? entry.pairAddr : null;
  }

  // Return all known device descriptors for current version
  // Each: { addr, name, cat, desc, pairAddr }
  function getAllKnownDevices() {
    var map = getMap();
    if (!map) return [];
    var result = [];
    for (var addr in map) {
      if (map.hasOwnProperty(addr)) {
        var e = map[addr];
        result.push({ addr: parseInt(addr, 10), name: e.name, cat: e.cat, desc: e.desc, pairAddr: e.pairAddr });
      }
    }
    result.sort(function(a, b) { return a.addr - b.addr; });
    return result;
  }

  // =========================================================
  // Category ordering for grouped/tabbed display
  // =========================================================
  var CATEGORY_ORDER = [
    'Disk', 'SCSI', 'Tape', 'Floppy', 'Domain', 'Vector',
    'HDLC', 'ND-500', 'Multi-Net', 'CDF',
    'Terminal', 'Block Device', 'Other'
  ];

  // =========================================================
  // Export
  // =========================================================
  window.resolveDeviceName = resolveDeviceName;
  window.resolveDeviceCategory = resolveDeviceCategory;
  window.getDeviceDescription = getDeviceDescription;
  window.getDevicePairAddr = getDevicePairAddr;
  window.sintranDevNames = {
    resolveDeviceName: resolveDeviceName,
    resolveDeviceCategory: resolveDeviceCategory,
    getDeviceDescription: getDeviceDescription,
    getDevicePairAddr: getDevicePairAddr,
    getAllKnownDevices: getAllKnownDevices,
    CATEGORY_ORDER: CATEGORY_ORDER
  };
})();
