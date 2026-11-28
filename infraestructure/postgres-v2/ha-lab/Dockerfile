FROM postgres:17
RUN apt-get update && apt-get install -y --no-install-recommends python3-venv \
    && python3 -m venv /opt/patroni \
    && /opt/patroni/bin/pip install --no-cache-dir 'patroni[etcd3,psycopg3]==4.1.3' \
    && apt-get clean
USER postgres
ENTRYPOINT ["/opt/patroni/bin/patroni", "/etc/patroni.yaml"]
