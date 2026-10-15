#!/usr/bin/env node
//
// verify-disasm-worker.js - Puppeteer verification for the OPFS/Worker-mode
// segment-disassembler disk-read + disassembly fix.
//
// Exercises the exact code paths that were broken:
//   1. Dbg_ReadSMDSectors OPFS path (C)      -> readSMDSectorsAsync round-trip
//   2. disassembleWordsAsync worker round-trip -> mnemonics instead of hex dump
//
// It mounts SMD0.IMG via OPFS on unit 0 in Worker mode, reads sector 0, and
// disassembles a handful of words. No SINTRAN boot required.
//
// Usage (from the repo root): node tests/browser/verify-disasm-worker.js [--headed]

'use strict';

const puppeteer = require('puppeteer');
const http = require('http');
const fs = require('fs');
const path = require('path');
const REPO_ROOT = path.resolve(__dirname, '..', '..'); // this file lives in tests/browser/

const headed = process.argv.includes('--headed');
const HTTP_PORT = 19081;
const SERVE_DIR = path.join(REPO_ROOT, 'build_wasm_glass', 'bin');

let httpServer = null;
let browser = null;

function startHTTPServer() {
  return new Promise((resolve, reject) => {
    const mime = {
      '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css',
      '.wasm': 'application/wasm', '.json': 'application/json', '.png': 'image/png',
      '.ico': 'image/x-icon', '.svg': 'image/svg+xml', '.IMG': 'application/octet-stream'
    };
    httpServer = http.createServer((req, res) => {
      const rel = req.url === '/' ? 'index.html' : req.url.split('?')[0];
      const filePath = path.join(SERVE_DIR, rel);
      fs.readFile(filePath, (err, data) => {
        if (err) { res.writeHead(404); res.end('Not found: ' + req.url); return; }
        res.writeHead(200, {
          'Content-Type': mime[path.extname(filePath)] || 'application/octet-stream',
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

async function cleanup() {
  if (browser) try { await browser.close(); } catch (e) {}
  if (httpServer) httpServer.close();
}

(async () => {
  let ok = true;
  try {
    await startHTTPServer();
    browser = await puppeteer.launch({
      headless: headed ? false : 'new',
      executablePath: process.env.CHROME_PATH || undefined,
      args: ['--no-sandbox', '--enable-features=SharedArrayBuffer']
    });
    const page = (await browser.pages())[0];
    page.on('console', m => {
      const t = m.text();
      if (/\[disasm\]|\[Worker\]|readSMDSectors|disassemble|OPFS|MountSMD/i.test(t)) {
        console.log('  [page]', t);
      }
    });
    page.on('pageerror', e => console.log('  [pageerror]', e.message));

    await page.goto(`http://localhost:${HTTP_PORT}/index.html?worker=1`, {
      waitUntil: 'networkidle0', timeout: 30000
    });

    const useWorker = await page.evaluate(() => typeof USE_WORKER !== 'undefined' && USE_WORKER);
    console.log('Worker mode:', useWorker);
    if (!useWorker) throw new Error('Not in worker mode');

    // Run the whole flow inside the page.
    const result = await page.evaluate(async () => {
      function waitFor(cond, ms) {
        return new Promise((res, rej) => {
          const t0 = Date.now();
          (function poll() {
            try { if (cond()) return res(true); } catch (e) {}
            if (Date.now() - t0 > ms) return rej(new Error('timeout'));
            setTimeout(poll, 50);
          })();
        });
      }

      await waitFor(() => window.emu && window.smdStorage, 15000);
      await smdStorage.init();

      // Fetch the disk image and store it in OPFS.
      const buf = await fetch('SMD0.IMG').then(r => r.arrayBuffer());
      const data = new Uint8Array(buf);
      const uuid = smdStorage.generateUUID();
      await smdStorage.storeImage(uuid, data, { name: 'VERIFY.IMG', source: 'verify' });

      // Init hardware in the worker, then mount unit 0 from OPFS.
      const initDone = new Promise(res => window.addEventListener('emu-initialized', res, { once: true }));
      emu.init();
      await initDone;

      const mount = await emu.opfsMountSMD(0, uuid);

      // 1) Read sector 0 across the worker boundary from the OPFS-backed drive.
      const sec = await emu.readSMDSectorsAsync(0, 0, 1);
      let nonZero = 0;
      for (let i = 0; i < sec.length; i++) if (sec[i]) nonZero++;

      // Compare first 16 bytes against the image we uploaded.
      let matches = true;
      for (let i = 0; i < 16; i++) if (sec[i] !== data[i]) { matches = false; break; }

      // 2) Disassemble the first 8 words of sector 0.
      const words = new Uint16Array(8);
      for (let w = 0; w < 8; w++) words[w] = (sec[w * 2] << 8) | sec[w * 2 + 1];
      const disasm = await emu.disassembleWordsAsync(words, 0);

      return {
        mountOk: mount && mount.ok,
        mountSize: mount && mount.size,
        secLen: sec.length,
        secNonZero: nonZero,
        firstBytesMatch: matches,
        disasmLen: disasm ? disasm.length : 0,
        disasmSample: disasm ? disasm.split('\n').slice(0, 4).join(' | ') : ''
      };
    });

    console.log('\n--- Results ---');
    console.log(JSON.stringify(result, null, 2));

    function check(cond, msg) {
      console.log((cond ? '  PASS: ' : '  FAIL: ') + msg);
      if (!cond) ok = false;
    }
    check(result.mountOk, 'OPFS mount on unit 0 succeeded');
    check(result.secLen === 1024, 'readSMDSectorsAsync returned 1024 bytes');
    check(result.secNonZero > 0, 'sector 0 has non-zero data (OPFS read path works)');
    check(result.firstBytesMatch, 'sector 0 matches uploaded image bytes');
    check(result.disasmLen > 0, 'disassembleWordsAsync returned text');
    check(/\d/.test(result.disasmSample) && result.disasmSample.trim().length > 0,
          'disassembly produced mnemonic lines');

  } catch (e) {
    console.log('ERROR:', e.message);
    ok = false;
  } finally {
    await cleanup();
  }
  console.log('\n' + (ok ? 'ALL CHECKS PASSED' : 'SOME CHECKS FAILED'));
  process.exit(ok ? 0 : 1);
})();
