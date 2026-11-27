#!/bin/bash
# Runs the V1 performance baseline (or the "after" measurement) with k6.
#
#   performance/scripts/run_baseline.sh <label>
#
# Requires: the 3 APIs running, k6 on PATH, the seeded config from
# seed_loadtest.py, and the team's load-tests folder (branch Load_Test) at
# $LOADTEST_DIR (default ~/tecflix-loadtests/load-tests).
# Writes k6 summaries, a run index (start/end timestamps per run) and process
# CPU/RSS samples to performance/results/<label>/.
set -euo pipefail

LABEL=${1:?usage: run_baseline.sh <label>}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
LOADTEST_DIR=${LOADTEST_DIR:-$HOME/tecflix-loadtests/load-tests}
CFG=${LOADTEST_CONFIG:-$HOME/tecflix-env/loadtest/single.local.json}
PWFILE=${PASSWORD_FILE:-$HOME/tecflix-env/loadtest/password.txt}
DUR=${DURATION:-60s}
OUT="$REPO/performance/results/$LABEL"
mkdir -p "$OUT"

# Same config with no think time = closed-loop saturation.
CFG0="${CFG%.json}.think0.json"
python3 -c "import json,sys; c=json.load(open(sys.argv[1])); c['thinkTimeSeconds']=0; json.dump(c,open(sys.argv[2],'w'))" "$CFG" "$CFG0"
chmod 600 "$CFG0"

[ -f "$OUT/runs.csv" ] || echo "run,scenario,vus,think,start_epoch,end_epoch" > "$OUT/runs.csv"
ONLY=${ONLY:-}   # optional regex: run only matching run names (e.g. ONLY='^catalog_')
[ -f "$OUT/process_samples.csv" ] || \
  echo "epoch,run,service,pid,cpu_percent,rss_kb,cpu_seconds" > "$OUT/process_samples.csv"
TICKS=$(getconf CLK_TCK)

sample_processes() { # run_name
  while true; do
    now=$(date +%s)
    for pair in users:tecflix-build/users/user-service metadata:tecflix-build/metadata/metadata-service streaming:tecflix-build/streaming/tecflix-api; do
      svc=${pair%%:*}; pat=${pair#*:}
      pid=$(pgrep -f "$pat" | head -1 || true)
      [ -n "$pid" ] || continue
      # utime+stime (fields 14,15 of /proc/pid/stat) = cumulative CPU time.
      cpu_s=$(awk -v t="$TICKS" '{printf "%.2f", ($14+$15)/t}' "/proc/$pid/stat")
      rss=$(ps -o rss= -p "$pid" | tr -d ' ')
      echo "$now,$1,$svc,$pid,,$rss,$cpu_s" >> "$OUT/process_samples.csv"
    done
    sleep 2
  done
}

run() { # name scenario vus think k6-args...
  local name=$1 scenario=$2 vus=$3 think=$4; shift 4
  if [ -n "$ONLY" ] && ! [[ $name =~ $ONLY ]]; then return; fi
  # Re-running a scenario replaces its previous row in the index.
  sed -i "/^$name,/d" "$OUT/runs.csv"
  echo ">>> $name ($scenario, $vus VUs, think=$think, $DUR)"
  sample_processes "$name" & local sampler=$!
  local start; start=$(date +%s)
  k6 run --quiet --no-color --summary-export "$OUT/$name.json" "$@" > "$OUT/$name.txt" 2>&1 || true
  local end; end=$(date +%s)
  kill "$sampler" 2>/dev/null || true
  echo "$name,$scenario,$vus,$think,$start,$end" >> "$OUT/runs.csv"
  grep -E 'http_req_duration|http_reqs|http_req_failed|iterations' "$OUT/$name.txt" | head -4 || true
  sleep 20   # let one Prometheus scrape (15s) close the window cleanly
}

cd "$LOADTEST_DIR"
k6 run --quiet -e "LOADTEST_CONFIG=$CFG" scenarios/smoke.js > "$OUT/smoke.txt" 2>&1 \
  || { echo "smoke failed, see $OUT/smoke.txt"; exit 1; }

# 1) Team read journey (7 GETs across the 3 APIs), realistic 1 s think time.
for v in 1 8 32 64; do
  run "journey_think1_v$v" journey "$v" 1 -e "LOADTEST_CONFIG=$CFG" -e VUS=$v -e DURATION=$DUR scenarios/load.js
done
# 2) Same journey without think time: maximum throughput per concurrency level.
for v in 8 32 64; do
  run "journey_think0_v$v" journey "$v" 0 -e "LOADTEST_CONFIG=$CFG0" -e VUS=$v -e DURATION=$DUR scenarios/load.js
done
# 3) Routes the journey does not exercise. "create" runs last and its documents
#    are removed afterwards so every run sees the same catalog size.
for s in login catalog create; do
  for v in 8 32; do
    run "${s}_v$v" "$s" "$v" 0 -e "LOADTEST_CONFIG=$CFG" -e "PASSWORD_FILE=$PWFILE" \
      -e SCENARIO=$s -e VUS=$v -e DURATION=$DUR -e RUN_TAG=$LABEL "$REPO/performance/k6/baseline_extra.js"
  done
done
run "register_v8" register 8 0 -e "LOADTEST_CONFIG=$CFG" -e "PASSWORD_FILE=$PWFILE" \
  -e SCENARIO=register -e VUS=8 -e DURATION=$DUR -e RUN_TAG=$LABEL "$REPO/performance/k6/baseline_extra.js"

# Remove the synthetic documents inserted by the "create" scenario (title "carga ...").
docker exec tecflix-mongo mongosh --quiet tecflix_metadata \
  --eval 'printjson(db.videos.deleteMany({title: /^carga /}).deletedCount)' || true

echo "Listo: $OUT"
