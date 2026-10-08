/*
 * disk-types.js - single source of truth for drive/disk types in the Glass UI.
 *
 * The numeric driveType values MUST match the C DRIVE_TYPE enum in
 * src/machine/machine_types.h and the gateway wire protocol (driveType byte):
 *   0 = SMD, 1 = floppy, 2 = SCSI, 3 = Winchester.
 *
 * Every module that needs a drive-type name, unit count, block size, or the
 * name<->number mapping imports it from here instead of hard-coding a literal
 * like ['smd','floppy']. Adding a new type is a one-line change in this file.
 *
 * Winchester used to be a reserved stub here - in the tables, with a disabled
 * tab and units: 0 - because the WASM build had no way to mount one. It has one
 * now (MountWinchesterFromOPFS and friends), so it is a real type with 2 units,
 * matching the wd descriptor's disk_slots in src/machine/machine_config.c and
 * the hardware (disk system 1 carries the unit in one bit of the control word).
 *
 * This file is about CONTROLLER drive types only. The HDD Disk Manager's
 * library also tags images with types that are not controller units and so
 * are NOT listed here: 'nd500' (an NDIX root disc, chosen in Machine Setup)
 * and 'cdc' (the NORD TSS CDC 9427 cartridge disc, the emulator's
 * [runtime] cdc = FILE device). Those library-only tags, with their labels,
 * live in EXTRA_DISK_TYPE_LABELS in js/smd-manager.js.
 */
(function (global) {
    'use strict';

    // driveType number -> lowercase name. Index is the driveType value.
    var DRIVE_TYPE_NAMES = ['smd', 'floppy', 'scsi', 'winchester'];

    // name -> driveType number.
    var DRIVE_TYPE = {
        smd: 0,
        floppy: 1,
        scsi: 2,
        winchester: 3
    };

    // Mountable unit count per type. SCSI targets are IDs 0-6 (ID 7 is the
    // controller). Winchester is a stub with no units yet.
    var DRIVE_UNIT_COUNT = {
        smd: 4,
        floppy: 3,
        scsi: 7,
        winchester: 2
    };

    // Default block/sector size in bytes per type (floppy is 512, discs 1024).
    // Mirrors the block_size math in src/machine/machine.c.
    var DRIVE_BLOCK_SIZE = {
        smd: 1024,
        floppy: 512,
        scsi: 1024,
        winchester: 1024
    };

    // Human-readable label for UI (tabs, titles).
    var DRIVE_TYPE_LABEL = {
        smd: 'SMD',
        floppy: 'Floppy',
        scsi: 'SCSI',
        winchester: 'Winchester'
    };

    // Types with a working mount path. All four now have the same five ways in
    // - OPFS, buffer, gateway, remount, unmount - so this is every type the
    // controller registry knows. Floppy still has its own separate manager
    // window; being listed here is about the mount path existing, not about
    // which window drives it.
    var DRIVE_TYPES_IMPLEMENTED = ['smd', 'scsi', 'winchester', 'floppy'];

    function driveTypeNumber(name) {
        return DRIVE_TYPE[name];
    }

    function driveTypeName(num) {
        return DRIVE_TYPE_NAMES[num];
    }

    function isImplemented(name) {
        return DRIVE_TYPES_IMPLEMENTED.indexOf(name) !== -1;
    }

    var api = {
        DRIVE_TYPE_NAMES: DRIVE_TYPE_NAMES,
        DRIVE_TYPE: DRIVE_TYPE,
        DRIVE_UNIT_COUNT: DRIVE_UNIT_COUNT,
        DRIVE_BLOCK_SIZE: DRIVE_BLOCK_SIZE,
        DRIVE_TYPE_LABEL: DRIVE_TYPE_LABEL,
        DRIVE_TYPES_IMPLEMENTED: DRIVE_TYPES_IMPLEMENTED,
        driveTypeNumber: driveTypeNumber,
        driveTypeName: driveTypeName,
        isImplemented: isImplemented
    };

    // Expose on window (Glass UI modules) and support importScripts in workers
    // (disk-io-worker.js, emu-worker.js) where `self` is the global.
    global.diskTypes = api;

})(typeof self !== 'undefined' ? self : this);
