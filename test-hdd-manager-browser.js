#!/usr/bin/env node
//
// test-hdd-manager-browser.js - Puppeteer smoke test for the disk manager UI.
//
// Loads the built Glass UI (build_wasm_glass/bin) in Direct mode, waits for WASM
// init, and checks:
//   1. no uncaught console errors during load
//   2. the boot selector offers a SCSI option
//   3. the disk manager window opens without error
//
// Prerequisites: make wasm-glass; npm install (puppeteer).
// Usage: node test-hdd-manager-browser.js [--verbose] [--headed]

'use strict';

const puppeteer = require('puppeteer');
const http = require('http');
const fs = require('fs');
const path = require('path');

const verbose = process.argv.includes('--verbose') || process.argv.includes('-v');
const headed = process.argv.includes('--headed');
const HTTP_PORT = 19081;
const SERVE_DIR = path.join(__dirname, 'build_wasm_glass', 'bin');

let httpServer = null, browser = null, passed = 0, failed = 0;
function log(...a){ if (verbose) console.log('  [test]', ...a); }
function check(cond, name){ if (cond){ console.log('  PASS: '+name); passed++; } else { console.log('  FAIL: '+name); failed++; } return cond; }
const sleep = ms => new Promise(r => setTimeout(r, ms));

function startHTTP(){
  return new Promise((resolve, reject) => {
    const mime = { '.html':'text/html', '.js':'application/javascript', '.css':'text/css',
      '.wasm':'application/wasm', '.json':'application/json', '.png':'image/png',
      '.ico':'image/x-icon', '.svg':'image/svg+xml' };
    httpServer = http.createServer((req, res) => {
      const fp = path.join(SERVE_DIR, req.url === '/' ? 'index.html' : req.url.split('?')[0]);
      fs.readFile(fp, (err, data) => {
        if (err){ res.writeHead(404); res.end('nf'); return; }
        res.writeHead(200, {
          'Content-Type': mime[path.extname(fp)] || 'application/octet-stream',
          'Cross-Origin-Opener-Policy': 'same-origin',
          'Cross-Origin-Embedder-Policy': 'require-corp'
        });
        res.end(data);
      });
    });
    httpServer.listen(HTTP_PORT, resolve);
    httpServer.on('error', reject);
  });
}

// Console errors we consider benign (network/asset noise unrelated to logic).
function benign(text){
  return /favicon|Failed to load resource|net::ERR|SMD0\.IMG|404|version\.js|the server responded/i.test(text);
}

