#!/usr/bin/env node
//
// test-nd500-worker-browser.js - the ND-500 in WORKER mode, over the gateway's
// WebSocket, in a real browser.
//
// This is the hop docs/HOWTO-TEST-NDIX-NETWORKING.md calls "NOT PROVEN". It was
// worse than unproven: it was not wired. emu-proxy-worker.js answered
// available:false for the ND-500 and stubbed every method, while emu-proxy.js
// (direct mode, which HAS the ND-500) refuses wsConnect. The ND-500 lived in
// the mode with no WebSocket and the WebSocket lived in the mode with no
// ND-500, so a browser NDIX could never reach the gateway's ethernet segment.
//
// What this checks, in order, each one meaningless without the one before it:
//   1. the page really is in Worker mode
//   2. the ND-500 reports AVAILABLE there          <- was hardcoded false
//   3. it can be created and booted from the disc alone
//   4. its console reaches the page                <- rides the frame message
//   5. ethAttach puts NDIX's frames on the gateway segment, where a plain
//      RETH client that is not a browser can read them
//
// Step 5 is the one that matters: it is the browser talking to the same
// ethernet segment a native nd500x joins.
//
// Prerequisites: make wasm-glass, and an NDIX image (ND500X_ROOT or NDIX_IMAGE).
// Usage: node test-nd500-worker-browser.js [--verbose] [--headed]

'use strict';

const puppeteer = require('puppeteer');
const net = require('net');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn, execFileSync } = require('child_process');

const verbose = process.argv.includes('--verbose') || process.argv.includes('-v');
const headed = process.argv.includes('--headed');

const HTTP_PORT = 19081;
const WS_PORT = 19766;
const ETH_PORT = 19094;
const SERVE_DIR = path.join(__dirname, 'build_wasm_glass', 'bin');
const ND500X_ROOT = process.env.ND500X_ROOT ||
      path.resolve(__dirname, '..', 'nd500x');
const NDIX_IMAGE = process.env.NDIX_IMAGE ||
      path.join(ND500X_ROOT, 'docker', 'disk', 'rootfs_net.img');

let gatewayProc = null, browser = null, observer = null, tmpDir = null;
let passed = 0, failed = 0;

function log(...a) { if (verbose) console.log('  [test]', ...a); }
function check(name, ok, detail) {
  if (ok) { passed++; console.log('  [PASS] ' + name); }
  else { failed++; console.log('  [FAIL] ' + name + (detail ? ' - ' + detail : '')); }
}
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---- a plain RETH member, so what it sees proves the wire and not our code --
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

async function cleanup() {
  if (browser) { try { await browser.close(); } catch (e) {} }
  if (observer && observer.sock) observer.sock.destroy();
  if (gatewayProc) gatewayProc.kill('SIGTERM');
  if (tmpDir) { try { fs.rmSync(tmpDir, { recursive: true, force: true }); } catch (e) {} }
}

