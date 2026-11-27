#!/usr/bin/env python3
"""Seeds a local Tecflix environment with data for repeatable performance tests.

Creates N test accounts, uploads one short generated video (transcoded by the
streaming API), registers its metadata, fills the catalog with extra metadata
documents, and writes a k6 config compatible with load-tests/lib/tecflix.js.

The config contains JWTs, so it is written OUTSIDE the repository (default
~/tecflix-env/loadtest/single.local.json). Uses only the Python standard library.

Usage:
  python3 performance/scripts/seed_loadtest.py --users 64 --catalog 2000
"""
import argparse
import json
import os
import random
import secrets
import string
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

USERS_API = "http://127.0.0.1:8081"
METADATA_API = "http://127.0.0.1:8082"
STREAMING_API = "http://127.0.0.1:8083"

TOPICS = ["ciencia", "deportes", "musica", "historia", "cocina", "viajes", "tecnologia", "arte"]
WORDS = ["rapido", "nuevo", "clasico", "tutorial", "resumen", "en vivo", "parte", "final",
         "especial", "documental", "entrevista", "demo", "estreno", "remasterizado"]


def call(method, url, body=None, token=None, content_type="application/json", timeout=60):
    data = None
    headers = {}
    if body is not None:
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        headers["Content-Type"] = content_type
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read()
            return response.status, (json.loads(raw) if raw and raw[:1] in b"{[" else raw)
    except urllib.error.HTTPError as error:
        raw = error.read()
        try:
            return error.code, json.loads(raw)
        except ValueError:
            return error.code, raw


def ensure_user(username, password):
    status, body = call("POST", f"{USERS_API}/api/users",
                        {"username": username, "email": f"{username}@loadtest.invalid",
                         "password": password})
    if status not in (201, 409):
        raise SystemExit(f"register {username}: HTTP {status} {body}")
    status, body = call("POST", f"{USERS_API}/api/auth/login",
                        {"username": username, "password": password})
    if status != 200:
        raise SystemExit(f"login {username}: HTTP {status} {body}")
    return {"userId": str(body["user"]["id"]), "bearerToken": body["token"]}


def make_test_video(seconds):
    path = Path(tempfile.gettempdir()) / f"tecflix_loadtest_{seconds}s.mp4"
    if not path.exists():
        subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-y",
             "-f", "lavfi", "-i", f"testsrc2=size=1280x720:rate=30:duration={seconds}",
             "-f", "lavfi", "-i", f"sine=frequency=440:duration={seconds}",
             "-c:v", "libx264", "-preset", "veryfast", "-c:a", "aac", "-shortest", str(path)],
            check=True)
    return path


def upload_ready_video(owner, video_path, title):
    payload = video_path.read_bytes()
    status, body = call("POST", f"{STREAMING_API}/api/videos", payload, owner["bearerToken"],
                        "application/octet-stream", timeout=300)
    if status not in (200, 201, 202):
        raise SystemExit(f"upload: HTTP {status} {body}")
    video_id = body["id"]
    status, body = call("POST", f"{METADATA_API}/api/metadata/videos",
                        {"id": video_id, "title": title, "description": "video de pruebas de carga",
                         "topic": "tecnologia", "tags": ["loadtest"],
                         "duration_seconds": 30, "size_bytes": len(payload),
                         "visibility": "public"}, owner["bearerToken"])
    if status not in (200, 201):
        raise SystemExit(f"metadata for uploaded video: HTTP {status} {body}")
    deadline = time.time() + 300
    while time.time() < deadline:
        status, body = call("GET", f"{STREAMING_API}/api/videos/{video_id}",
                            token=owner["bearerToken"])
        state = body.get("status") if isinstance(body, dict) else None
        if state == "ready":
            return video_id
        if state == "failed":
            raise SystemExit(f"transcoding failed for {video_id}")
        time.sleep(2)
    raise SystemExit(f"video {video_id} not ready after 5 minutes")


def fill_catalog(users, count, rng):
    created = 0
    for index in range(count):
        owner = users[index % len(users)]
        title = f"{rng.choice(WORDS).title()} {rng.choice(TOPICS)} {index:05d}"
        status, body = call("POST", f"{METADATA_API}/api/metadata/videos", {
            "id": secrets.token_hex(16),
            "title": title,
            "description": " ".join(rng.choice(WORDS) for _ in range(12)),
            "topic": rng.choice(TOPICS),
            "tags": rng.sample(WORDS, 3),
            "duration_seconds": rng.randint(30, 7200),
            "size_bytes": rng.randint(1_000_000, 50_000_000),
            "visibility": "public" if rng.random() < 0.7 else "private",
        }, owner["bearerToken"])
        if status not in (200, 201):
            raise SystemExit(f"catalog item {index}: HTTP {status} {body}")
        created += 1
        if created % 5000 == 0:
            print(f"  catálogo: {created}/{count}")
    return created


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--users", type=int, default=64)
    parser.add_argument("--catalog", type=int, default=2000)
    parser.add_argument("--video-seconds", type=int, default=30)
    parser.add_argument("--prefix", default="loadtest")
    parser.add_argument("--out", default=str(Path.home() / "tecflix-env/loadtest/single.local.json"))
    parser.add_argument("--seed", type=int, default=2103)
    parser.add_argument("--catalog-only", action="store_true",
                        help="reuse users/video from --out and only add --catalog documents")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    out = Path(args.out)
    if args.catalog_only:
        users = json.loads(out.read_text())["credentials"]["users"]
        print(f"Agregando {args.catalog} documentos al catálogo...")
        fill_catalog(users, args.catalog, rng)
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    secret_file = out.parent / "password.txt"
    if secret_file.exists():
        password = secret_file.read_text().strip()
    else:
        password = "".join(secrets.choice(string.ascii_letters + string.digits) for _ in range(24))
        secret_file.write_text(password)
        os.chmod(secret_file, 0o600)

    print(f"Creando/autenticando {args.users} usuarios de prueba...")
    users = [ensure_user(f"{args.prefix}_{i:03d}", password) for i in range(1, args.users + 1)]

    print("Generando y subiendo un video de prueba (transcodificación real con FFmpeg)...")
    video_id = upload_ready_video(users[0], make_test_video(args.video_seconds),
                                  "Video base de pruebas de carga")
    print(f"  video listo: {video_id}")

    print(f"Llenando el catálogo con {args.catalog} documentos de metadata...")
    fill_catalog(users, args.catalog, rng)

    config = {
        "topology": "single",
        "physicalServerCount": 1,
        "instancesPerService": 1,
        "loadBalancer": None,
        "services": {"users": USERS_API, "metadata": METADATA_API, "streaming": STREAMING_API},
        "credentials": {"users": users, "videoId": video_id, "segment": "segment_00001.ts"},
        "thinkTimeSeconds": 1,
    }
    old_umask = os.umask(0o077)
    try:
        out.write_text(json.dumps(config, indent=2))
    finally:
        os.umask(old_umask)
    print(f"Config k6 escrita en {out} (contiene JWT: no subir a Git)")


if __name__ == "__main__":
    sys.exit(main())
