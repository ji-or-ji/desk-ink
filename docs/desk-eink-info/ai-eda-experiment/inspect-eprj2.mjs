import { DatabaseSync } from 'node:sqlite';

const file = process.argv[2];
const needle = process.argv[3] || null;
const db = new DatabaseSync(file);

const tables = db.prepare("SELECT name FROM sqlite_master WHERE type='table'").all();
console.log('TABLES:', JSON.stringify(tables.map(t => t.name)));

for (const t of tables) {
  const name = t.name;
  let cols;
  try { cols = db.prepare(`PRAGMA table_info("${name}")`).all(); } catch { continue; }
  const colNames = cols.map(c => c.name);
  console.log(`\n== TABLE ${name} cols=${JSON.stringify(colNames)} ==`);
  let rows;
  try { rows = db.prepare(`SELECT * FROM "${name}"`).all(); } catch (e) { console.log('  read err', e.message); continue; }
  console.log('  rowcount=', rows.length);
  if (needle) {
    for (const r of rows) {
      const s = JSON.stringify(r);
      if (s.includes(needle)) {
        console.log('  MATCH:', s.length > 4000 ? s.slice(0, 4000) : s);
      }
    }
  }
}
db.close();
