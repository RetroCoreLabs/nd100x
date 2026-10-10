#!/usr/bin/env node
//
// test-eth-nd100-browser.js - the ND-100 Ethernet II card in the browser,
// end to end through the gateway.
//
// Boots the built-in TCP/IP machine (WD0-SINTRAN-M.IMG, net = gateway:0) in
// Worker mode, connects the page to a gateway with Ethernet segment 0, and
// joins that segment as a plain RETH member. The member then talks to the
// ND the way any host on the wire would:
//   1. ARP who-has 192.168.210.40      -> expects an ARP reply from the ND
//   2. ICMP echo request to .40        -> expects an ICMP echo reply
// What this proves: frames leave the page through the gateway (0x31), frames
// from the segment reach the card (0x30), and SINTRAN's TCP/IP answers.
//
// --machine=COSMOS boots the built-in COSMOS machine (BIGDISK0-K-100.IMG)
// instead and checks that the console says "Network server ENNS0 started"
// and that IEEE 802.3 length frames (type/length field <= 1500) from the
// card reach the segment. No COSMOS peer is on the segment, so nothing is
// expected back.
//
// Prerequisites: make wasm-glass (or BUILD_DIR=<dir> for another build dir).
// Usage (from the repo root): node test-eth-nd100-browser.js [--machine=COSMOS] [--verbose] [--headed]

'use strict';

const puppeteer = require('puppeteer');
const net = require('net');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const verbose = process.argv.includes('--verbose') || process.argv.includes('-v');
const headed = process.argv.includes('--headed');

const SERVE_DIR = path.join(__dirname, process.env.BUILD_DIR || 'build_wasm_glass', 'bin');
const WS_PORT = 19766;
const ETH_PORT = 19094;
const TERM_PORT = 19003;
const MACHINE = (process.argv.find(a => a.startsWith('--machine=')) || '--machine=TCP/IP').slice(10);
const IMAGE = { 'TCP/IP': 'WD0-SINTRAN-M.IMG', 'COSMOS': 'BIGDISK0-K-100.IMG' }[MACHINE];

// The ND's address comes from the AIP files on WD0-SINTRAN-M.IMG (docs/ETHERNET.md).
const ND_IP = [192, 168, 210, 40];
const HOST_IP = [192, 168, 210, 1];
const HOST_MAC = Buffer.from([0x02, 0x00, 0x00, 0x00, 0x00, 0x99]);

let browser = null, gatewayProc = null, observer = null, confPath = null;
let passed = 0, failed = 0;

