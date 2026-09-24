import { readFileSync } from 'node:fs';

const file = process.argv[2];
const lines = readFileSync(file, 'utf8').split(/\r?\n/);

const components = {};
const nets = {};
let mode = null;
let cur = null;

for (let i = 0; i < lines.length; i++) {
  const raw = lines[i];
  const t = raw.trim();
  if (t === '[') { mode = 'comp'; cur = {}; continue; }
  if (t === ']') { if (cur && cur.DESIGNATOR) components[cur.DESIGNATOR] = cur; mode = null; cur = null; continue; }
  if (t === '(') { mode = 'net'; cur = { name: null, nodes: [] }; continue; }
  if (t === ')') { if (cur && cur.name) nets[cur.name] = cur.nodes; mode = null; cur = null; continue; }
  if (mode === 'comp') {
    // key/value: value is the next line
    if (['DESIGNATOR','FOOTPRINT','PARTTYPE','Value'].includes(t)) {
      cur[t] = (lines[i+1] || '').trim();
      i++;
    }
  } else if (mode === 'net') {
    if (cur.name === null) { cur.name = t; continue; }
    if (t) {
      const parts = t.split(/\s+/);
      cur.nodes.push(parts[0]);
    }
  }
}

console.log('COMPONENT COUNT:', Object.keys(components).length);
for (const [d, c] of Object.entries(components)) {
  console.log(`  ${d}\t${c.PARTTYPE || ''}\t${c.FOOTPRINT || ''}\t${c.Value || ''}`);
}

console.log('\nNET COUNT:', Object.keys(nets).length);
const singles = [];
for (const [n, nodes] of Object.entries(nets)) {
  if (nodes.length <= 1) singles.push([n, nodes]);
}
console.log('\nSINGLE-NODE NETS (<=1 pin):');
for (const [n, nodes] of singles) console.log(`  ${n}: ${JSON.stringify(nodes)}`);

console.log('\nALL NETS (name : nodecount):');
for (const [n, nodes] of Object.entries(nets)) console.log(`  ${n} : ${nodes.length}`);
