//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-rt-names.js - RT program name resolution
//
// RT program names are NOT stored as strings in memory (an RT
// description carries no name field). Names exist only in the linker
// symbol tables (SYMBOL-2-LIST.SYMB.TXT), which are shipped verbatim
// under data/symbols/{K03,L07,M06}/ and parsed by sintran-symbols.js
// into an address -> name map, selected by the version letter read
// from SINVER0 in memory.
//
// Resolution: RT slot n lives at address RTSTA + n*22; the program
// name is the symbol whose value equals that address. This covers
// system RTs (DUMMY, STSIN, ...) and background/batch slots
// (BAK01..BAKnn, BCH01..BCHnn) alike - all are in the symbol table.
//
// Descriptions are keyed by resolved NAME (not slot number, which
// differs between versions), with pattern rules for numbered families.

(function() {
  'use strict';

  // Entry size in words (22 decimal = 0o26 = 5RTSI)
  var RT_ENTRY_SIZE = 22;

  // =========================================================
  // Descriptions keyed by symbol name
  // Cross-checked against system segment names where possible
  // (e.g. DIMWD <-> S3IDMWD disk mirroring watchdog,
  //  NKSER/NKNAM <-> S3SNKSE/S3SNKNA NUCLEUS servers,
  //  ERSWD <-> S3SERWD ERS watchdog, PROMA <-> S3SPPRMA).
  // =========================================================
  var DESCRIPTIONS = {
    'DUMMY': 'Idle loop program. Runs when no other RT program is ready.',
    'DUMM2': 'Second dummy/placeholder RT. Separator between system RT slots and spool/extended slots.',
    'STSIN': 'System initialization. Performs post-boot terminal setup, file system mounting, and device configuration.',
    'RTERR': 'RT error handler. Catches runtime errors including illegal instructions and memory protection violations.',
    '1SWAP': 'Primary swapper (swap-out). Swaps program segments between physical memory and disk.',
    'TIMRT': 'Timer RT program. Manages real-time clock events and wakes up RT programs whose timer has expired.',
    'RTDIL': 'RT DIL dispatch handler. Manages Device Independent Language subsystem calls for high-level I/O.',
    'DIMWD': 'Disk mirroring watchdog. Monitors mirrored disk pairs (matches segments S3IDMWD/S3SDMWD).',
    'BPTMP': 'Breakpoint/temporary RT. Handles software breakpoints for the debugger and single-step execution.',
    'RTSLI': 'Timeslice scheduler. Implements preemptive multitasking by periodically checking program priorities.',
    'ACCRT': 'Accounting RT. Tracks CPU time usage, I/O statistics, and resource consumption per user/program.',
    'TERMP': 'Terminal program handler. Core terminal I/O driver for all physical and virtual terminals.',
    '5SWAP': 'Secondary swapper (swap-in). Loads program segments from disk back into physical memory.',
    'RTRFA': 'Record File Access. Handles record-oriented file I/O for sequential and indexed file access.',
    'COSPO': 'Console spool program. Special spool handler for the system console (terminal 0).',
    'TADAD': 'TAD adapter. Terminal access multiplexing for remote terminals via modem or network.',
    'XROUT': 'XMSG routing. Routes inter-process messages, possibly across network nodes.',
    'XTRAC': 'XMSG trace. Diagnostic for tracing inter-process message flow.',
    'XMFID': 'XMSG File ID handler. Manages file identification across message boundaries.',
    'NKSER': 'NUCLEUS server (matches segments S3SNKSE/S3INKSE).',
    'NKNAM': 'NUCLEUS name server (matches segments S3SNKNA/S3INKNA).',
    'ERSWD': 'ERS watchdog (matches segments S3SERWD/S3IERWD).',
    'PROMA': 'Processor Manager server (matches segments S3SPPRMA/S3IPRMA).',
    'EVMES': 'Event Message handler. Handles asynchronous event notification between processes.',
    'BOPCO': 'BOPCOM server. Manages HDLC/BOP communication protocol for synchronous serial links.',
    'MTSER': 'Magnetic Tape server. Sequential access to magnetic tape drives.',
    'RTREC': 'RT Recovery. Crash recovery and consistency checking for RT program state.',
    'RTBES': 'Background Execution System handler. Manages batch and background program scheduling.'
  };

  // Pattern rules for numbered program families
  var PATTERN_DESCRIPTIONS = [
    { re: /^BAK\d+$/,  desc: 'Background user process. One per user terminal, timesliced execution.' },
    { re: /^BK\d+$/,   desc: 'Background user process. One per user terminal, timesliced execution.' },
    { re: /^BCH\d+$/,  desc: 'Batch job slot. Non-interactive batch processing.' },
    { re: /^SPRT?\d+$/, desc: 'Spool terminal handler. Manages terminal I/O and print spooling.' },
    { re: /^RWRT?\d+$/, desc: 'Read/Write RT. Disk I/O worker for concurrent read/write operations.' },
    { re: /^RWR\d+$/,  desc: 'Extended Read/Write RT. Additional disk I/O worker.' },
    { re: /^WRT\d+$/,  desc: 'Extended Read/Write RT. Additional disk I/O worker.' },
    { re: /^FDRT\d+$/, desc: 'Floppy disk I/O worker.' },
    { re: /^UDR\d+$/,  desc: 'User Device Driver slot. Reserved for user-installable device drivers.' }
  ];

  // =========================================================
  // Name resolution
  // =========================================================

  // Compute the RT-description address for a slot number, using the
  // RT table base discovered from RTSTA in memory. Returns -1 if the
  // table has not been discovered yet.
  function rtSlotAddress(rtNum) {
    var sym = window.sintranSymbols;
    if (!sym) return -1;
    var rtInfo = sym.discoverRtTableSync();
    if (rtInfo.base === 0) return -1;
    return rtInfo.base + rtNum * RT_ENTRY_SIZE;
  }

  /**
   * Resolve an RT program name from its slot number.
   * The slot address (RTSTA + n*22) is reverse-looked-up in the
   * detected version's linker symbol table.
   *
   * @param {number} rtNum - RT slot index (0-based)
   * @returns {string} Program name (e.g. "DUMMY", "BAK01") or "RT #N" if unknown
   */
  function resolveProcessName(rtNum) {
    var rtAddr = rtSlotAddress(rtNum);
    if (rtAddr >= 0 && window.sintranSymbols.lookupSymbolName) {
      var name = window.sintranSymbols.lookupSymbolName(rtAddr);
      if (name) return name;
    }
    return 'RT #' + rtNum;
  }

  function resolveProcessDescription(rtNum) {
    var name = resolveProcessName(rtNum);
    if (DESCRIPTIONS[name]) return DESCRIPTIONS[name];
    for (var i = 0; i < PATTERN_DESCRIPTIONS.length; i++) {
      if (PATTERN_DESCRIPTIONS[i].re.test(name)) return PATTERN_DESCRIPTIONS[i].desc;
    }
    return null;
  }

  // =========================================================
  // Export
  // =========================================================
  window.resolveProcessName = resolveProcessName;
  window.resolveProcessDescription = resolveProcessDescription;
  window.sintranRtNames = {
    DESCRIPTIONS: DESCRIPTIONS,
    resolveProcessName: resolveProcessName,
    resolveProcessDescription: resolveProcessDescription
  };
})();