function log(...a) { if (verbose) console.log('  [test]', ...a); }
function check(name, ok, detail) {
  if (ok) { passed++; console.log('  [PASS] ' + name); }
  else { failed++; console.log('  [FAIL] ' + name + (detail ? ' - ' + detail : '')); }
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---- a plain RETH member: TCP "RETH"+version, then u16 BE length + frame ----
function joinSegment(port) {
  return new Promise((resolve, reject) => {
    const sock = net.connect(port, '127.0.0.1');
    const frames = [];
    let rx = Buffer.alloc(0), greeted = false;
    sock.on('connect', () => {
      const hello = Buffer.alloc(5);
      Buffer.from('RETH', 'ascii').copy(hello, 0);
      hello[4] = 1;
      sock.write(hello);
      resolve({ sock, frames });
    });
    sock.on('data', d => {
      rx = Buffer.concat([rx, d]);
      if (!greeted) {
        if (rx.length < 5) return;
        rx = rx.slice(5); greeted = true;
      }
      for (;;) {
        if (rx.length < 2) break;
        const len = (rx[0] << 8) | rx[1];
        if (len === 0 || len > 2048 || rx.length < 2 + len) break;
        frames.push(rx.slice(2, 2 + len));
        rx = rx.slice(2 + len);
      }
    });
    sock.on('error', reject);
  });
}

function sendFrame(sock, frame) {
  const hdr = Buffer.alloc(2);
  hdr.writeUInt16BE(frame.length, 0);
  sock.write(Buffer.concat([hdr, frame]));
}

function pad60(b) { return b.length >= 60 ? b : Buffer.concat([b, Buffer.alloc(60 - b.length)]); }

// RFC 1071 one's-complement sum.
function ipsum(b) {
  let s = 0;
  for (let i = 0; i < b.length; i += 2) s += (b[i] << 8) | (i + 1 < b.length ? b[i + 1] : 0);
  while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
  return (~s) & 0xFFFF;
}

// ARP request, RFC 826: htype 1, ptype 0800, hlen 6, plen 4, op 1.
function arpRequest() {
  const f = Buffer.alloc(42);
  Buffer.from([0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]).copy(f, 0);
  HOST_MAC.copy(f, 6);
  f.writeUInt16BE(0x0806, 12);
  f.writeUInt16BE(1, 14); f.writeUInt16BE(0x0800, 16); f[18] = 6; f[19] = 4;
  f.writeUInt16BE(1, 20);
  HOST_MAC.copy(f, 22); Buffer.from(HOST_IP).copy(f, 28);
  Buffer.from(ND_IP).copy(f, 38);
  return pad60(f);
}

// ICMP echo request, RFC 792, inside a 20-byte IPv4 header (RFC 791).
function icmpEcho(ndMac, seq) {
  const payload = Buffer.from('nd100x-eth-browser-test');
  const icmp = Buffer.alloc(8 + payload.length);
  icmp[0] = 8; icmp.writeUInt16BE(0x4E44, 4); icmp.writeUInt16BE(seq, 6);
  payload.copy(icmp, 8);
  icmp.writeUInt16BE(ipsum(icmp), 2);
  const ip = Buffer.alloc(20);
  ip[0] = 0x45; ip.writeUInt16BE(20 + icmp.length, 2); ip.writeUInt16BE(seq, 4);
  ip[8] = 64; ip[9] = 1;
  Buffer.from(HOST_IP).copy(ip, 12); Buffer.from(ND_IP).copy(ip, 16);
  ip.writeUInt16BE(ipsum(ip), 10);
  const eth = Buffer.alloc(14);
  ndMac.copy(eth, 0); HOST_MAC.copy(eth, 6); eth.writeUInt16BE(0x0800, 12);
  return pad60(Buffer.concat([eth, ip, icmp]));
}

function isArpReplyFromNd(f) {
  return f.length >= 42 && f.readUInt16BE(12) === 0x0806 && f.readUInt16BE(20) === 2 &&
         Buffer.compare(f.slice(28, 32), Buffer.from(ND_IP)) === 0;
}
function isEchoReplyFromNd(f) {
  return f.length >= 42 && f.readUInt16BE(12) === 0x0800 && f[23] === 1 &&
         Buffer.compare(f.slice(26, 30), Buffer.from(ND_IP)) === 0 && f[34] === 0;
}

async function waitFor(fn, ms) {
  for (let t = 0; t < ms; t += 500) { const r = fn(); if (r) return r; await sleep(500); }
  return null;
}

async function finish() {
  await cleanup();
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
}

async function cosmosChecks(page) {
  let con = '';
  for (let i = 0; i < 300; i++) {
    await sleep(1000);
    con = await page.evaluate(() => window.__con);
    if (/Network server ENNS0 started/.test(con)) break;
  }
  check('console reaches the page', con.length > 0, 'console empty');
  check('COSMOS says "Network server ENNS0 started"', /Network server ENNS0 started/.test(con),
        'console tail: ' + JSON.stringify(con.slice(-400)));
  const is8023 = f => f.length >= 17 && f.readUInt16BE(12) <= 1500;
  const f = await waitFor(() => observer.frames.find(is8023), 60000);
  check('IEEE 802.3 frames from the card reach the segment', !!f,
        observer.frames.length + ' frames seen, none with a length field');
  if (f) {
    console.log('  first: dst ' + f.slice(0, 6).toString('hex') + ' src ' + f.slice(6, 12).toString('hex') +
                ' len ' + f.readUInt16BE(12) + ' LLC ' + f.slice(14, 17).toString('hex'));
  }
  await finish();
}

async function cleanup() {
  if (browser) { try { await browser.close(); } catch (e) {} }
  if (observer && observer.sock) observer.sock.destroy();
  if (gatewayProc) gatewayProc.kill('SIGTERM');
  if (confPath) { try { fs.unlinkSync(confPath); } catch (e) {} }
}

(async () => {
  console.log('\nND-100 Ethernet II in the browser, through the gateway');
  console.log('======================================================\n');
  if (!IMAGE) { console.log('--machine must be TCP/IP or COSMOS'); process.exit(1); }
  if (!fs.existsSync(path.join(SERVE_DIR, 'nd100wasm.js')) ||
      !fs.existsSync(path.join(SERVE_DIR, IMAGE))) {
    console.log('no build or no ' + IMAGE + ' at ' + SERVE_DIR + ' - run: make wasm-glass');
    process.exit(1);
  }

  // The gateway serves the build (staticDir sends the COOP/COEP headers) and
  // carries Ethernet segment 0.
  confPath = path.join(__dirname, '.test-eth-nd100-gateway.json');
  fs.writeFileSync(confPath, JSON.stringify({
    websocket: { port: WS_PORT }, staticDir: SERVE_DIR,
    terminals: { port: TERM_PORT, welcome: 'nd100 eth test' }, hdlc: [],
    ethernet: [{ name: 'ETH-0', segment: 0, port: ETH_PORT, enabled: true }],
    smd: { images: [] }, floppy: { images: [] }, scsi: { images: [] }
  }));
  gatewayProc = spawn('node', [path.join(__dirname, 'tools', 'nd100-gateway', 'gateway.js'),
                               '--config', confPath].concat(verbose ? ['--verbose'] : []), { stdio: verbose ? 'inherit' : 'ignore' });
  await sleep(1500);
  observer = await joinSegment(ETH_PORT);
  log('observer joined segment 0');

  browser = await puppeteer.launch({ headless: !headed, args: ['--no-sandbox', '--disable-setuid-sandbox'] });
  const page = await browser.newPage();
  page.on('console', m => log('page:', m.text()));
  page.on('pageerror', e => log('pageerror:', e.message));

  const url = `http://localhost:${WS_PORT}/index.html?worker=1`;
  await page.goto(url, { waitUntil: 'networkidle2', timeout: 60000 });
  const known = await page.evaluate((m) => machineProfiles.list().indexOf(m) >= 0, MACHINE);
  check('the ' + MACHINE + ' machine is built in', known);
  if (!known) { await cleanup(); console.log(`\n${passed} passed, ${failed} failed`); process.exit(1); }
  await page.evaluate((m) => machineProfiles.setActive(m), MACHINE);
  await page.goto(url, { waitUntil: 'networkidle2', timeout: 60000 });
  check('it is the active machine after reload',
        await page.evaluate((m) => machineProfiles.activeName() === m, MACHINE));
  check('page is in Worker mode', await page.evaluate(() => !!(window.emu && emu.isWorkerMode())));

  // Capture the console terminal output (ident 1) the way the page delivers it.
  await page.evaluate(() => {
    window.__con = '';
    const prev = window.handleTerminalOutputFromC;
    window.handleTerminalOutputFromC = function (id, ch) {
      window.__ids = window.__ids || {};
      window.__ids[id] = (window.__ids[id] || 0) + 1;
      if (id === 1) window.__con += String.fromCharCode(ch & 0x7F);
      if (prev) prev(id, ch);
    };
  });

  await page.evaluate(() => document.getElementById('toolbar-power').click());
  await page.waitForFunction(() => window.emu && emu.isInitialized && emu.isInitialized(),
                             { timeout: 120000 }).catch(() => {});
  await page.evaluate((p) => emu.wsConnect('ws://localhost:' + p + '/'), WS_PORT);
  console.log('Booting ' + MACHINE + ' from ' + IMAGE + ' (up to 5 minutes)...');
  if (MACHINE === 'COSMOS') { await cosmosChecks(page); return; }

  let started = null;
  for (let i = 0; i < (+process.env.BOOT_SECONDS || 300) && !started; i++) {
    await sleep(1000);
    const c = await page.evaluate(() => window.__con);
    if (/started/i.test(c) && /192\.168\.210\.40/.test(c)) started = c;
  }
  const con = await page.evaluate(() => window.__con);
  log('terminal output by ident: ' + JSON.stringify(await page.evaluate(() => window.__ids || {})));
  check('console reaches the page', con.length > 0, 'console empty');
  check('SINTRAN reports 192.168.210.40 started', !!started,
        'console tail: ' + JSON.stringify(con.slice(-400)));

  // ARP: the reply proves both directions through the gateway.
  const fromNdBefore = observer.frames.length;
  // SINTRAN prints "started" before every part of its TCP/IP is listening,
  // so the request is repeated for a minute.
  let arp = null;
  for (let i = 0; i < 12 && !arp; i++) {
    sendFrame(observer.sock, arpRequest());
    arp = await waitFor(() => observer.frames.find(isArpReplyFromNd), 5000);
  }
  check('ARP reply from 192.168.210.40 on the segment', !!arp,
        (observer.frames.length - fromNdBefore) + ' frames seen after the request');
  if (!arp) { await cleanup(); console.log(`\n${passed} passed, ${failed} failed`); process.exit(1); }
  const ndMac = arp.slice(22, 28);
  console.log('  ND MAC: ' + [...ndMac].map(b => b.toString(16).padStart(2, '0')).join(':'));

  let echo = null;
  for (let seq = 1; seq <= 3 && !echo; seq++) {
    sendFrame(observer.sock, icmpEcho(ndMac, seq));
    echo = await waitFor(() => observer.frames.find(isEchoReplyFromNd), 5000);
  }
  check('ICMP echo reply from 192.168.210.40', !!echo);

  await cleanup();
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})().catch(async e => {
  console.error('test error:', e && e.message ? e.message : e);
  await cleanup();
  process.exit(1);
});
