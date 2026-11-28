#!/usr/bin/env python3
"""Inicia metadata leyendo claves de .env SIN evaluar codigo de shell. macOS o Ubuntu.

Usa el ejecutable compilado existente (no instala paquetes). El tunel en Ubuntu
se configura como mongodb://127.0.0.1:27018, preservando usuario/password,
base y parametros originales del MONGO_URI. No imprime secretos.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import sys
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit

ALLOWED = {"AUTH_SECRET", "SERVICE_SECRET", "MONGO_URI", "MONGO_DB", "CORS_ORIGIN", "PORT"}
KEY = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def parse_env(path):
    values = {}
    for num, raw in enumerate(Path(path).read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{num} no tiene =")
        name, value = [x.strip() for x in line.split("=", 1)]
        if not KEY.fullmatch(name):
            raise ValueError(f"{path}:{num} nombre de variable no válido")
        if name not in ALLOWED:
            continue
        if value.startswith(('"', "'")):
            if len(value) < 2 or value[-1] != value[0]:
                raise ValueError(f"{path}:{num} comillas no cerradas")
            value = value[1:-1]
        if any(token in value for token in ("$(`", "$(`", "$(", "`", "${")):
            raise ValueError(f"{path}:{num} contiene sintaxis de shell; no se ejecuta")
        values[name] = value
    return values


def tunneled_uri(uri):
    parsed = urlsplit(uri)
    if parsed.scheme != "mongodb":
        raise ValueError("El túnel espera URI mongodb://, no mongodb+srv://")
    if "," in parsed.netloc:
        raise ValueError("URI multihost: no usar el túnel simple para replica sets")
    auth = parsed.netloc.rpartition("@")[0] + "@" if "@" in parsed.netloc else ""
    query = dict(parse_qsl(parsed.query, keep_blank_values=True))
    if "replicaSet" in query:
        raise ValueError("URI replicaSet: necesita topología alcanzable, no túnel directo")
    query["directConnection"] = "true"
    query["serverSelectionTimeoutMS"] = "5000"
    return urlunsplit(("mongodb", auth + "127.0.0.1:27018", parsed.path, urlencode(query), parsed.fragment))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--machine", choices=["macos", "ubuntu"], required=True)
    parser.add_argument("--mongo-connection", choices=["configured", "tunnel"],
                        help="Usar MONGO_URI tal cual o el túnel local. Por defecto Ubuntu usa el túnel.")
    parser.add_argument("--env-file", default=".env")
    parser.add_argument("--executable", default="build/metadata-mongo/metadata-service")
    parser.add_argument("--port", type=int, default=8082)
    parser.add_argument("--check", action="store_true", help="Solo validar las variables, sin iniciar")
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("Puerto fuera de rango")
    try:
        values = parse_env(args.env_file)
        env = os.environ.copy()
        env.update(values)
        secret = env.get("AUTH_SECRET", "")
        if len(secret.encode("utf-8")) < 32:
            raise ValueError("AUTH_SECRET debe tener >= 32 bytes")
        if not env.get("MONGO_DB") or not env.get("MONGO_URI"):
            raise ValueError("MONGO_URI y MONGO_DB deben estar definidos")
        connection = args.mongo_connection or ("tunnel" if args.machine == "ubuntu" else "configured")
        if connection == "tunnel":
            env["MONGO_URI"] = tunneled_uri(env["MONGO_URI"])
        env["PORT"] = str(args.port)
        # Huella para comparar configuración ENTRE maquinas; nunca imprime el valor real.
        fingerprint = hashlib.sha256(secret.encode()).hexdigest()[:12]
        print(f"Configuración: {args.machine}, puerto {args.port}, base {env['MONGO_DB']}, "
              f"huella AUTH_SECRET sha256/12={fingerprint}", flush=True)
        if args.check:
            return 0
        binary = Path(args.executable).resolve()
        if not binary.is_file():
            raise ValueError(f"No existe el ejecutable: {binary}")
        os.execve(str(binary), [str(binary)], env)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
