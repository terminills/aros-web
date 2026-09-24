// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// two-windows.js - one app, two BrowserWindows.  Isolates the windowing
// layer from the multi-app runtime: with a single Node runtime
// the first window must keep painting after the second opens, and closing
// it with win.close() must take its Intuition window off the screen.
//
//   ElectronShell "--main-script=SYS:Developer/ElectronShell/two-windows.js"
//
//   TWO_WINDOWS_CLOSE_FIRST_AFTER  seconds before window 1 is closed (0 = never,
//                                  default 25)
//   TWO_WINDOWS_EXIT_AFTER         seconds before app.exit(0) (default 60)
//   TWO_WINDOWS_X_STEP / Y_STEP    offset of window 1 from window 0 (default
//                                  160/120, overlapping; 420/0 = side by side)
//   TWO_WINDOWS_SECOND_DELAY       seconds before window 1 opens (default 0)
'use strict';
const { app, BrowserWindow } = require('electron');

const closeFirstAfter = Number(process.env.TWO_WINDOWS_CLOSE_FIRST_AFTER || 25);
const exitAfter = Number(process.env.TWO_WINDOWS_EXIT_AFTER || 60);
const xStep = Number(process.env.TWO_WINDOWS_X_STEP || 160);
const yStep = Number(process.env.TWO_WINDOWS_Y_STEP || 120);
const secondDelay = Number(process.env.TWO_WINDOWS_SECOND_DELAY || 0);

function createWindow(index) {
  const win = new BrowserWindow({
    width: 360,
    height: 200,
    x: 40 + index * xStep,
    y: 60 + index * yStep,
    title: 'window ' + index,
    show: true,
  });
  const html = '<html><body style="font:20px sans-serif;background:#' +
    (index ? '2b4c7e' : '7e4c2b') + ';color:#fff;margin:0;padding:16px">' +
    '<h2 style="margin:0 0 8px">window ' + index + '</h2>' +
    '<div id="t" style="font-size:40px;margin-top:12px">0</div>' +
    '<script>var n=0;setInterval(function(){n++;' +
    'document.getElementById("t").textContent=n;},1000)</script>' +
    '</body></html>';
  win.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  win.on('closed', () => console.log('[two-windows] window ' + index +
    ' closed'));
  return win;
}

app.whenReady().then(() => {
  const first = createWindow(0);
  setTimeout(() => createWindow(1), secondDelay * 1000);
  if (closeFirstAfter > 0) {
    setTimeout(() => {
      console.log('[two-windows] closing window 0');
      first.close();
    }, closeFirstAfter * 1000);
  }
  setTimeout(() => {
    console.log('[two-windows] calling app.exit(0)');
    app.exit(0);
  }, exitAfter * 1000);
});

app.on('window-all-closed', () => {
  console.log('[two-windows] all windows closed, waiting for timer');
});