(async () => {
  if (!fs.existsSync(path.join(SERVE_DIR, 'index.html'))) {
    console.log('  SKIP: build_wasm_glass/bin/index.html missing - run "make wasm-glass" first.');
    process.exit(0);
  }
  await startHTTP();
  // Point at a cached puppeteer Chrome if the bundled version isn't installed.
  const glob = require('fs');
  let execPath;
  try {
    const base = path.join(require('os').homedir(), '.cache/puppeteer/chrome');
    const vers = glob.readdirSync(base).filter(d => d.startsWith('linux-')).sort();
    if (vers.length) execPath = path.join(base, vers[vers.length-1], 'chrome-linux64', 'chrome');
  } catch(e) {}
  const launchOpts = { headless: headed ? false : 'new',
    args: ['--no-sandbox', '--disable-setuid-sandbox'] };
  if (execPath && glob.existsSync(execPath)) launchOpts.executablePath = execPath;
  browser = await puppeteer.launch(launchOpts);
  const page = await browser.newPage();
  // Enable SMD persistence before any script runs, so the manager window is not
  // force-hidden (index.html hides it when persistence is off).
  await page.evaluateOnNewDocument(() => {
    try { localStorage.setItem('nd100x-smd-persist', 'true'); } catch(e) {}
  });
  const errors = [];
  page.on('console', m => { if (m.type() === 'error' && !benign(m.text())) errors.push(m.text()); });
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));

  try {
    await page.goto(`http://localhost:${HTTP_PORT}/index.html`, { waitUntil: 'load', timeout: 30000 });
    await sleep(9000); // allow WASM init + module wiring

    check(errors.length === 0, 'no uncaught console/page errors during load'
      + (errors.length ? ' -> ' + errors.slice(0,4).join(' | ') : ''));

    // Boot selector offers SCSI
    const bootOpts = await page.$$eval('#boot-select option', els => els.map(e => e.value));
    check(bootOpts.includes('scsi'), 'boot selector has a SCSI option (got: ' + bootOpts.join(',') + ')');

    // Disk manager window opens
    const opened = await page.evaluate(() => {
      var fn = window.smdManagerShow || window.hddManagerShow;
      if (typeof fn !== 'function') return { ok:false, why:'no manager show fn' };
      try { fn(); } catch(e){ return { ok:false, why:e.message }; }
      var w = document.getElementById('smd-manager-window') || document.getElementById('hdd-manager-window');
      return { ok: !!w && w.style.display !== 'none', why: w ? w.style.display : 'no window el' };
    });
    check(opened.ok, 'disk manager window opens (' + opened.why + ')');

    // Window title renamed to HDD Disk Manager
    const title = await page.evaluate(() => {
      var w = document.getElementById('smd-manager-window') || document.getElementById('hdd-manager-window');
      if (!w) return '';
      var t = w.querySelector('.glass-window-title');
      return t ? t.textContent.trim() : '';
    });
    check(/HDD Disk Manager/i.test(title), 'window titled "HDD Disk Manager" (got: "' + title + '")');

    // Menu item renamed
    const menu = await page.evaluate(() => {
      var b = document.getElementById('menu-smd-manager');
      return b ? b.textContent.trim() : '';
    });
    check(/HDD Disk Manager/i.test(menu), 'menu item says "HDD Disk Manager" (got: "' + menu + '")');

    // Tabs present: SMD, SCSI, Winchester
    const tabs = await page.$$eval('#hdd-tabs .hdd-tab', els => els.map(e => e.getAttribute('data-hdd-tab')));
    check(tabs.includes('smd') && tabs.includes('scsi') && tabs.includes('winchester'),
      'HDD manager has SMD/SCSI/Winchester tabs (got: ' + tabs.join(',') + ')');

    // Switching to SCSI shows the SCSI panel and hides SMD; 7 ID rows present
    const scsiTab = await page.evaluate(() => {
      if (typeof window.hddSelectTab !== 'function') return { ok:false, why:'no hddSelectTab' };
      try { window.hddSelectTab('scsi'); } catch(e){ return { ok:false, why:e.message }; }
      var scsi = document.getElementById('hdd-scsi-body');
      var smd = document.getElementById('smd-manager-body');
      var rows = document.querySelectorAll('#hdd-scsi-unit-list .smd-unit-row').length;
      return { ok: scsi && scsi.style.display !== 'none' && smd && smd.style.display === 'none' && rows === 7,
               why: 'scsiShown=' + (scsi && scsi.style.display) + ' smdHidden=' + (smd && smd.style.display) + ' rows=' + rows };
    });
    check(scsiTab.ok, 'SCSI tab shows SCSI panel (7 IDs) and hides SMD (' + scsiTab.why + ')');

    // Winchester tab is enabled (82b838d gave every disc controller a way
    // into the browser, Winchester included)
    const winDisabled = await page.$eval('#hdd-tabs .hdd-tab[data-hdd-tab="winchester"]', el => el.disabled);
    check(winDisabled === false, 'Winchester tab is enabled');

    // SCSI library renderer runs and shows its empty-state (no scsi images stored)
    const scsiLib = await page.evaluate(() => {
      if (typeof window.hddScsiRefreshLibrary !== 'function') return { ok:false, why:'no hddScsiRefreshLibrary' };
      try { window.hddScsiRefreshLibrary(); } catch(e){ return { ok:false, why:e.message }; }
      var el = document.getElementById('hdd-scsi-installed-list');
      var txt = el ? el.textContent : '';
      return { ok: /No SCSI disk images stored/i.test(txt), why: txt.slice(0,60) };
    });
    check(scsiLib.ok, 'SCSI library renderer produces empty-state (' + scsiLib.why + ')');

    // Assign helper exists and enforces the diskType constraint
    const hasAssign = await page.evaluate(() => typeof window.hddScsiAssign === 'function');
    check(hasAssign, 'hddScsiAssign (diskType-constrained SCSI assign) is defined');

    // Server catalog loads from hdd-catalog.json (not the old name)
    await page.evaluate(() => { window.hddSelectTab && window.hddSelectTab('smd'); if (window.smdRefreshCatalogList) window.smdRefreshCatalogList(); });
    await sleep(1500);
    const catOk = await page.evaluate(() => {
      var el = document.getElementById('smd-catalog-list');
      var txt = el ? el.textContent : '';
      return { err: /Could not load catalog/i.test(txt), has: /SINTRAN K/i.test(txt), txt: txt.slice(0,50) };
    });
    check(!catOk.err && catOk.has, 'server catalog loads from hdd-catalog.json (' + catOk.txt + ')');

    // Machine Setup window opens with an INI textarea
    const msOpen = await page.evaluate(() => {
      if (typeof window.machineSetupShow !== 'function') return { ok:false, why:'no machineSetupShow' };
      try { window.machineSetupShow(); } catch(e){ return { ok:false, why:e.message }; }
      var w = document.getElementById('machine-setup-window');
      var ta = document.getElementById('machine-setup-ini');
      return { ok: w && w.style.display !== 'none' && ta && ta.value.indexOf('[machine]') >= 0,
               why: 'disp=' + (w&&w.style.display) + ' hasIni=' + (ta && ta.value.length>0) };
    });
    check(msOpen.ok, 'Machine Setup window opens with default INI (' + msOpen.why + ')');

    // Native validator (ValidateMachineINI export) accepts the default INI...
    const okValid = await page.evaluate(async () => {
      if (!window.emu || !window.emu.validateMachineINI) return 'no validateMachineINI';
      return await Promise.resolve(window.emu.validateMachineINI(document.getElementById('machine-setup-ini').value));
    });
    check(okValid === '', 'ValidateMachineINI accepts default INI (msg="' + okValid + '")');

    // ...and rejects a bad one with a friendly message
    const badValid = await page.evaluate(async () => {
      return await Promise.resolve(window.emu.validateMachineINI('[controller.scsi.9]\ndisk0=hdd:x.img\n'));
    });
    check(/thumbwheel 9 out of range/i.test(badValid || ''), 'ValidateMachineINI rejects bad wheel (msg="' + badValid + '")');

  } catch (e) {
    check(false, 'test run threw: ' + e.message);
  } finally {
    if (browser) await browser.close();
    if (httpServer) httpServer.close();
  }

  console.log('\n  ' + passed + ' passed, ' + failed + ' failed');
  process.exit(failed ? 1 : 0);
})();
