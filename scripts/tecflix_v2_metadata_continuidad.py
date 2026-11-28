#!/usr/bin/env python3
"""Tecflix V2: prueba de continuidad de metadata entre procesos.

Uso (solo en entorno de desarrollo):
  --phase create --url http://127.0.0.1:8082
  # detener o reiniciar la primera API; iniciar la otra
  --phase verify --url http://127.0.0.1:8182
  # Si verify falla, revisar; limpieza manual opcional:
  --phase cleanup --url http://127.0.0.1:8182

El registro de prueba se elimina al verificar correctamente. El archivo de estado
se guarda en build/ (ignorado por .gitignore) y no contiene claves o tokens.
Requiere AUTH_SECRET de desarrollo exportado en el proceso.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import sys
import time
import urllib.error
import urllib.request


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def issue_token(secret: str, user_id: str) -> str:
    header = b64url(b'{"alg":"HS256","typ":"JWT"}')
    claims = {"sub": user_id, "username": "tecflix_spof_continuidad", "exp": int(time.time()) + 900}
    payload = b64url(json.dumps(claims, separators=(",", ":")).encode())
    signed = f"{header}.{payload}"
    signature = hmac.new(secret.encode(), signed.encode(), hashlib.sha256).digest()
    return signed + "." + b64url(signature)


def request(url: str, method: str, route: str, token: str | None = None,
            data: dict | None = None) -> tuple[int, object]:
    headers = {"Accept": "application/json"}
    raw_data = None
    if data is not None:
        raw_data = json.dumps(data).encode()
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url.rstrip("/") + route,
                                 data=raw_data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as res:
            status, content = res.status, res.read()
    except urllib.error.HTTPError as ex:
        status, content = ex.code, ex.read()
    except (urllib.error.URLError, TimeoutError) as ex:
        raise RuntimeError(f"No se pudo conectar a la API: {type(ex).__name__}") from ex
    try:
        return status, json.loads(content) if content else None
    except json.JSONDecodeError:
        return status, content.decode("utf-8", errors="replace")[:180]


def expect(url: str, method: str, route: str, expected: int,
           token: str | None = None, data: dict | None = None) -> object:
    status, body = request(url, method, route, token, data)
    if status != expected:
        raise RuntimeError(f"{method} {route}: esperado HTTP {expected}, obtenido {status}; respuesta={body!r}")
    print(f"  ✓ {method} {route} -> HTTP {status}")
    return body


def create(url: str, path: Path, secret: str) -> None:
    if path.exists():
        raise RuntimeError(f"Ya existe {path}; verifica/limpia esa prueba antes de crear otra")
    owner_id = str(secrets.randbelow(8_000_000_000) + 1_000_000_000)
    video_id = secrets.token_hex(16)
    title = "Tecflix continuidad " + video_id
    token = issue_token(secret, owner_id)
    payload = {"id": video_id, "title": title,
               "description": "video ficticio para comprobar continuidad",
               "topic": "testing", "tags": ["spof-continuidad"],
               "duration_seconds": 2, "size_bytes": 2001, "visibility": "private"}
    expect(url, "GET", "/health", 200)
    result = expect(url, "POST", "/api/metadata/videos", 201, token, payload)
    if not isinstance(result, dict) or result.get("id") != video_id or result.get("owner_id") != owner_id:
        try:
            request(url, "DELETE", f"/api/metadata/videos/{video_id}", token)
        finally:
            raise RuntimeError("La API creó un registro con contenido inesperado; se intentó limpiarlo")
    state = {"id": video_id, "owner_id": owner_id, "title": title,
             "created_from": url.rstrip("/"), "created_at_utc": int(time.time())}
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        json.dump(state, stream, indent=2)
        stream.write("\n")
    print(f"\nCREADO. ID: {video_id}")
    print(f"Estado guardado en: {path}")
    print("Ahora detén o reinicia la API original. Ejecuta --phase verify contra la otra instancia.")


def finish(url: str, path: Path, secret: str, verify: bool) -> None:
    if not path.is_file():
        raise RuntimeError(f"No existe {path}; primero ejecuta --phase create")
    state = json.loads(path.read_text(encoding="utf-8"))
    video_id, owner_id, title = state["id"], state["owner_id"], state["title"]
    token = issue_token(secret, owner_id)
    route = f"/api/metadata/videos/{video_id}"
    if verify:
        expect(url, "GET", "/health", 200)
        found = expect(url, "GET", route, 200, token)
        if not isinstance(found, dict) or found.get("id") != video_id or found.get("title") != title:
            raise RuntimeError("El registro recuperado no coincide con el creado en la primera instancia")
        print("  ✓ La API receptora recuperó los datos originales")
    status, _ = request(url, "DELETE", route, token)
    if status not in (204, 404):
        raise RuntimeError(f"No se pudo borrar el registro de prueba (HTTP {status})")
    print(f"  ✓ Limpieza -> HTTP {status}")
    path.unlink()
    print("\nRESULTADO: OK. Continuidad de datos verificada y registro de prueba eliminado." if verify
          else "\nLIMPIEZA: OK. Se eliminó o no existía el registro de prueba.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Prueba de continuidad de metadata Tecflix V2")
    parser.add_argument("--phase", choices=("create", "verify", "cleanup"), required=True)
    parser.add_argument("--url", default="http://127.0.0.1:8082")
    parser.add_argument("--state", default="build/tecflix-metadata-continuidad.json")
    args = parser.parse_args()
    secret = os.environ.get("AUTH_SECRET", "")
    if len(secret.encode()) < 32:
        parser.error("Falta AUTH_SECRET de desarrollo (al menos 32 bytes) en el entorno")
    if not args.url.startswith(("http://", "https://")):
        parser.error("--url debe comenzar por http:// o https://")
    try:
        if args.phase == "create":
            create(args.url, Path(args.state), secret)
        else:
            finish(args.url, Path(args.state), secret, verify=args.phase == "verify")
        return 0
    except (RuntimeError, OSError, ValueError, KeyError, TypeError) as ex:
        print(f"\nRESULTADO: FALLÓ: {ex}", file=sys.stderr)
        print("El registro de prueba podría seguir en MongoDB; conserva el archivo --state para limpiarlo.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
