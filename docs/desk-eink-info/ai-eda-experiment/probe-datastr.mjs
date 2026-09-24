import { DatabaseSync } from 'node:sqlite';
const file = process.argv[2];
const db = new DatabaseSync(file);
const rows = db.prepare('SELECT id, length(dataStr) AS len, dataStr FROM history_data ORDER BY id').all();
for (const r of rows) {
  const s = r.dataStr || '';
  let b;
  try { b = Buffer.from(s, 'base64'); } catch { b = Buffer.alloc(0); }
  const hex = b.subarray(0, 8).toString('hex');
  const printable = b.subarray(0, 32).toString('latin1').replace(/[^\x20-\x7e]/g, '.');
  console.log(`id=${r.id} len=${r.len} b64len=${s.length} decodedLen=${b.length} headHex=${hex} headTxt=${printable}`);
}
db.close();
