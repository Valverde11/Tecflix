// Baseline for the routes NOT covered by load-tests/scenarios/load.js
// (that journey only issues read-only GETs on a fixed catalog page).
//
// Scenarios (selected with -e SCENARIO=...):
//   login    - POST /api/auth/login        (PBKDF2 password verification)
//   register - POST /api/users             (PBKDF2 hash + B-tree insert on disk)
//   catalog  - GET  /api/metadata/videos   (every sort field, shallow and deep pages)
//   create   - POST /api/metadata/videos   (MongoDB insert)
//   all      - the four above, at the same time
//
// Reuses the config written by performance/scripts/seed_loadtest.py:
//   k6 run -e LOADTEST_CONFIG=$HOME/tecflix-env/loadtest/single.local.json \
//          -e PASSWORD_FILE=$HOME/tecflix-env/loadtest/password.txt \
//          -e SCENARIO=catalog -e VUS=16 -e DURATION=60s performance/k6/baseline_extra.js
import http from 'k6/http';
import { check } from 'k6';
import exec from 'k6/execution';

const config = JSON.parse(open(__ENV.LOADTEST_CONFIG));
const password = open(__ENV.PASSWORD_FILE).trim();
const users = config.credentials.users;
const base = config.services;
const vus = Number(__ENV.VUS || 8);
const duration = __ENV.DURATION || '60s';
const which = __ENV.SCENARIO || 'all';
const runTag = __ENV.RUN_TAG || 'run';
// Usernames allow only letters, digits and "_": derive a unique suffix per execution.
const runId = `${Date.now().toString(36)}`;

const json = { 'Content-Type': 'application/json' };
const auth = (user) => ({ Authorization: `Bearer ${user.bearerToken}` });
const vuUser = () => users[(exec.vu.idInTest - 1) % users.length];
const SORTS = ['uploaded_at', 'title', 'author'];

const all = {
  login: { exec: 'login' },
  register: { exec: 'register' },
  catalog: { exec: 'catalog' },
  create: { exec: 'create' },
};
const selected = which === 'all' ? Object.keys(all) : [which];
const scenarios = {};
for (const name of selected) {
  if (!all[name]) throw new Error(`Unknown SCENARIO ${name}`);
  scenarios[name] = { executor: 'constant-vus', vus, duration, exec: all[name].exec };
}

export const options = {
  scenarios,
  summaryTrendStats: ['avg', 'min', 'med', 'max', 'p(90)', 'p(95)', 'p(99)'],
};

export function login() {
  const index = (exec.vu.idInTest - 1) % users.length;
  const username = `loadtest_${String(index + 1).padStart(3, '0')}`;
  const res = http.post(`${base.users}/api/auth/login`,
    JSON.stringify({ username, password }), { headers: json, tags: { name: 'POST /api/auth/login' } });
  check(res, { 'login 200': (r) => r.status === 200 });
}

export function register() {
  const name = `lt${runId}_${exec.vu.idInTest}_${exec.vu.iterationInScenario}`;
  const res = http.post(`${base.users}/api/users`,
    JSON.stringify({ username: name, email: `${name}@loadtest.invalid`, password }),
    { headers: json, tags: { name: 'POST /api/users' } });
  check(res, { 'register 201': (r) => r.status === 201 });
}

export function catalog() {
  const user = vuUser();
  const sort = SORTS[exec.vu.iterationInScenario % SORTS.length];
  // Alternate a first page and a deep page (~page 50 of 100 at size 20 for 2000 docs).
  const deep = exec.vu.iterationInScenario % 2 === 1;
  const page = deep ? 50 : 1;
  const res = http.get(
    `${base.metadata}/api/metadata/videos?page=${page}&page_size=20&sort=${sort}`,
    { headers: auth(user), tags: { name: `GET catalog sort=${sort} ${deep ? 'deep' : 'first'}` } });
  check(res, { 'catalog 200': (r) => r.status === 200 });
}

export function create() {
  const user = vuUser();
  const hex = '0123456789abcdef';
  let id = '';
  for (let i = 0; i < 32; i += 1) id += hex[Math.floor(Math.random() * 16)];
  const res = http.post(`${base.metadata}/api/metadata/videos`, JSON.stringify({
    id, title: `carga ${runTag} ${id.slice(0, 6)}`, topic: 'tecnologia',
    tags: ['loadtest'], duration_seconds: 60, size_bytes: 1000, visibility: 'private',
  }), { headers: { ...json, ...auth(user) }, tags: { name: 'POST /api/metadata/videos' } });
  check(res, { 'create 201': (r) => r.status === 201 || r.status === 200 });
}
