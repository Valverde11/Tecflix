#!/usr/bin/env python3
"""Prueba aislada de elección MongoDB con tres mongod y dos APIs de metadata."""
import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time

from tecflix_v2_metadata_continuidad import create, finish, request


def wait_for(description, check, timeout=65):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(0.5)
    raise RuntimeError(f"No se verificó a tiempo: {description}")


def available_ports(count):
    with ExitStack() as sockets:
        bound = [sockets.enter_context(socket.socket()) for _ in range(count)]
        for listener in bound:
            listener.bind(("127.0.0.1", 0))
        return [listener.getsockname()[1] for listener in bound]


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def mongo(port, expression):
    result = subprocess.run(
        ["mongosh", "--norc", "--quiet",
         f"mongodb://127.0.0.1:{port}/admin?directConnection=true&serverSelectionTimeoutMS=1500",
         "--eval", expression], capture_output=True, text=True, timeout=10, check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def status(url):
    try:
        return request(url, "GET", "/ready")[0]
    except RuntimeError:
        return None


def run(binary):
    ports = available_ports(5)
    mongo_ports, first_api_port, second_api_port = ports[:3], ports[3], ports[4]
    hosts = [f"localhost:{port}" for port in mongo_ports]
    secret = secrets.token_hex(32)
    first_url = f"http://127.0.0.1:{first_api_port}"
    second_url = f"http://127.0.0.1:{second_api_port}"
    config = {"_id": "tecflixLab", "members": [
        {"_id": index, "host": host} for index, host in enumerate(hosts)
    ]}

    with tempfile.TemporaryDirectory(prefix="tecflix-mongo-") as folder, ExitStack() as cleanup:
        root = Path(folder)

        def start(args, log, env=None):
            stream = cleanup.enter_context(log.open("ab"))
            process = subprocess.Popen(args, stdout=stream, stderr=stream, env=env)
            cleanup.callback(stop, process)
            return process

        def member(port):
            dbpath = root / str(port)
            dbpath.mkdir(exist_ok=True)
            return start(["mongod", "--bind_ip", "127.0.0.1", "--port", str(port),
                          "--replSet", "tecflixLab", "--dbpath", str(dbpath)],
                         root / f"{port}.log")

        nodes = {port: member(port) for port in mongo_ports}
        for port in nodes:
            wait_for(f"MongoDB en {port}", lambda: mongo(port, "db.runCommand({ping:1}).ok") == "1")
        if mongo(mongo_ports[0], f"rs.initiate({json.dumps(config)}).ok") != "1":
            raise RuntimeError("No se pudo iniciar el replica set de laboratorio")

        def primary(candidates):
            return next((port for port in candidates
                         if mongo(port, "db.hello().isWritablePrimary") == "true"), None)

        first = wait_for("primer primario", lambda: primary(mongo_ports))
        wait_for("tres miembros sincronizados", lambda: mongo(
            first, "rs.status().members.filter(m => [1,2].includes(m.state)).length"
        ) == "3")
        env = dict(os.environ, AUTH_SECRET=secret, MONGO_DB="tecflix_lab",
                   LLVM_PROFILE_FILE=str(root / "metadata-%p.profraw"),
                   MONGO_URI="mongodb://" + ",".join(hosts) +
                   "/?replicaSet=tecflixLab&w=majority&serverSelectionTimeoutMS=1500")
        def api(port):
            return start([str(binary)], root / f"api-{port}.log", dict(env, PORT=str(port)))

        first_api = api(first_api_port)
        api(second_api_port)
        wait_for("primera API lista", lambda: status(first_url) == 200)
        wait_for("segunda API lista", lambda: status(second_url) == 200)
        state = root / "video.json"
        create(first_url, state, secret)
        stop(first_api)
        finish(second_url, state, secret, verify=True)
        api(first_api_port)
        wait_for("primera API recuperada", lambda: status(first_url) == 200)
        create(second_url, state, secret)

        stop(nodes[first])
        survivors = [port for port in mongo_ports if port != first]
        promoted = wait_for("elección tras perder el primario", lambda: primary(survivors))
        wait_for("API lista tras elección", lambda: status(first_url) == 200)
        finish(first_url, state, secret, verify=True)
        create(second_url, state, secret)
        print("Lecturas y escrituras recuperadas tras perder un miembro.")

        second = next(port for port in survivors if port != promoted)
        stop(nodes[second])
        wait_for("HTTP 503 sin mayoría", lambda: status(first_url) == status(second_url) == 503)
        print("Sin mayoría, la API devuelve HTTP 503.")
        member(first)
        wait_for("API lista tras recuperar mayoría", lambda: status(first_url) == 200)
        finish(first_url, state, secret, verify=True)
        print("Mayoría y datos recuperados.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True)
    run(parser.parse_args().metadata.resolve())
