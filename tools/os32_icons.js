#!/usr/bin/env node
// Copyright (c) 2026 Dalsin Limited. SPDX-License-Identifier: MIT
// Turns OpenPrint's classic icons into OS 3.2-style ones (an OS 3.5 colour
// icon with a classic fallback), keeping each icon's type, default tool,
// tool types, stack and drawer window. Written by ACBuild's amiga-icon.js.
//   node tools/os32_icons.js AMIGA_ICON_JS JOBS.json
// JOBS: [{ "info": "path.info", "rgba": "art.rgba", "w": 48, "h": 40 }
//        | { "info": "path.info", "from": "a colour icon to take the picture from" }]
const fs = require('fs'), path = require('path');
const A = require(path.resolve(process.argv[2]));
const jobs = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
(async () => {
  for (const j of jobs) {
    const old = await A.parse(new Uint8Array(fs.readFileSync(j.info)));
    let doc;
    if (j.from) doc = A.toDocument(await A.parse(new Uint8Array(fs.readFileSync(j.from))));
    else { doc = A.documentFromRGBA([new Uint8Array(fs.readFileSync(j.rgba))], j.w, j.h); doc = A.glowSelected(doc) || doc; }
    doc.meta = Object.assign({}, doc.meta || {}, { type: old.type, defaultTool: old.defaultTool, toolTypes: old.toolTypes || [],
      toolWindow: old.toolWindow, stack: old.stack || 4096, drawer: old.drawer || (doc.meta && doc.meta.drawer) });
    fs.writeFileSync(j.info, Buffer.from(await A.write(doc, 'os35')));
    console.log('OS 3.2 icon:', j.info);
  }
})().catch(e => { console.error(e); process.exit(1); });
