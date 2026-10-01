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
    if (t === ')') { if (cur && cur.name) nets[cur.name] = cur.nodes; mode = null; cur = null; continue; }
    if (mode === 'comp') {
      if (pendingKey !== null) { cur[pendingKey] = t; pendingKey = null; continue; }
      if (t) pendingKey = t;
    } else if (mode === 'net') {
      if (cur.name === null) { cur.name = t; continue; }
      if (t) { cur.nodes.push(t.split(/\s+/)[0]); }
    }
  }
  return { components, nets };
}

const { components, nets } = parseNetlist(process.argv[2]);
console.log('===== COMPONENTS =====');
for (const [d, c] of Object.entries(components)) {
  console.log(`${d}\t${(c.PARTTYPE || '').padEnd(22)}\t${(c.Value || c.Name || '').padEnd(14)}\t${c.FOOTPRINT || ''}`);
}
console.log('\n===== NETS =====');
for (const n of Object.keys(nets).sort()) {
  console.log(`\n[${n}] (${nets[n].length})`);
  console.log('  ' + nets[n].sort().join('  '));
}
