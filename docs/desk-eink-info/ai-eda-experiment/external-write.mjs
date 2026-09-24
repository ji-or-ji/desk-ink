import { DatabaseSync } from 'node:sqlite';
const file = process.argv[2];
const db = new DatabaseSync(file);

const before = db.prepare('SELECT id, length(structure) AS len, structure FROM project_structures').all();
for (const r of before) console.log(`BEFORE id=${r.id} len=${r.len} hasBoard1=${r.structure.includes('"title":"Board1"')}`);

const info = db.prepare(`UPDATE project_structures SET structure = replace(structure, '"title":"Board1"', '"title":"AI_EXTERNAL_BOARD"')`).run();
console.log('UPDATE changes:', info.changes);

const after = db.prepare('SELECT id, length(structure) AS len, structure FROM project_structures').all();
for (const r of after) console.log(`AFTER  id=${r.id} len=${r.len} hasAIBoard=${r.structure.includes('AI_EXTERNAL_BOARD')}`);
db.close();
