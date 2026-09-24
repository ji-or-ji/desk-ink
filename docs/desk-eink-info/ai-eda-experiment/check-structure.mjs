import { DatabaseSync } from 'node:sqlite';
const file = process.argv[2];
const db = new DatabaseSync(file);
const rows = db.prepare('SELECT id, structure FROM project_structures').all();
for (const r of rows) {
  console.log(`id=${r.id} hasBoard1=${r.structure.includes('"title":"Board1"')} hasAIBoard=${r.structure.includes('AI_EXTERNAL_BOARD')}`);
}
db.close();
