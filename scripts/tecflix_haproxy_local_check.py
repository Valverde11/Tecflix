#!/usr/bin/env python3
"""Comprueba distribución y failover del laboratorio local de Tecflix Metadata.

No utiliza secretos ni modifica MongoDB. Requiere que HAProxy esté en :9082.
  python3 scripts/tecflix_haproxy_local_check.py --phase both
  # Apagar proceso metadata A :8082; mantener B :8182
  python3 scripts/tecflix_haproxy_local_check.py --phase only-b
  # Reiniciar A :8082
  python3 scripts/tecflix_haproxy_local_check.py --phase restore
"""
import argparse
from collections import Counter
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

LB = "http://127.0.0.1:9082/health"
A = "http://127.0.0.1:8082/health"
B = "http://127.0.0.1:8182/health"


def health(url: str):
    try:
        with urlopen(Request(url, headers={"Connection": "close"}), timeout=2) as response:
            return response.status, response.headers.get("X-Tecflix-Backend", "")
    except (HTTPError, URLError, OSError, TimeoutError) as error:
        return 0, str(error)


def sample(n=12, delay=.15):
    counts = Counter()
    failures = []
    for _ in range(n):
        code, server = health(LB)
        if code == 200 and server in ("metadata-A", "metadata-B"):
            counts[server] += 1
        else:
            failures.append((code, server))
        time.sleep(delay)
    return counts, failures


def await_ready(predicate, label, timeout=15):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        counts, failures = sample(n=6, delay=.15)
        last = (counts, failures)
        if predicate(counts, failures):
            print(f"  ✓ {label}: {dict(counts)}")
            return True
        time.sleep(.5)
    print(f"  ✗ {label}. Último muestreo: {last}", file=sys.stderr)
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", required=True, choices=("both", "only-b", "restore"))
    args = parser.parse_args()

    a_code, _ = health(A)
    b_code, _ = health(B)
    print(f"Metadata A (:8082): {'disponible' if a_code == 200 else 'no disponible'}")
    print(f"Metadata B (:8182): {'disponible' if b_code == 200 else 'no disponible'}")

    if args.phase in ("both", "restore"):
        if a_code != 200 or b_code != 200:
            parser.error("Ambas APIs deben responder HTTP 200 en /health para esta fase")
        ok = await_ready(lambda c, f: not f and c['metadata-A'] > 0 and c['metadata-B'] > 0,
                         "HAProxy distribuye solicitudes entre A y B")
    else:
        if a_code == 200 or b_code != 200:
            parser.error("Para only-b, detén A (:8082) y mantén B (:8182) disponible")
        ok = await_ready(lambda c, f: not f and c['metadata-B'] == 6 and c['metadata-A'] == 0,
                         "HAProxy solo utiliza B después de la caída de A")
    print("RESULTADO: OK" if ok else "RESULTADO: FALLÓ")
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
