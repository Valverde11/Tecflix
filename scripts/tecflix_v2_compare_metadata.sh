#!/usr/bin/env bash
# Comparar dos entradas del mismo proxy: una fija al Mac y otra round-robin.
set -euo pipefail
cd "$(dirname "$0")/.."
config=$(mktemp /tmp/tecflix-benchmark.XXXXXX)
name="tecflix-v2-bench-$(openssl rand -hex 6)"
trap 'docker stop "$name" >/dev/null 2>&1 || true; rm -f "$config"' EXIT
cat > "$config" <<'CFG'
global
    maxconn 1000
defaults
    mode http
    timeout connect 3s
    timeout client 15s
    timeout server 15s
frontend one
    bind *:9282
    default_backend one_backend
frontend two
    bind *:9382
    default_backend two_backends
backend one_backend
    option httpchk GET /ready
    http-check expect status 200
    http-response set-header X-Tecflix-Backend %[srv_name]
    server macos tecflix-macos:8082 check inter 1s
backend two_backends
    balance roundrobin
    option httpchk GET /ready
    http-check expect status 200
    http-response set-header X-Tecflix-Backend %[srv_name]
    server macos tecflix-macos:8082 check inter 1s
    server ubuntu tecflix-ubuntu:8082 check inter 1s
CFG
chmod 644 "$config"
docker run --rm -d --name "$name" -p 127.0.0.1::9282 -p 127.0.0.1::9382 \
    --add-host "tecflix-macos:${TECFLIX_MAC_ADDRESS:-host-gateway}" \
    --add-host "tecflix-ubuntu:${TECFLIX_UBUNTU_ADDRESS:-192.168.64.2}" \
    -v "$config:/usr/local/etc/haproxy/haproxy.cfg:ro" haproxy:3.2 >/dev/null
p1=$(docker port "$name" 9282/tcp)
p2=$(docker port "$name" 9382/tcp)
sleep 4
result=0
for concurrency in 5 15 30 60; do
    for repeat in 1 2 3; do
        for design in one two; do
            if [[ "$design" == one ]]; then port="${p1##*:}"; else port="${p2##*:}"; fi
            label="$name-$design-$concurrency-$repeat"
            echo "$label"
            python3 scripts/tecflix_v2_load_probe.py --label "$label" --requests 300 \
                --concurrency "$concurrency" --timeout 4 \
                --url "http://127.0.0.1:$port/api/metadata/videos?page=1&page_size=20" || result=1
        done
    done
done
exit "$result"
