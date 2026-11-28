#!/usr/bin/env python3
"""Verifica balanceo de una API en dos maquinas SIN modificar datos.

Ejemplos:
  python3 scripts/tecflix_v2_failover_check.py --service metadata --phase both
  python3 scripts/tecflix_v2_failover_check.py --service metadata --phase only-ubuntu
  python3 scripts/tecflix_v2_failover_check.py --service users --phase only-macos

El estado expected se comprueba contra los endpoints directos y el balanceador.
"""
import argparse
from collections import Counter
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

PORTS = {"users": (8081, 9281), "metadata": (8082, 9282), "streaming": (8083, 9283)}


def probe(url, timeout=3.0):
    try:
        with urlopen(Request(url, headers={"Connection": "close"}), timeout=timeout) as result:
            return result.status, result.headers.get("X-Tecflix-Backend", "")
    except HTTPError as error:
        return error.code, ""
    except (URLError, TimeoutError, OSError):
        return None, ""


def measure(lb, endpoint, samples, delay):
    counts = Counter()
    errors = []
    for _ in range(samples):
        code, backend = probe(lb.rstrip("/") + endpoint)
        if code == 200 and backend in ("macos", "ubuntu"):
            counts[backend] += 1
        else:
            errors.append((code, backend))
        time.sleep(delay)
    return counts, errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--service", choices=sorted(PORTS), default="metadata")
    parser.add_argument("--phase", choices=["both", "only-macos", "only-ubuntu", "restore"], required=True)
    parser.add_argument("--mac-host", default="127.0.0.1")
    parser.add_argument("--ubuntu-host", default="192.168.64.2")
    parser.add_argument("--lb-url", help="Override URL del balanceador")
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=25.0)
    args = parser.parse_args(argv)
    if args.samples < 4 or args.timeout < 3:
        parser.error("--samples >= 4 y --timeout >= 3 son obligatorios")
    backend_port, proxy_port = PORTS[args.service]
    path = "/ready" if args.service == "metadata" else "/health"
    lb = args.lb_url or f"http://127.0.0.1:{proxy_port}"
    mac = f"http://{args.mac_host}:{backend_port}{path}"
    ubuntu = f"http://{args.ubuntu_host}:{backend_port}{path}"
    mac_code, _ = probe(mac)
    ubuntu_code, _ = probe(ubuntu)
    print(f"macOS   ({mac}): HTTP {mac_code or 'sin conexión'}")
    print(f"Ubuntu  ({ubuntu}): HTTP {ubuntu_code or 'sin conexión'}")
    expected = ({"macos", "ubuntu"} if args.phase in ("both", "restore") else
                {"macos"} if args.phase == "only-macos" else {"ubuntu"})
    direct = {"macos": mac_code == 200, "ubuntu": ubuntu_code == 200}
    # Si no hay caída observable, fallar para evitar evidencias engañosas.
    if any(direct[node] != (node in expected) for node in direct):
        parser.error(f"Estado directo distinto a {sorted(expected)}; verifica qué proceso apagaste")
    last = None
    until = time.monotonic() + args.timeout
    while time.monotonic() < until:
        counts, errors = measure(lb, path, args.samples, .08)
        last = (dict(counts), errors)
        if not errors and all(counts[n] > 0 for n in expected) and all(
            counts[n] == 0 for n in direct if n not in expected
        ):
            print(f"✓ Balanceador conforme a {sorted(expected)}: {dict(counts)}")
            print("RESULTADO: OK")
            return 0
        time.sleep(.2)
    print(f"RESULTADO: FALLÓ. Último muestreo: {last}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
