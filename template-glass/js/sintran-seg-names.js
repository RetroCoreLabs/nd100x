//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// sintran-seg-names.js - System segment name lookup, version-keyed
//
// Segment table entries carry no name field, and the S3xxx names are
// NOT in any linker symbol table (verified by grep of all K03/L07/M06
// SYMBOL/RTLO/FILSYS/N500/XMSG lists). At runtime they exist only in
// the RT-loader's packed PSGNA table, mapped while the RT-loader runs.
//
// The clean source is therefore a per-version segment-number -> name
// map captured from a real "@RT-LOADER LIST-SEGMENT" run, shipped
// under data/segment-names/{K03,L07,M06}/list-segment.txt and selected
// by the version letter read from SINVER0 in memory.
// File format: "# comment" lines, then " NAME  NNN" (number in OCTAL).
//
// (Optional future path: decode the live PSGNA packed-name table for
// fully-dynamic names, including user/spooler segments.)

(function() {
  'use strict';

  var VERSION_DIRS = { K: 'K03', L: 'L07', M: 'M06' };

  // { letter: { segNum(decimal): name } }
  var nameMaps = {};
  var nameMapLoads = {};

  // Parse "@RT-LOADER LIST-SEGMENT" output: " NAME  NNN" with NNN octal.
  // A segment number can appear twice (e.g. L07 lists both S3XMSGP and
  // S3XMK as 76); keep all names joined with '/'.
  function parseListSegment(text) {
    var map = {};
    var lines = text.split('\n');
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].trim();
      if (line === '' || line.charAt(0) === '#') continue;
      var m = line.match(/^(\S+)\s+([0-7]+)$/);
      if (!m) continue;
      var name = m[1];
      var num = parseInt(m[2], 8);
      if (map[num] && map[num].indexOf(name) === -1) {
        map[num] = map[num] + '/' + name;
      } else if (!map[num]) {
        map[num] = name;
      }
    }
    return map;
  }

  // Load the segment-name map for a version letter (K/L/M).
  // Returns Promise<map|null>; result is cached.
  function loadSegmentNames(letter) {
    if (!letter) return Promise.resolve(null);
    var key = letter.toUpperCase();
    if (nameMaps[key]) return Promise.resolve(nameMaps[key]);
    if (nameMapLoads[key]) return nameMapLoads[key];
    var dir = VERSION_DIRS[key];
    if (!dir) return Promise.resolve(null);
    var url = 'data/segment-names/' + dir + '/list-segment.txt';
    nameMapLoads[key] = fetch(url)
      .then(function(r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.text();
      })
      .then(function(text) {
        var map = parseListSegment(text);
        nameMaps[key] = map;
        delete nameMapLoads[key];
        console.log('[segnames] Loaded ' + dir + ' segment names: ' +
                    Object.keys(map).length + ' entries');
        return map;
      })
      .catch(function(err) {
        delete nameMapLoads[key];
        console.warn('[segnames] No segment-name capture for ' + dir + ':', err.message);
        return null;
      });
    return nameMapLoads[key];
  }

  function currentMap() {
    if (typeof sintranState === 'undefined' || !sintranState.versionLetter) return null;
    return nameMaps[sintranState.versionLetter.toUpperCase()] || null;
  }

  // Resolve segment number (decimal) to name, or empty string
  function resolveSegmentName(segNum) {
    var map = currentMap();
    if (!map) return '';
    return map[segNum] || '';
  }

  // Mechanically derived description from the S3 naming convention:
  // S3Sxxx = save copy, S3Ixxx = image copy of the corresponding
  // active segment. No invented facts.
  function resolveSegmentDescription(segNum) {
    var name = resolveSegmentName(segNum);
    if (!name) return null;
    var first = name.split('/')[0];
    if (/^S3S/.test(first)) return 'System segment (save copy)';
    if (/^S3I/.test(first)) return 'System segment (image copy)';
    if (/^S3/.test(first)) return 'System segment';
    return null;
  }

  function resolveSegmentCategory(segNum) {
    var name = resolveSegmentName(segNum);
    if (!name) return null;
    var first = name.split('/')[0];
    if (/^S3S/.test(first)) return 'Save copy';
    if (/^S3I/.test(first)) return 'Image copy';
    if (/^S3/.test(first)) return 'System';
    return null;
  }

  window.resolveSegmentName = resolveSegmentName;
  window.resolveSegmentDescription = resolveSegmentDescription;
  window.resolveSegmentCategory = resolveSegmentCategory;
  window.sintranSegNames = {
    loadSegmentNames: loadSegmentNames,
    parseListSegment: parseListSegment,
    resolveSegmentName: resolveSegmentName,
    resolveSegmentDescription: resolveSegmentDescription,
    resolveSegmentCategory: resolveSegmentCategory
  };
})();
