import { readFileSync } from 'node:fs';

function parseNetlist(file) {
  const lines = readFileSync(file, 'utf8').split(/\r?\n/);
  const components = {};
  const nets = {};
  let mode = null;
  let cur = null;
  let pendingKey = null;
  for (let i = 0; i < lines.length; i++) {
    const t = lines[i].trim();
    if (t === '[') { mode = 'comp'; cur = {}; pendingKey = null; continue; }
    if (t === ']') { if (cur && cur.DESIGNATOR) components[cur.DESIGNATOR] = cur; mode = null; cur = null; pendingKey = null; continue; }
    if (t === '(') { mode = 'net'; cur = { name: null, nodes: [] }; continue; }
    if (t === ')') { if (cur && cur.name) nets[cur.name] = { nodes: cur.nodes, count: cur.nodes.length }; mode = null; cur = null; continue; }
    if (mode === 'comp') {
      if (pendingKey !== null) { cur[pendingKey] = t; pendingKey = null; continue; }
      if (t) pendingKey = t;
    } else if (mode === 'net') {
      if (cur.name === null) { cur.name = t; continue; }
      if (t) { const p = t.split(/\s+/); cur.nodes.push(p[0]); }
    }
  }
  return { components, nets };
}

function pinMap(nets) {
  const m = new Map();
  for (const [net, info] of Object.entries(nets)) {
    for (const node of info.nodes) m.set(node, net);
  }
  return m;
}

const A = parseNetlist(process.argv[2]); // baseline
const B = parseNetlist(process.argv[3]); // current
const labelA = process.argv[4] || 'baseline';
const labelB = process.argv[5] || 'current';

console.log(`=== COMPONENT DIFF (${labelA} -> ${labelB}) ===`);
const aDs = new Set(Object.keys(A.components));
const bDs = new Set(Object.keys(B.components));
for (const d of bDs) if (!aDs.has(d)) console.log(`+ component ${d}  ${B.components[d].PARTTYPE || ''} ${B.components[d].Value || ''}`);
for (const d of aDs) if (!bDs.has(d)) console.log(`- component ${d}  ${A.components[d].PARTTYPE || ''} ${A.components[d].Value || ''}`);
const fieldKeys = ['PARTTYPE', 'Value', 'FOOTPRINT', 'Name', 'Manufacturer Part', 'Supplier Part'];
for (const d of [...aDs].filter((x) => bDs.has(x)).sort()) {
  const a = A.components[d], b = B.components[d];
  const changes = [];
  for (const k of fieldKeys) {
    if ((a[k] ?? '') !== (b[k] ?? '')) changes.push(`${k}: "${a[k] ?? ''}" -> "${b[k] ?? ''}"`);
  }
  if (changes.length) console.log(`~ component ${d}  ${changes.join(' | ')}`);
}

console.log(`\n=== PIN -> NET DIFF (${labelA} -> ${labelB}) ===`);
const pa = pinMap(A.nets);
const pb = pinMap(B.nets);
const allPins = new Set([...pa.keys(), ...pb.keys()]);
for (const pin of [...allPins].sort()) {
  const na = pa.get(pin) ?? '(unconnected)';
  const nb = pb.get(pin) ?? '(unconnected)';
  if (na !== nb) console.log(`${pin}\t${na}\t->\t${nb}`);
}

console.log(`\n=== NET-LEVEL DIFF (${labelA} -> ${labelB}) ===`);
const aNets = new Set(Object.keys(A.nets));
const bNets = new Set(Object.keys(B.nets));
console.log('--- nets only in current (ADDED) ---');
for (const n of [...bNets].filter((x) => !aNets.has(x)).sort()) console.log(`+ ${n} (${B.nets[n].count}) ${JSON.stringify(B.nets[n].nodes)}`);
console.log('--- nets only in baseline (REMOVED) ---');
for (const n of [...aNets].filter((x) => !bNets.has(x)).sort()) console.log(`- ${n} (${A.nets[n].count}) ${JSON.stringify(A.nets[n].nodes)}`);
console.log('--- common nets with different node sets ---');
for (const n of [...aNets].filter((x) => bNets.has(x)).sort()) {
  const sa = new Set(A.nets[n].nodes);
  const sb = new Set(B.nets[n].nodes);
  const add = [...sb].filter((x) => !sa.has(x));
  const rem = [...sa].filter((x) => !sb.has(x));
  if (add.length || rem.length) console.log(`~ ${n}\n    +added: ${JSON.stringify(add)}\n    -removed: ${JSON.stringify(rem)}`);
}
