#!/usr/bin/env python3
"""Usuarios redundantes y Playwright con APIs reales y PostgreSQL de laboratorio."""
import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import tempfile
from urllib.request import Request, urlopen

from tecflix_v2_failover_check import measure
from tecflix_v2_metadata_continuidad import expect, request
from tecflix_v2_mongo_replica_lab import available_ports, mongo, status, stop, wait_for


def run(args):
    root = Path(__file__).resolve().parents[1]
    ports = available_ports(8)
    users_a, users_b, metadata, streaming, web, mongo_port, standby, proxy_port = ports
    base = {port: f"http://127.0.0.1:{port}" for port in ports}
    service_secret = secrets.token_hex(32)
    env = dict(os.environ, AUTH_SECRET=secrets.token_hex(32), SERVICE_SECRET=service_secret,
               CORS_ORIGIN=f"http://localhost:{web}", TECFLIX_USER_STORAGE="postgres")

    with tempfile.TemporaryDirectory(prefix="tecflix-functional-") as folder, ExitStack() as cleanup:
        data = Path(folder)

        def start(command, label, **overrides):
            log = cleanup.enter_context((data / f"{label}.log").open("ab"))
            process = subprocess.Popen(command, cwd=data,
                                       env=dict(env, **overrides), stdout=log, stderr=log)
            cleanup.callback(stop, process)
            return process

        def user(port, **overrides):
            return start([str(Path(args.users).resolve())], f"users-{port}", PORT=str(port), **overrides)

        incomplete = user(users_a, PGDATABASE="postgres")
        wait_for("HTTP 503 en la base sin esquema", lambda: status(base[users_a]) == 503)
        expect(base[users_a], "GET", "/health", 200)
        stop(incomplete)
        print("PostgreSQL accesible sin esquema: /health 200 y /ready 503.", flush=True)

        first = user(users_a)
        user(users_b)
        for port in (users_a, users_b):
            wait_for(f"usuarios listo en {port}", lambda: status(base[port]) == 200)
        users_http_port = users_b
        if args.docker_proxy:
            users_http_port = proxy_port
            config = data / "haproxy.cfg"
            config.write_text(f"""defaults
    mode http
    timeout connect 2s
    timeout client 15s
    timeout server 15s
frontend users
    bind *:8080
    default_backend nodes
backend nodes
    balance roundrobin
    option httpchk GET /ready
    http-check expect status 200
    http-response set-header X-Tecflix-Backend %[srv_name]
    server macos host.docker.internal:{users_a} check inter 500ms
    server ubuntu host.docker.internal:{users_b} check inter 500ms
""")
            name = f"tecflix-functional-users-{secrets.token_hex(6)}"
            subprocess.run(["docker", "run", "--detach", "--rm", "--name", name,
                            "--publish", f"127.0.0.1:{proxy_port}:8080",
                            "--volume", f"{config}:/usr/local/etc/haproxy/haproxy.cfg:ro",
                            "haproxy:3.2"], check=True, stdout=subprocess.DEVNULL)
            cleanup.callback(subprocess.run, ["docker", "stop", name], stdout=subprocess.DEVNULL)

        def check_distribution(expected):
            if not args.docker_proxy:
                return

            def matches():
                counts, errors = measure(base[proxy_port], "/ready", 6, 0.05)
                return not errors and set(counts) == expected

            wait_for(f"HAProxy de usuarios con {sorted(expected)}", matches)

        check_distribution({"macos", "ubuntu"})
        username = "lab_" + secrets.token_hex(6)
        credentials = {"username": username, "password": secrets.token_urlsafe(24)}
        account = expect(base[users_a], "POST", "/api/users", 201,
                         data=dict(credentials, email=f"{username}@example.test"))
        login = expect(base[users_b], "POST", "/api/auth/login", 200, data=credentials)
        quota_path = f"/api/users/{account['id']}/quota/usage"

        def quota(port, delta, operation_id):
            payload = json.dumps({"deltaBytes": delta, "operationId": operation_id}).encode()
            call = Request(base[port] + quota_path, data=payload, headers={
                "Content-Type": "application/json", "X-Service-Token": service_secret,
            }, method="POST")
            with urlopen(call, timeout=10) as response:
                return json.load(response)["usedBytes"]

        reservation = secrets.token_hex(16)
        if quota(users_a, 400, reservation) != 400:
            raise RuntimeError("La reserva no quedó registrada")
        stop(first)
        check_distribution({"ubuntu"})
        expect(base[users_http_port], "GET", "/api/users/me", 200, login["token"])
        if quota(users_b, 400, reservation) != 400:
            raise RuntimeError("El reintento en otra API duplicó la cuota")
        user(users_a)
        wait_for("regreso de la primera API", lambda: status(base[users_a]) == 200)
        check_distribution({"macos", "ubuntu"})
        if quota(users_a, -400, secrets.token_hex(16)) != 0:
            raise RuntimeError("La API recuperada no pudo liberar la cuota")
        print("Usuarios: registro, login cruzado, caída, cuota idempotente y recuperación verificados.", flush=True)
        if args.users_only:
            return

        mongo_path = data / "mongo"
        mongo_path.mkdir()
        start(["mongod", "--bind_ip", "127.0.0.1", "--port", str(mongo_port),
               "--dbpath", str(mongo_path)], "mongo")
        wait_for("MongoDB listo", lambda: mongo(mongo_port, "db.runCommand({ping:1}).ok") == "1")
        start([str(Path(args.metadata).resolve())], "metadata", PORT=str(metadata),
              MONGO_URI=f"mongodb://127.0.0.1:{mongo_port}/?serverSelectionTimeoutMS=1500",
              MONGO_DB=f"tecflix_lab_{secrets.token_hex(8)}")
        wait_for("metadata lista", lambda: status(base[metadata]) == 200)
        def streaming_api(port, **overrides):
            return start([str(Path(args.streaming).resolve())], f"streaming-{port}",
                         TECFLIX_PORT=str(port), TECFLIX_STORAGE=str(data / "video"),
                         TECFLIX_STREAMING_EXCLUSIVE_STORAGE="1",
                         TECFLIX_ENABLE_QUOTAS="1", TECFLIX_USER_SERVICE_HOST="127.0.0.1",
                         TECFLIX_USER_SERVICE_PORT=str(users_http_port), TECFLIX_ENABLE_METADATA_AUTH="1",
                         TECFLIX_METADATA_HOST="127.0.0.1", TECFLIX_METADATA_PORT=str(metadata),
                         **overrides)

        writer = streaming_api(streaming)
        wait_for("streaming listo", lambda: expect_health(base[streaming]))
        if streaming_api(standby).wait(timeout=10) != 1:
            raise RuntimeError("El segundo escritor no rechazó el almacenamiento ocupado")
        clip = data / "test.mp4"
        subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=320x180:rate=25", "-t", "3", "-c:v", "libx264",
                        "-pix_fmt", "yuv420p", str(clip)], check=True)
        subprocess.run(["npm", "run", "e2e"], cwd=root / "Aplicacion_Web", check=True,
                       env=dict(env, E2E_WEB_PORT=str(web), E2E_MOCK_VIDEO_APIS="false",
                                E2E_USERS_API_URL=base[users_http_port], E2E_METADATA_API_URL=base[metadata],
                                E2E_STREAMING_API_URL=base[streaming], E2E_VIDEO_FILE=str(clip)))
        token = login["token"]
        headers = {"Authorization": f"Bearer {token}"}

        def register_video(port):
            upload = Request(base[port] + "/api/videos", data=clip.read_bytes(),
                             headers={**headers, "Content-Type": "application/octet-stream"})
            with urlopen(upload, timeout=15) as response:
                if response.status != 202:
                    raise RuntimeError("Streaming no aceptó el video de relevo")
                video_id = json.load(response)["id"]
            expect(base[metadata], "POST", "/api/metadata/videos", 201, token,
                   {"id": video_id, "title": "Prueba de relevo", "description": "", "topic": "testing",
                    "tags": [], "duration_seconds": 3, "size_bytes": clip.stat().st_size,
                    "visibility": "private"})
            return f"/api/videos/{video_id}"

        def video_ready(port, path):
            return request(base[port], "GET", path, token) == (200, {"status": "ready"})

        def media(port, video_path):
            with urlopen(Request(base[port] + video_path + "/index.m3u8", headers=headers),
                         timeout=10) as response:
                manifest = response.read()
            segment = next(line for line in manifest.decode().splitlines() if line.startswith("segments/"))
            with urlopen(Request(base[port] + video_path + "/" + segment, headers=headers),
                         timeout=10) as response:
                return manifest, response.read()

        video_path = register_video(streaming)
        wait_for("video transcodificado", lambda: video_ready(streaming, video_path))
        before = media(streaming, video_path)
        stop(writer)
        backup = streaming_api(standby)
        wait_for("streaming de respaldo listo", lambda: expect_health(base[standby]))
        if media(standby, video_path) != before:
            raise RuntimeError("El manifiesto o segmento cambió tras el relevo")
        quota_after = expect(base[users_b], "GET", f"/api/users/{account['id']}/quota", 200, token)
        if quota_after["usedBytes"] != clip.stat().st_size:
            raise RuntimeError("El relevo modificó la cuota de un video confirmado")
        print("Streaming: exclusión del segundo escritor, relevo, HLS persistente y cuota única verificados.")

        stop(backup)
        marker, release, wrapper = data / "ffmpeg.started", data / "ffmpeg.release", data / "ffmpeg-gate"
        wrapper.write_text("""#!/bin/sh
touch "$LAB_FFMPEG_MARKER"
i=0
while [ ! -f "$LAB_FFMPEG_RELEASE" ] && [ "$i" -lt 150 ]; do
    sleep 0.1
    i=$((i + 1))
done
exec "$LAB_REAL_FFMPEG" "$@"
""")
        wrapper.chmod(0o700)
        cleanup.callback(release.touch)
        writer = streaming_api(streaming, TECFLIX_FFMPEG=str(wrapper), LAB_FFMPEG_MARKER=str(marker),
                               LAB_FFMPEG_RELEASE=str(release), LAB_REAL_FFMPEG=shutil.which("ffmpeg"))
        wait_for("streaming listo para cortar transcodificación", lambda: expect_health(base[streaming]))
        interrupted = register_video(streaming)
        wait_for("FFmpeg en ejecución", marker.exists)
        writer.kill()
        writer.wait(timeout=10)
        if streaming_api(standby).wait(timeout=10) != 1:
            raise RuntimeError("El respaldo abrió la carpeta mientras el FFmpeg anterior seguía activo")
        release.touch()

        def start_replacement():
            process = streaming_api(standby)
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                return process
            return None

        wait_for("FFmpeg anterior liberó la carpeta", start_replacement)
        wait_for("streaming recuperó una transcodificación interrumpida",
                 lambda: expect_health(base[standby]) and video_ready(standby, interrupted))
        media(standby, interrupted)
        quota_after = expect(base[users_b], "GET", f"/api/users/{account['id']}/quota", 200, token)
        if quota_after["usedBytes"] != 2 * clip.stat().st_size:
            raise RuntimeError("El relevo de una transcodificación interrumpida duplicó la cuota")
        print("Streaming: el respaldo esperó a FFmpeg y recuperó la transcodificación interrumpida.")


def expect_health(url):
    try:
        return request(url, "GET", "/health")[0] == 200
    except RuntimeError:
        return False


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--users", required=True)
    parser.add_argument("--metadata")
    parser.add_argument("--streaming")
    parser.add_argument("--users-only", action="store_true")
    parser.add_argument("--docker-proxy", action="store_true",
                        help="Comprobar usuarios a través de HAProxy en Docker Desktop")
    args = parser.parse_args()
    if os.environ.get("TECFLIX_DISPOSABLE_DATABASES") != "1":
        parser.error("Usa bases desechables y confirma con TECFLIX_DISPOSABLE_DATABASES=1")
    if not args.users_only and not (args.metadata and args.streaming):
        parser.error("El flujo completo requiere --metadata y --streaming")
    run(args)
