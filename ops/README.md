# Observability stack

Prometheus + Grafana for watching the server's metrics live. The stack runs in
Docker; the **measured server and bots run natively** on the WSL host so their
latency/bandwidth numbers are never tainted by Docker's NAT hop.

## Run

1. Build and start the server (native):
   ```
   cmake --build build -j
   ./build/server/server 9999        # serves /metrics on :8080
   ```
2. Bring up the stack:
   ```
   docker compose -f ops/docker-compose.yml up -d
   ```
3. Drive load (native), ramping N to see the O(N²) curve:
   ```
   ./build/bots/bots 50 127.0.0.1 9999 60
   ```
4. Open Grafana at http://localhost:3000 → dashboard **"Netcode — P2 baseline"**
   (anonymous admin, no login). Prometheus is at http://localhost:9090; check
   Status → Targets shows `netcode-server` **UP**.

Tear down: `docker compose -f ops/docker-compose.yml down`.

## Notes

- Prometheus scrapes `host.docker.internal:8080` (mapped to the host via
  `extra_hosts` in the compose file). If the target shows DOWN, confirm the server
  is running and bound to `0.0.0.0:8080`.
- The dashboard uses `rate(...[10s])` over a 1 s scrape interval — the window is
  deliberately wider than the scrape so rates are stable; it is not the scrape rate.
- Metrics are also printed to the server's stdout once a second as a
  dependency-free fallback.
- Dashboard JSON is provisioned from `grafana/provisioning/dashboards/netcode.json`.
  If you edit it in the Grafana UI, export it back to that file or the change is lost.
