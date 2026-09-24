// Copyright (C) 2026, The AROS Development Team. All rights reserved.
//
// exit-after.js - phase-2 probe app: open one BrowserWindow, then
// call app.exit(<code>) after <seconds>.  Run two of these (or one next to a
// real app) in a single ElectronShell to prove that one app's exit takes
// down only that app: its window closes and its runtime slot is released
// while the other app keeps running, and the process ends with its last app.
//
//   ElectronShell "--main-script=SYS:Developer/ElectronShell/exit-after.js"
//                 "--app-script=SYS:Developer/ElectronShell/exit-after.js"
//
// The delay and exit code come from the environment so the same file serves
// as either the early or the late app:
//   EXIT_AFTER_SECONDS (default 30), EXIT_AFTER_CODE (default 0),
//   EXIT_AFTER_LABEL (window title, default "exit-after").
// Extra apps share the process environment, so the primary/extra split is
// made with EXIT_AFTER_EXTRA_SECONDS / EXIT_AFTER_EXTRA_CODE, which the
// second instance (electron's app.isPackaged is the same; the shell marks the
// extra app with process.env.ELECTRON_APP_SLOT when it has one) picks up.
'use strict';
const { app, BrowserWindow } = require('electron');

const slot = Number(process.env.ELECTRON_APP_SLOT || 0);
const pick = (extraName, name, fallback) => {
  const value = slot > 0 && process.env[extraName] !== undefined
    ? process.env[extraName] : process.env[name];
  return value === undefined ? fallback : value;
};
const seconds = Number(pick('EXIT_AFTER_EXTRA_SECONDS', 'EXIT_AFTER_SECONDS', 30));
const code = Number(pick('EXIT_AFTER_EXTRA_CODE', 'EXIT_AFTER_CODE', 0));
const label = pick('EXIT_AFTER_EXTRA_LABEL', 'EXIT_AFTER_LABEL', 'exit-after') +
  ' slot ' + slot;

console.log('[exit-after] slot=' + slot + ' seconds=' + seconds +
  ' code=' + code + ' pid=' + process.pid);

function createWindow() {
  const win = new BrowserWindow({
    width: 360,
    height: 200,
    x: 40 + slot * 120,
    y: 60 + slot * 90,
    title: label,
    show: true,
  });
  const html = '<html><body style="font:20px sans-serif;background:#' +
    (slot ? '2b4c7e' : '7e4c2b') + ';color:#fff;margin:0;padding:16px">' +
    '<h2 style="margin:0 0 8px">' + label + '</h2>' +
    '<div>exits with code ' + code + ' after ' + seconds + ' s</div>' +
    '<div id="t" style="font-size:40px;margin-top:12px">' + seconds + '</div>' +
    '<script>var n=' + seconds + ';setInterval(function(){n--;' +
    'document.getElementById("t").textContent=n<0?0:n;},1000)</script>' +
    '</body></html>';
  win.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  win.on('closed', () => console.log('[exit-after] slot=' + slot +
    ' window closed'));
  return win;
}

app.whenReady().then(() => {
  createWindow();
  setTimeout(() => {
    console.log('[exit-after] slot=' + slot + ' calling app.exit(' + code + ')');
    app.exit(code);
  }, seconds * 1000);
});

// Closing the window by hand must not end the app before its timer does:
// that is the shell's "windows closed without app.exit" path, exercised by
// the other tests.
app.on('window-all-closed', () => {
  console.log('[exit-after] slot=' + slot + ' all windows closed, waiting for timer');
});
