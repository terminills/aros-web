// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// blink-worker-isolate.js - regression test for the platform a Blink worker
// isolate resolves.
//
// Blink creates a Web Worker's v8::Isolate ON the worker thread
// (v8::Isolate::Initialize, crash report 17789-933-321-ServiceWorker_thread),
// so whatever V8 platform that thread resolves is the one V8 will ask about
// that isolate for the rest of its life.  If it resolves node's
// MultiIsolatePlatform - which node.library creates for the isolates IT owns
// - then node::NodePlatform::ForIsolate fails CHECK(data.first) on an isolate
// it never registered, V8's OS::Abort turns that into a trap instruction, and
// the guest shows an "Invalid opcode" Guru on a worker thread.  That is what
// took out VS Code's extension host and any page with a service worker.
//
// A plain Web Worker is the smallest thing that reaches it: no VS Code, no
// service worker registration, no network.
//
//   ElectronShell --single-process \
//     "--main-script=SYS:Developer/ElectronShell/blink-worker-isolate.js"
//
// PASS: "[BLINK-WORKER] worker replied" and exit code 0.
// FAIL: a Guru on a worker thread, or the reply never arrives (exit code 3).
//
//   BLINK_WORKER_COUNT      workers to start (default 4; more than one, so a
//                           second isolate after the first also has to work)
//   BLINK_WORKER_TIMEOUT    seconds to wait for all replies (default 45)
'use strict';
const { app, BrowserWindow } = require('electron');

const workerCount = Number(process.env.BLINK_WORKER_COUNT || 4);
const timeoutSecs = Number(process.env.BLINK_WORKER_TIMEOUT || 45);

// The page starts the workers itself; each worker does a little arithmetic so
// its isolate actually runs code (and gets a GC-capable heap) rather than
// only being created.
const page = `<!doctype html>
<meta charset="utf-8">
<title>Blink worker isolate</title>
<body style="font:14px sans-serif;background:#101418;color:#e6e9ef">
<p id="s">starting ${workerCount} workers...</p>
<script>
const total = ${workerCount};
let replied = 0;
const src = 'self.onmessage = function (e) {' +
            '  var acc = 0;' +
            '  for (var i = 0; i < 200000; i++) acc += i % 7;' +
            '  self.postMessage({ id: e.data.id, acc: acc });' +
            '};';
const url = URL.createObjectURL(new Blob([src], { type: 'text/javascript' }));
for (let i = 0; i < total; i++) {
  const w = new Worker(url);
  w.onmessage = function (e) {
    replied++;
    console.log('[BLINK-WORKER] reply ' + e.data.id + ' acc=' + e.data.acc +
                ' (' + replied + '/' + total + ')');
    document.getElementById('s').textContent = replied + '/' + total +
                                               ' workers replied';
    if (replied === total) {
      console.log('[BLINK-WORKER] all workers replied');
      document.title = 'WORKERS-OK ' + replied + '/' + total;
    }
  };
  w.onerror = function (e) {
    console.log('[BLINK-WORKER] worker error: ' + (e && e.message));
    document.title = 'WORKER-ERROR ' + (e && e.message);
  };
  w.postMessage({ id: i });
}
</script>
</body>`;

let done = false;

function finish(code, why) {
  if (done) return;
  done = true;
  console.log('[BLINK-WORKER] ' + why);
  app.exit(code);
}

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 420,
    height: 200,
    webPreferences: { nodeIntegration: false, contextIsolation: true },
  });

  win.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(page));

  /*
   * The page reports through its TITLE rather than console-message: the
   * renderer's console does reach the debug log, but a main-process
   * 'console-message' listener is not wired up in this port, and a test
   * whose verdict depends on a listener that never fires reports a false
   * failure (it did: 4/4 workers replied and the test still exited 3).
   */
  let last = '';
  const poll = setInterval(() => {
    let title = '';
    try {
      title = win.getTitle() || '';
    } catch (e) {
      clearInterval(poll);
      finish(5, 'FAIL: window gone: ' + e);
      return;
    }
    if (title === last) return;
    last = title;
    process._rawDebug('[BLINK-WORKER] status: ' + title);
    if (title.indexOf('WORKERS-OK') === 0) {
      clearInterval(poll);
      finish(0, 'PASS: every Blink worker isolate ran (' + title + ')');
    } else if (title.indexOf('WORKER-ERROR') === 0) {
      clearInterval(poll);
      finish(4, 'FAIL: ' + title);
    }
  }, 500);

  setTimeout(() => {
    clearInterval(poll);
    finish(3, 'FAIL: timed out, last status "' + last + '"');
  }, timeoutSecs * 1000);
});
