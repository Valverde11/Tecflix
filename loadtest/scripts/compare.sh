#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LOADTEST_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
WORKLOAD="$LOADTEST_DIR/k6/workload.js"
RESULTS_DIR="$LOADTEST_DIR/results"

required_vars=(
  LOADTEST_USERNAME
  LOADTEST_PASSWORD
  LOADTEST_VIDEO_ID
  SINGLE_USERS_URL
  SINGLE_METADATA_URL
  SINGLE_STREAMING_URL
  DUAL_USERS_URL
  DUAL_METADATA_URL
  DUAL_STREAMING_URL
)

for name in "${required_vars[@]}"; do
  if ! printenv "$name" >/dev/null || [[ -z "$(printenv "$name")" ]]; then
    printf 'Falta la variable requerida: %s\n' "$name" >&2
    exit 2
  fi
done

if ! command -v k6 >/dev/null 2>&1; then
  printf 'No se encontró k6 en PATH. Instálelo antes de ejecutar la comparación.\n' >&2
  exit 127
fi
if ! command -v python3 >/dev/null 2>&1; then
  printf 'No se encontró python3 en PATH; se necesita para generar comparison.csv.\n' >&2
  exit 127
fi

PROFILE="${LOADTEST_PROFILE:-capacity}"
if [[ "$PROFILE" != "smoke" && "$PROFILE" != "capacity" ]]; then
  printf 'LOADTEST_PROFILE debe ser smoke o capacity.\n' >&2
  exit 2
fi

MAX_VUS="${LOADTEST_MAX_VUS:-20}"
if [[ ! "$MAX_VUS" =~ ^[0-9]+$ ]] || (( MAX_VUS < 1 || MAX_VUS > 50 )); then
  printf 'LOADTEST_MAX_VUS debe ser un entero entre 1 y 50.\n' >&2
  exit 2
fi

STAGE_DURATION="${LOADTEST_STAGE_DURATION_SECONDS:-30}"
if [[ "$PROFILE" == "capacity" ]] &&
  { [[ ! "$STAGE_DURATION" =~ ^[0-9]+$ ]] || (( STAGE_DURATION < 10 || STAGE_DURATION > 300 )); }; then
  printf 'LOADTEST_STAGE_DURATION_SECONDS debe ser un entero entre 10 y 300.\n' >&2
  exit 2
fi

COOLDOWN="${LOADTEST_COOLDOWN_SECONDS:-15}"
if [[ ! "$COOLDOWN" =~ ^[0-9]+$ ]] || (( COOLDOWN > 300 )); then
  printf 'LOADTEST_COOLDOWN_SECONDS debe ser un entero entre 0 y 300.\n' >&2
  exit 2
fi

levels=()
if [[ "$PROFILE" == "smoke" ]]; then
  levels=(1)
else
  for level in 5 10 20 30 40; do
    if (( level < MAX_VUS )); then
      levels+=("$level")
    fi
  done
  levels+=("$MAX_VUS")
fi

mkdir -p -- "$RESULTS_DIR"
RUN_DIR="$(mktemp -d "$RESULTS_DIR/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"

{
  printf 'profile=%s\n' "$PROFILE"
  printf 'max_vus=%s\n' "$MAX_VUS"
  printf 'stage_duration_seconds=%s\n' "$STAGE_DURATION"
  printf 'tested_vus=%s\n' "$(IFS=,; printf '%s' "${levels[*]}")"
  printf 'tecflix_commit=%s\n' \
    "$(git -C "$LOADTEST_DIR/.." rev-parse --short HEAD 2>/dev/null || printf unknown)"
  printf 'k6_version=%s\n' "$(k6 version 2>&1)"
  printf 'single_users_url=%s\n' "$SINGLE_USERS_URL"
  printf 'single_metadata_url=%s\n' "$SINGLE_METADATA_URL"
  printf 'single_streaming_url=%s\n' "$SINGLE_STREAMING_URL"
  printf 'dual_users_url=%s\n' "$DUAL_USERS_URL"
  printf 'dual_metadata_url=%s\n' "$DUAL_METADATA_URL"
  printf 'dual_streaming_url=%s\n' "$DUAL_STREAMING_URL"
  printf 'started_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >"$RUN_DIR/manifest.txt"

run_topology() {
  local topology="$1"
  local vus="$2"
  local profile="$3"
  local users_url="$4"
  local metadata_url="$5"
  local streaming_url="$6"
  local summary="$RUN_DIR/${topology}-${vus}vus-summary.json"

  printf '\n== Topología %s: %s VUs (%s) ==\n' "$topology" "$vus" "$PROFILE"
  k6 run \
    --include-system-env-vars \
    --env "LOADTEST_PROFILE=$profile" \
    --env "LOADTEST_MAX_VUS=$vus" \
    --env "LOADTEST_STAGE_DURATION_SECONDS=$STAGE_DURATION" \
    --env "USERS_URL=$users_url" \
    --env "METADATA_URL=$metadata_url" \
    --env "STREAMING_URL=$streaming_url" \
    --env "LOADTEST_RUN_LABEL=$topology" \
    --summary-export "$summary" \
    "$WORKLOAD"
}

failed=0
index=0
last_index=$((${#levels[@]} - 1))
set +e
for vus in "${levels[@]}"; do
  run_profile=fixed
  if [[ "$PROFILE" == "smoke" ]]; then
    run_profile=smoke
  fi

  run_topology single "$vus" "$run_profile" \
    "$SINGLE_USERS_URL" "$SINGLE_METADATA_URL" "$SINGLE_STREAMING_URL"
  single_status=$?
  printf 'single_%s_vus_exit_code=%s\n' "$vus" "$single_status" >>"$RUN_DIR/manifest.txt"
  if (( single_status != 0 )); then
    failed=1
  fi

  if (( COOLDOWN > 0 )); then
    printf '\n== Pausa de %s segundos antes de la topología dual ==\n' "$COOLDOWN"
    sleep "$COOLDOWN"
  fi

  run_topology dual "$vus" "$run_profile" \
    "$DUAL_USERS_URL" "$DUAL_METADATA_URL" "$DUAL_STREAMING_URL"
  dual_status=$?
  printf 'dual_%s_vus_exit_code=%s\n' "$vus" "$dual_status" >>"$RUN_DIR/manifest.txt"
  if (( dual_status != 0 )); then
    failed=1
  fi

  if (( COOLDOWN > 0 && index < last_index )); then
    printf '\n== Pausa de %s segundos antes del siguiente nivel ==\n' "$COOLDOWN"
    sleep "$COOLDOWN"
  fi
  index=$((index + 1))
done
python3 "$LOADTEST_DIR/scripts/report.py" "$RUN_DIR"
report_status=$?
if (( report_status != 0 )); then
  failed=1
fi
set -e

printf '\nResultados guardados en: %s\n' "$RUN_DIR"

if (( failed != 0 )); then
  printf 'Al menos una ejecución falló; revise cada resumen antes de comparar.\n' >&2
  exit 1
fi

printf 'Todas las ejecuciones finalizaron sin fallos de checks.\n'
