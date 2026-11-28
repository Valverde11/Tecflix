
import getpass
import json
import os
import uuid
import urllib.error
import urllib.request

A = "http://127.0.0.1:8181"
B = "http://192.168.64.2:8182"

SERVICE_HEADER = os.environ.get("TECFLIX_SERVICE_HEADER", "")
if not SERVICE_HEADER:
    raise SystemExit(
        "Define TECFLIX_SERVICE_HEADER con el nombre real del encabezado"
    )

service_token = getpass.getpass(
    "Valor completo del encabezado de servicio: "
)

prefix = "ha_quota_" + uuid.uuid4().hex[:8]
username = prefix
email = prefix + "@example.test"
password = "TecflixV2_TestPassword2026!"

def request(base, method, path, body=None, headers=None):
    request_headers = {"Content-Type": "application/json"}
    if headers:
        request_headers.update(headers)

    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")

    req = urllib.request.Request(
        base + path,
        data=data,
        headers=request_headers,
        method=method
    )

    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            payload = response.read()
            return response.status, (
                json.loads(payload) if payload else {}
            )
    except urllib.error.HTTPError as error:
        payload = error.read()
        try:
            result = json.loads(payload) if payload else {}
        except json.JSONDecodeError:
            result = {"message": payload.decode(errors="replace")}
        return error.code, result

def check(condition, message):
    print(("OK" if condition else "FALLO") + ": " + message)
    if not condition:
        raise AssertionError(message)

created_id = None
jwt = None
completed = False

try:
    status, body = request(A, "POST", "/api/users", {
        "username": username,
        "email": email,
        "password": password
    })

    check(status == 201, f"Registro A: HTTP {status}")
    created_id = body["id"]

    status, body = request(B, "POST", "/api/auth/login", {
        "username": username,
        "password": password
    })

    check(
        status == 200 and bool(body.get("token")),
        f"Login B: HTTP {status}"
    )
    jwt = body["token"]

    route = f"/api/users/{created_id}/quota/usage"
    headers = {SERVICE_HEADER: service_token}

    op_reserve = prefix + "_reserve"
    op_release = prefix + "_release"

    def quota(base, delta, operation):
        return request(
            base,
            "POST",
            route,
            {
                "deltaBytes": delta,
                "operationId": operation
            },
            headers
        )

    print("\nPRUEBA 1: Reservar desde A")
    status, body = quota(A, 400, op_reserve)
    check(status == 200, f"HTTP {status}; {body}")
    check(body.get("usedBytes") == 400, "Consumo = 400")

    print("\nPRUEBA 2: Repetir desde B")
    status, body = quota(B, 400, op_reserve)
    check(status == 200, f"HTTP {status}; {body}")
    check(body.get("usedBytes") == 400, "Sin doble cobro")

    print("\nPRUEBA 3: Reutilizar ID con delta distinto")
    status, body = quota(B, 600, op_reserve)
    check(status == 409, f"Conflicto HTTP {status}; {body}")

    print("\nPRUEBA 4: Liberar desde B")
    status, body = quota(B, -200, op_release)
    check(status == 200, f"HTTP {status}; {body}")
    check(body.get("usedBytes") == 200, "Consumo = 200")

    print("\nPRUEBA 5: Repetir liberacion desde A")
    status, body = quota(A, -200, op_release)
    check(status == 200, f"HTTP {status}; {body}")
    check(body.get("usedBytes") == 200, "Sin doble liberacion")

    completed = True
    print("\nTODAS LAS PRUEBAS DE CUOTAS PASARON")

finally:
    if created_id is not None and jwt:
        status, body = request(
            A,
            "DELETE",
            f"/api/users/{created_id}",
            headers={"Authorization": f"Bearer {jwt}"}
        )
        print(f"\nLimpieza de usuario {created_id}: HTTP {status}")
        if status != 204:
            print("Revisar limpieza manual del usuario:", created_id)
    elif created_id is not None:
        print("\nUsuario pendiente de limpieza:", created_id)
        print("Username:", username)
