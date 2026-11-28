#!/usr/bin/env python3
"""Renews the JWTs of the load-test accounts without touching the dataset.

The tokens written by seed_loadtest.py expire after 24 h. This logs every
<prefix>_NNN account in again and rewrites only "bearerToken" in the k6 config
(same users, same video, same catalog), so later runs stay comparable.

Usage:
  python3 performance/scripts/refresh_tokens.py
"""
import argparse
import json
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from seed_loadtest import USERS_API, call


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--config", default=str(Path.home() / "tecflix-env/loadtest/single.local.json"))
    parser.add_argument("--password-file", default=str(Path.home() / "tecflix-env/loadtest/password.txt"))
    parser.add_argument("--prefix", default="loadtest")
    args = parser.parse_args()

    config = json.loads(Path(args.config).read_text())
    password = Path(args.password_file).read_text().strip()
    users = config["credentials"]["users"]

    def login(index):
        username = f"{args.prefix}_{index + 1:03d}"
        status, body = call("POST", f"{USERS_API}/api/auth/login",
                            {"username": username, "password": password})
        if status != 200:
            raise SystemExit(f"login {username}: HTTP {status} {body}")
        if str(body["user"]["id"]) != users[index]["userId"]:
            raise SystemExit(f"{username} has id {body['user']['id']}, config expects {users[index]['userId']}")
        return body["token"]

    # PBKDF2 makes each login cost ~0.5 s of CPU: a few in parallel.
    with ThreadPoolExecutor(max_workers=8) as pool:
        tokens = list(pool.map(login, range(len(users))))
    for user, token in zip(users, tokens):
        user["bearerToken"] = token

    path = Path(args.config)
    path.write_text(json.dumps(config, indent=2))
    os.chmod(path, 0o600)
    print(f"{len(users)} tokens renovados en {path}")


if __name__ == "__main__":
    main()
