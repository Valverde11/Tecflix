
import concurrent.futures
import json
import time
import urllib.error
import urllib.request
import uuid

A = "http://127.0.0.1:8181"
B = "http://192.168.64.2:8182"

PREFIX = "ha_" + uuid.uuid4().hex[:10]
PASSWORD = "TecflixV2_TestPassword2026!"

created_ids = []

def register(base, username, email):
    data = json.dumps({
        "username": username,
        "email": email,
        "password": PASSWORD
    }).encode("utf-8")

    req = urllib.request.Request(
        base + "/api/users",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST"
    )

    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.loads(
            error.read().decode("utf-8")
        )

def simultaneous(tasks):
    with concurrent.futures.ThreadPoolExecutor(
        max_workers=len(tasks)
    ) as pool:
        futures = [
            pool.submit(register, *task)
            for task in tasks
        ]
        return [f.result() for f in futures]

def show_results(name, results, expected):
    statuses = sorted(status for status, _ in results)

    for status, body in results:
        print(
            f"  HTTP {status} | "
            f"id={body.get('id', '-')} | "
            f"error={body.get('error', '-')}"
        )
        if status == 201:
            created_ids.append(body["id"])

    print("  Resultado:", "OK" if statuses == expected else "FALLO")
    return statuses == expected

all_ok = True

print("\nPRUEBA 1: Mismo username en ambas instancias")

results = simultaneous([
    (A, PREFIX + "_same", PREFIX + "_a@example.test"),
    (B, PREFIX + "_same", PREFIX + "_b@example.test"),
])

all_ok &= show_results(
    "Username duplicado", results, [201, 409]
)

print("\nPRUEBA 2: Mismo email en ambas instancias")

results = simultaneous([
    (A, PREFIX + "_one", PREFIX + "_same@example.test"),
    (B, PREFIX + "_two", PREFIX + "_same@example.test"),
])

all_ok &= show_results(
    "Email duplicado", results, [201, 409]
)

print("\nPRUEBA 3: Usuarios diferentes simultáneos")

tasks = [
    (
        A if i % 2 == 0 else B,
        f"{PREFIX}_{i}",
        f"{PREFIX}_{i}@example.test"
    )
    for i in range(10)
]

results = simultaneous(tasks)

all_ok &= show_results(
    "Registros independientes",
    results,
    [201] * 10
)

print("\nPRUEBA 4: Identificadores únicos")

unique_ids = len(created_ids) == len(set(created_ids))
print("IDs registrados:", len(created_ids))
print("IDs únicos:", len(set(created_ids)))
print("Resultado:", "OK" if unique_ids else "FALLO")

all_ok &= unique_ids

print("\nRESUMEN")
print("Usuarios creados:", created_ids)
print("Prefijo:", PREFIX)
print("Estado:", "TODAS LAS PRUEBAS PASARON" if all_ok else "FALLO")

raise SystemExit(0 if all_ok else 1)

