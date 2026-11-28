#!/usr/bin/env bash
# Promoción coordinada y continuidad de cuota en un clúster desechable.
set -euo pipefail
if [[ $# -gt 1 || (${1:-} != "" && ${1:-} != "--graceful") ]]; then
    echo "Uso: bash $0 [--graceful]" >&2
    exit 2
fi
cd "$(dirname "$0")/.."
export TECFLIX_LAB_PG_PASSWORD="$(openssl rand -hex 24)"
export TECFLIX_LAB_REPLICATION_PASSWORD="$(openssl rand -hex 24)"
project="tecflix-pg-$(openssl rand -hex 8)"
compose=(docker compose -f infraestructure/postgres-v2/ha-lab/compose.yaml -p "$project")
trap '"${compose[@]}" down --volumes' EXIT
"${compose[@]}" build pg1
"${compose[@]}" up -d --no-build
export PGUSER=postgres PGPASSWORD="$TECFLIX_LAB_PG_PASSWORD" PGCONNECT_TIMEOUT=2
export PGTARGETSESSIONATTRS=read-write PGOPTIONS='-c statement_timeout=10000'
export PGHOST=127.0.0.1,127.0.0.1,127.0.0.1 PGDATABASE=postgres
pg_ports=()
api_ports=()
for node in pg1 pg2 pg3; do
    address=$("${compose[@]}" port "$node" 5432)
    pg_ports+=("${address##*:}")
    address=$("${compose[@]}" port "$node" 8008)
    api_ports+=("${address##*:}")
done
export PGPORT="${pg_ports[0]},${pg_ports[1]},${pg_ports[2]}"

wait_for() {
    local expected="$1" output
    shift
    for ((attempt=0; attempt<90; attempt++)); do
        if output=$("$@" 2>/dev/null) && [[ "$output" == "$expected" ]]; then return; fi
        sleep 1
    done
    echo "No se obtuvo el resultado esperado: $expected" >&2
    return 1
}
sql() { psql --no-psqlrc --no-password --set ON_ERROR_STOP=1 -Atq "$@"; }
primary_node() {
    local index
    for index in 0 1 2; do
        if curl --noproxy '*' -fs --max-time 2 "http://127.0.0.1:${api_ports[$index]}/primary" >/dev/null; then
            echo "pg$((index + 1))"
            return
        fi
    done
    return 1
}
sync_node() {
    local index
    for index in 0 1 2; do
        if curl --noproxy '*' -fs --max-time 2 "http://127.0.0.1:${api_ports[$index]}/synchronous" >/dev/null; then
            echo yes
            return
        fi
    done
    return 1
}
sync_count() {
    sql -c "SELECT count(*) FROM pg_stat_replication WHERE state='streaming' AND sync_state='sync'"
}
no_writable_session() {
    if sql -c 'SELECT 1' >/dev/null 2>&1; then return 1; fi
    echo yes
}
wait_for 1 sync_count
wait_for yes sync_node
old=$(primary_node)
sql -c 'CREATE DATABASE tecflix_v2'
export PGDATABASE=tecflix_v2
sql -f infraestructure/postgres-v2/init/001_schema.sql -f infraestructure/postgres-v2/init/002_quota_transaction.sql
sql -c "INSERT INTO users (username,username_key,email,email_key,password_hash,salt,created_at)
        VALUES ('laboratorio','laboratorio','lab@example.test','lab@example.test',
                decode(repeat('00',32),'hex'),decode(repeat('00',16),'hex'),0)"
wait_for '400|f' sql -c "SELECT used_bytes, already_applied FROM apply_quota_operation(1,'lab-reserve',400)"
"${compose[@]}" stop etcd3
if [[ ${1:-} == "--graceful" ]]; then
    "${compose[@]}" stop "$old"
else
    "${compose[@]}" kill -s SIGKILL "$old"
fi
wait_for 1 sync_count
new=$(primary_node)
[[ "$old" != "$new" ]]
wait_for '400|t' sql -c "SELECT used_bytes, already_applied FROM apply_quota_operation(1,'lab-reserve',400)"
wait_for '200|f' sql -c "SELECT used_bytes, already_applied FROM apply_quota_operation(1,'lab-release',-200)"
"${compose[@]}" up -d --no-deps --no-build "$old"
wait_for 2 sql -c "SELECT count(*) FROM pg_stat_replication WHERE state='streaming'"
echo "PostgreSQL: $old → $new, cuota persistente e idempotente y antiguo primario reincorporado como réplica."
"${compose[@]}" stop etcd2 etcd3
wait_for yes no_writable_session
"${compose[@]}" start etcd2
wait_for 1 sync_count
wait_for '200' sql -c 'SELECT used_bytes FROM users WHERE id=1'
echo "Sin mayoría de etcd se retira el primario. Tras restablecer el quórum vuelve la sesión de escritura."
