# Tecflix load-test topology

The single Compose file starts one instance of each API. The dual file is explicitly **provisional, solo valida la herramienta**: it runs two Metadata replicas behind Nginx, but only one Users and one Streaming instance. It does not satisfy the V2 requirement of two instances per API. The team has not authorized changes to Users or Streaming persistence.

Nginx exposes Users, Metadata and Streaming at ports 8081, 8082 and 8083 for single; dual uses host ports 8181, 8182 and 8183. Compose healthchecks actively check each `/health` endpoint. Nginx uses passive upstream failure detection (`max_fails`/`fail_timeout`); it is not an active health-check module.

## Start

From the repository root, generate local secrets in the same shell for both projects. They are not written to the repository:

```bash
export AUTH_SECRET="$(openssl rand -hex 32)"
export SERVICE_SECRET="$(openssl rand -hex 32)"
```

Start single first. It creates isolated single/dual service networks, attaches Mongo to both, and creates the persistent test volumes:

```bash
docker compose -p tecflix-loadtest-single \
  -f deploy/loadtest/docker-compose.single.yml up -d --build
docker compose -p tecflix-loadtest-single \
  -f deploy/loadtest/docker-compose.single.yml ps
```

Seed the existing test account, a Metadata record and a ready video using the team's normal authorized setup procedure. Finish seeding before starting dual. Both topologies reuse the same Users/Streaming volumes and Mongo database so the read-only/login comparison sees the same fixture.

Start dual with two Metadata replicas by default (`METADATA_REPLICAS` can override the count):

```bash
docker compose -p tecflix-loadtest-dual \
  -f deploy/loadtest/docker-compose.dual.yml up -d --build
docker compose -p tecflix-loadtest-dual \
  -f deploy/loadtest/docker-compose.dual.yml ps
```

Verify both ingress sets:

```bash
for port in 8081 8082 8083 8181 8182 8183; do
  curl --fail "http://127.0.0.1:${port}/health"
done
```

Run only the existing `login` or `read-only` load scenarios. Do not upload, delete, change quotas, or modify metadata while both projects are up: Users and Streaming storage is shared without inter-process write coordination. The generated JSON/CSV is marked **provisional, solo valida la herramienta**.

Stop dual before single. Keep the volumes for repeat runs; `down -v` deletes the seeded databases and files:

```bash
docker compose -p tecflix-loadtest-dual \
  -f deploy/loadtest/docker-compose.dual.yml down
docker compose -p tecflix-loadtest-single \
  -f deploy/loadtest/docker-compose.single.yml down
```

The single and dual files use the same service images, secrets, environment and volumes. The only intended service-count difference is Metadata: one instance in single, two by default in dual. Users and Streaming remain one instance in both.