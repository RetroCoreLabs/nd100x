//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// smd-image-source.js - Get the bytes of a mounted SMD image, in either
// storage mode. Shared by the NDFS Viewer and the Initial Commands window.
//
//   persistent mode (OPFS)      -> smdStorage.retrieveImage(uuid)
//   non-persistent mode (MEMFS) -> Module.FS.readFile('/SMDn.IMG')
//
// Both are local reads. Nothing is fetched over the gateway: disk I/O over
// the gateway is always block read/write, never a full image transfer.

(function() {
  'use strict';

  // Returns Promise<{bytes: Uint8Array, name: string, mode: string}>
  function getMountedImage(unit) {
    var u = unit || 0;

    // 1. Persistent mode: image lives in OPFS, unit->uuid via smdStorage
    if (typeof smdStorage !== 'undefined' && smdStorage.getUnitAssignment) {
      var uuid = smdStorage.getUnitAssignment(u);
      if (uuid) {
        var meta = smdStorage.getMetadata ? smdStorage.getMetadata(uuid) : null;
        return Promise.resolve(smdStorage.retrieveImage(uuid)).then(function(data) {
          if (!data || !data.byteLength) throw new Error('SMD unit ' + u + ': OPFS image is empty');
          return {
            bytes: new Uint8Array(data),
            name: (meta && meta.name) || ('SMD' + u + '.IMG'),
            mode: 'OPFS (persistent)'
          };
        });
      }
    }

    // 2. Non-persistent mode: image is in the Emscripten MEMFS
    try {
      if (typeof Module !== 'undefined' && Module.FS && Module.FS.readFile) {
        var bytes = Module.FS.readFile('/SMD' + u + '.IMG');
        if (bytes && bytes.length > 1024) {
          return Promise.resolve({
            bytes: bytes,
            name: 'SMD' + u + '.IMG',
            mode: 'MEMFS (session only)'
          });
        }
      }
    } catch (e) { /* not present */ }

    return Promise.reject(new Error('No image mounted on SMD unit ' + u +
      '. Mount one from the SMD Manager first.'));
  }

  // True when an image is reachable on `unit` without doing the read.
  function hasMountedImage(unit) {
    var u = unit || 0;
    if (typeof smdStorage !== 'undefined' && smdStorage.getUnitAssignment &&
        smdStorage.getUnitAssignment(u)) return true;
    try {
      if (typeof Module !== 'undefined' && Module.FS && Module.FS.analyzePath) {
        return !!Module.FS.analyzePath('/SMD' + u + '.IMG').exists;
      }
    } catch (e) { /* ignore */ }
    return false;
  }

  window.smdImageSource = {
    getMountedImage: getMountedImage,
    hasMountedImage: hasMountedImage
  };
})();
