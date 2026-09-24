import { DatabaseSync } from 'node:sqlite';
const file = process.argv[2];
const db = new DatabaseSync(file);

function show(label, sql, maxLen = 1200) {
  console.log(`\n===== ${label} =====`);
  console.log('SQL:', sql);
  try {
    const rows = db.prepare(sql).all();
    for (const r of rows) {
      const out = {};
      for (const [k, v] of Object.entries(r)) {
        if (typeof v === 'string') out[k] = v.length > maxLen ? v.slice(0, maxLen) + `...(len=${v.length})` : v;
        else if (v instanceof Uint8Array) out[k] = `<blob len=${v.length}>`;
        else out[k] = v;
      }
      console.log(JSON.stringify(out, null, 1));
    }
  } catch (e) { console.log('ERR', e.message); }
}

show('projects', 'SELECT uuid,name,length(content) AS content_len,substr(content,1,600) AS content_head FROM projects');
show('project_structures', 'SELECT id,ticket,project_uuid,branch_uuid,length(structure) AS len,substr(structure,1,600) AS head FROM project_structures');
show('history_data count/len', 'SELECT id,history_uuid,length(dataStr) AS len,substr(dataStr,1,120) AS head FROM history_data');
show('projects.boards', 'SELECT uuid,length(boards) AS len,substr(boards,1,600) AS head FROM projects');
db.close();
