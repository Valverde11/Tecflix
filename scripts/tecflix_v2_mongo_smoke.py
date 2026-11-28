#!/usr/bin/env python3
"""Prueba de contrato HTTP y persistencia de metadata Tecflix.

Solo crea un video de prueba con ID aleatorio y procura borrarlo al terminar.
Si --verify-mongo está habilitado, verifica también la colección Mongo real.
El JWT de prueba se firma con AUTH_SECRET del entorno; no usa usuarios reales.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import os
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any


HEADER = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"


def b64url(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def test_token(secret: str, owner_id: str) -> str:
    payload = {"sub": owner_id, "username": "tecflix_smoke", "exp": int(time.time()) + 900}
    signing_input = HEADER + "." + b64url(json.dumps(payload, separators=(",", ":")).encode())
    signature = hmac.new(secret.encode(), signing_input.encode(), hashlib.sha256).digest()
    return signing_input + "." + b64url(signature)


def call(base: str, method: str, path: str, token: str | None = None,
         body: dict[str, Any] | None = None) -> tuple[int, Any]:
    headers = {"Accept": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(base.rstrip("/") + path, data=data,
                                 method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=12) as response:
            code, content = response.status, response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        code, content = error.code, error.read().decode("utf-8", errors="replace")
    except urllib.error.URLError as error:
        raise RuntimeError(f"No se pudo conectar a {base}: {error.reason}") from error
    except TimeoutError as error:
        raise RuntimeError(f"La conexión a {base} excedió el tiempo de espera") from error
    try:
        result = json.loads(content) if content else None
    except json.JSONDecodeError:
        result = content[:200]
    return code, result


def expect(base: str, method: str, path: str, expected: int, token: str | None = None,
           body: dict[str, Any] | None = None) -> Any:
    code, result = call(base, method, path, token, body)
    if code != expected:
        raise AssertionError(f"{method} {base}{path}: esperado HTTP {expected}, "
                             f"recibido {code}: {result!r}")
    print(f"  ✓ {method} {path} -> HTTP {code}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Tecflix: smoke test de metadata y MongoDB")
    parser.add_argument("--url", default="http://127.0.0.1:8082",
                        help="URL de la primera API de metadata (default: :8082)")
    parser.add_argument("--peer-url", help="URL de una segunda instancia de metadata")
    parser.add_argument("--verify-mongo", action="store_true",
                        help="Comprueba directamente que los documentos están en MongoDB (requiere pymongo)")
    args = parser.parse_args()
    secret = os.environ.get("AUTH_SECRET", "")
    if len(secret.encode()) < 32:
        parser.error("exporta AUTH_SECRET (mínimo 32 bytes) antes de probar")

    db_collection = None
    if args.verify_mongo:
        uri = os.environ.get("MONGO_URI")
        database = os.environ.get("MONGO_DB", "tecflix_metadata")
        if not uri:
            parser.error("--verify-mongo necesita MONGO_URI en el entorno")
        try:
            from pymongo import MongoClient  # type: ignore[import-not-found]
        except ImportError:
            parser.error("Falta pymongo. Instalar con: python3 -m pip install pymongo")
        try:
            client = MongoClient(uri, serverSelectionTimeoutMS=4000)
            client.admin.command("ping")
            db_collection = client[database]["videos"]
        except Exception as error:
            parser.error("No se pudo conectar a MongoDB: " + type(error).__name__ +
                         ". Revisa MONGO_URI, red, credenciales y servicio mongod")
        print("  ✓ Conexión a MongoDB (ping)")

    base = args.url.rstrip("/")
    peer = args.peer_url.rstrip("/") if args.peer_url else base
    owner_id = str(1_000_000_000 + secrets.randbelow(8_000_000_000))
    token = test_token(secret, owner_id)
    video_id = secrets.token_hex(16)
    video_path = f"/api/metadata/videos/{video_id}"
    create_path = "/api/metadata/videos"
    title = "SPOF smoke " + video_id
    payload = {"id": video_id, "title": title, "description": "prueba automatizada efimera",
               "topic": "testing", "tags": ["spof-test"], "duration_seconds": 2,
               "size_bytes": 1234567, "visibility": "private"}
    created = False
    try:
        print("Probando contrato HTTP (los datos pertenecen a un usuario ficticio de prueba):")
        expect(base, "GET", "/health", 200)
        if peer != base:
            expect(peer, "GET", "/health", 200)
        expect(base, "GET", video_path, 404)
        first = expect(base, "POST", create_path, 201, token, payload)
        created = True
        assert first["id"] == video_id and first["owner_id"] == owner_id
        assert first["size_bytes"] == 1234567
        expect(base, "GET", video_path, 404)  # privado para anónimos
        own = expect(peer, "GET", video_path, 200, token)
        assert own["id"] == video_id, "La segunda instancia no ve el documento creado en A"
        retry = expect(base, "POST", create_path, 201, token, payload)
        assert retry["uploaded_at"] == first["uploaded_at"], "El POST idempotente cambió la fecha"
        usage = expect(peer, "GET", f"/api/metadata/users/{owner_id}/usage", 200, token)
        assert usage["video_count"] == 1 and usage["used_bytes"] == 1234567, usage
        if db_collection is not None:
            stored = db_collection.find_one({"_id": video_id})
            assert stored is not None, "La API respondió 201 pero el documento no está en MongoDB"
            assert stored["owner_id"] == owner_id and stored["size_bytes"] == 1234567
            print("  ✓ Documento insertado en MongoDB real")
        patched = expect(peer, "PATCH", video_path, 200, token,
                         {"title": title + " actualizado", "visibility": "public"})
        assert patched["visibility"] == "public"
        public = expect(base, "GET", video_path, 200)
        assert public["title"] == title + " actualizado"
        query = urllib.parse.urlencode({"page": 1, "page_size": 20, "q": video_id})
        listing = expect(base, "GET", "/api/metadata/videos?" + query, 200)
        # Busca el ID en el título (el título original contiene el ID hexadecimal).
        assert any(v["id"] == video_id for v in listing["items"]), listing
        if db_collection is not None:
            assert db_collection.find_one({"_id": video_id})["visibility"] == "public"
            print("  ✓ PATCH persistido en MongoDB real")
        expect(peer, "DELETE", video_path, 204, token)
        created = False
        expect(base, "GET", video_path, 404)
        if db_collection is not None:
            assert db_collection.find_one({"_id": video_id}) is None
            print("  ✓ DELETE persistido en MongoDB real")
        checked = "y persistencia directa en MongoDB" if db_collection is not None else "en HTTP"
        print("\nRESULTADO: OK. CRUD, privacidad e idempotencia " + checked + " verificados.")
        return 0
    except (AssertionError, RuntimeError, KeyError, TypeError, ValueError) as error:
        print(f"\nRESULTADO: FALLÓ: {error}", file=sys.stderr)
        print(f"ID del video de prueba: {video_id}", file=sys.stderr)
        return 1
    finally:
        if created:
            try:
                code, _ = call(base, "DELETE", video_path, token)
                if code not in (204, 404):
                    print(f"Advertencia: limpieza devolvió HTTP {code}; revisar {video_id}", file=sys.stderr)
            except Exception as error:
                print(f"Advertencia: no se pudo limpiar {video_id}: {error}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
