/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Print the runtime dependency closure of one built-in VS Code extension:
    every package reachable from its package.json "dependencies" through
    Node's resolution order (the extension's own node_modules, a package's
    nested node_modules, then extensions/node_modules and the repo root),
    one directory per line relative to <vscode-aros>.  stage-vscode.sh links
    exactly these, so devDependencies (typescript, esbuild, @types) stay out
    of the staged tree and an extension only fails to activate when a real
    runtime dependency is missing from the source checkout.

    Usage: node extension-runtime-deps.js <vscode-aros dir> <extension name>
*/
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.resolve(process.argv[2]);
const name = process.argv[3];
if (!root || !name) {
    process.stderr.write('usage: extension-runtime-deps.js <vscode-aros dir> <extension name>\n');
    process.exit(2);
}

const extDir = path.join(root, 'extensions', name);
const readPkg = (dir) => {
    try { return JSON.parse(fs.readFileSync(path.join(dir, 'package.json'), 'utf8')); }
    catch (error) { return null; }
};

// Node resolution: <from>/node_modules/<pkg>, then each parent's node_modules.
const resolvePkg = (from, pkg) => {
    let dir = from;
    for (;;) {
        const candidate = path.join(dir, 'node_modules', pkg);
        if (fs.existsSync(path.join(candidate, 'package.json'))) return candidate;
        const parent = path.dirname(dir);
        if (parent === dir || !dir.startsWith(root)) return null;
        dir = parent;
    }
};

const seen = new Set();
const missing = [];
const walk = (dir) => {
    const pkg = readPkg(dir);
    if (!pkg) return;
    for (const dep of Object.keys(pkg.dependencies || {})) {
        const found = resolvePkg(dir, dep);
        if (!found) { missing.push(dep + ' (from ' + path.relative(root, dir) + ')'); continue; }
        if (seen.has(found)) continue;
        seen.add(found);
        walk(found);
    }
};

walk(extDir);
for (const dir of [...seen].sort()) process.stdout.write(path.relative(root, dir) + '\n');
for (const m of missing) process.stderr.write('missing: ' + m + '\n');