(async () => {
  if (!fs.existsSync(path.join(SERVE_DIR, 'index.html')))
    { console.log('no build at ' + SERVE_DIR + ' - run: make wasm-glass'); process.exit(1); }
  if (!fs.existsSync(NDIX_IMAGE))
    { console.log('no NDIX image at ' + NDIX_IMAGE + ' - set NDIX_IMAGE'); process.exit(1); }

  // A scratch copy with /etc/rc patched so the guest brings up et0 itself.
  // Never touch the original; it is the only one.
  tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'nd500-ws-'));
  const img = path.join(tmpDir, 'rootfs.img');
  const served = path.join(SERVE_DIR, 'nd500-test-disk.img');
  console.log('Preparing a scratch image (71 MB)...');
  fs.copyFileSync(NDIX_IMAGE, img);
  const ndixput = path.join(ND500X_ROOT, 'tools', 'ndix', 'ndixput.py');
  const rcFile = path.join(tmpDir, 'rc');
  execFileSync('python3', [ndixput, img, '/etc/rc', rcFile, '--get']);
  let rc = fs.readFileSync(rcFile, 'utf8').split('\n');
  const idx = rc.map((l, i) => [l.trim(), i]).filter(x => x[0] === 'exit 0').pop()[1];
  rc.splice(idx, 0,
    '/etc/etconfig et0 0x08 0x00 0x26 0xF4 0x01 0x00 > /dev/console 2>&1',
    '/etc/ifconfig et0 inet 223.255.254.9 netmask 255.255.255.0 -trailers up > /dev/console 2>&1');
  fs.writeFileSync(rcFile, rc.join('\n'));
  execFileSync('python3', [ndixput, img, '/etc/rc', rcFile, '--pad', '\\n']);
  fs.copyFileSync(img, served);   // served to the page over HTTP

  // The gateway serves the build AND the ethernet segment. --static is what
  // sends the COOP/COEP headers SharedArrayBuffer needs.
  const conf = path.join(tmpDir, 'gw.json');
  fs.writeFileSync(conf, JSON.stringify({
    websocket: { port: WS_PORT }, staticDir: SERVE_DIR,
    terminals: { port: 19002, welcome: 'nd500 ws test' }, hdlc: [],
    ethernet: [{ name: 'ETH-0', segment: 0, port: ETH_PORT, enabled: true }],
    smd: { images: [] }, floppy: { images: [] }, scsi: { images: [] }
  }));
  gatewayProc = spawn('node', [path.join(__dirname, 'tools', 'nd100-gateway', 'gateway.js'),
                               '--config', conf],
                      { stdio: verbose ? 'inherit' : 'ignore' });
  await sleep(1500);

  observer = await joinSegment(ETH_PORT);
  log('observer joined the segment');

  browser = await puppeteer.launch({
    headless: !headed,
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  });
  const page = await browser.newPage();
  page.on('console', m => log('page:', m.text()));
  page.on('pageerror', e => log('pageerror:', e.message));

  await page.goto(`http://localhost:${WS_PORT}/index.html?worker=1`,
                  { waitUntil: 'networkidle2', timeout: 60000 });

  check('page is in Worker mode',
        await page.evaluate(() => !!(window.emu && emu.isWorkerMode && emu.isWorkerMode())));

  await page.click('#toolbar-power');
  await page.waitForFunction(() => window.emu && emu.isInitialized && emu.isInitialized(),
                             { timeout: 60000 }).catch(() => {});

  // THE regression this whole change is about.
  const avail = await page.evaluate(() => emu.nd500.available());
  check('ND-500 reports available in Worker mode', avail === true,
        'available() returned ' + avail);

  if (!avail) { await cleanup(); console.log(`\n${passed} passed, ${failed} failed`); process.exit(1); }

  // Connect the gateway WebSocket FIRST. The worker's ethernet pump sits
  // inside `if (_ws && _ws.readyState === 1)`, so with no connection the
  // guest's frames are produced and then dropped on the floor, which looks
  // exactly like a guest that never transmitted.
  await page.evaluate((p) => emu.wsConnect('ws://localhost:' + p + '/'), WS_PORT);
  await sleep(2000);

  // Create, mount the disc, boot. Disc-only: no kernel file - the kernel is
  // taken out of /vmunix inside the image.
  await page.evaluate(() => { emu.nd500.setEnv('ND500X_NOXMSG', '0'); emu.nd500.create(0); });
  await sleep(500);
  check('ND-500 reports created', await page.evaluate(() => emu.nd500.isCreated()));

  console.log('Fetching the disc into the page and booting (this takes a moment)...');
  await page.evaluate(async () => {
    const r = await fetch('/nd500-test-disk.img');
    const b = new Uint8Array(await r.arrayBuffer());
    window.__disk = b;
    emu.nd500.mountDisk(0, b, true);
  });
  await sleep(1000);
  await page.evaluate(() => emu.nd500.boot());

  // The console arrives batched in the frame message, so accumulate it the way
  // the ND-500 window does.
  await page.evaluate(() => {
    window.__con = '';
    setInterval(() => {
      const c = emu.nd500.pollConsole();
      for (const ch of c) window.__con += ch.text;
    }, 50);
  });

  let booted = false;
  for (let i = 0; i < 120; i++) {
    await sleep(1000);
    const con = await page.evaluate(() => window.__con || '');
    if (/NDIX Release 3/.test(con)) { booted = true; break; }
  }
  const con = await page.evaluate(() => window.__con || '');
  check('ND-500 console reaches the page over the Worker port',
        con.length > 0, 'console was empty');
  check('NDIX booted in the browser', booted,
        'no "NDIX Release 3" after 120s; console tail: ' + JSON.stringify(con.slice(-200)));

  // The point of the exercise: frames from the browser onto the gateway segment.
  // XMSG must be ON or there is no et0 at all. nd500_wasm.c's Nd500_Create
  // does setenv("ND500X_NOXMSG","1",0) - overwrite 0, so the setEnv above
  // wins - but if it ever did not, the guest says so in one line and every
  // later conclusion about the ethernet would be wrong.
  const xg = /xgattach: bad completion code/.test(con);
  check('XMSG is enabled in the guest (no xgattach failure)', !xg,
        'xgattach failed - ND500X_NOXMSG was set, so et0 was never created');
  const et0 = /et0/.test(con);
  log('guest mentions et0: ' + et0);

  await page.evaluate(() => emu.nd500.ethAttach(0));
  const before = observer.frames.length;
  await sleep(20000);   // the guest ARPs and announces itself
  const after = observer.frames.length;
  check('browser frames reach the gateway ethernet segment', after > before,
        'the observer saw ' + (after - before) + ' frames after ethAttach');
  if (after > before)
    console.log('  first frame from the browser: ' +
                observer.frames[before].slice(0, 14).toString('hex'));

  try { fs.unlinkSync(served); } catch (e) {}
  await cleanup();
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed === 0 ? 0 : 1);
})().catch(async e => {
  console.error('test error:', e && e.message ? e.message : e);
  try { fs.unlinkSync(path.join(SERVE_DIR, 'nd500-test-disk.img')); } catch (_) {}
  await cleanup();
  process.exit(1);
});
