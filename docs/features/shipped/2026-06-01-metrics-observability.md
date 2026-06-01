# Feature spec: Metrics export + observability stack

> Tier: **Design**
> Status: **shipped**
> Started: 2026-06-01
> Spec author: Jaden Jin

---

## Classification

- **Reversibility**: Reversible
- **Scope**: Cross-cutting (lightly)
- **Tier rationale (1 sentence)**: Additive instrumentation plus standalone infra config with no API/wire change, but it spans build + server loop + a new ops config tree, so Design over Sketch.

---

## Problem

The server already computes the numbers that matter — egress bytes, packets, connected clients, entities, tick, reliable events — but it only `printf`s them once a second to stdout. That's enough to eyeball the P2 baseline once, but it can't be graphed over time, compared across N, or watched live while load ramps. P2's "measurement spine" is unfinished without a real metrics pipeline: a `/metrics` endpoint the server exposes, Prometheus scraping it, and a Grafana dashboard. This closes P2 and gives P3 (delta/quantization/AOI) a live before/after instrument to optimize against, instead of re-reading stdout.

## In scope

- **Instrument the server** with prometheus-cpp: export the counters it already tracks as Prometheus metrics on an HTTP `/metrics` endpoint (the library's `Exposer`).
- **Metric set:** `egress_bytes_total`, `egress_packets_total`, `reliable_events_total` (counters); `connected_clients`, `entities`, `tick_rate_hz` (gauges). Rates (bytes/s, packets/s) are computed in the dashboard via PromQL `rate()`, not server-side.
- **prometheus-cpp via FetchContent** (same mechanism as googletest), pinned to a tag.
- **Observability stack via Docker Compose:** Prometheus + Grafana containers only. Prometheus scrapes the *native* server; Grafana is provisioned with the Prometheus datasource + one dashboard, both checked in as code.
- **A first Grafana dashboard:** total egress (bytes/s), per-client egress, packets/s, connected clients, entities, tick rate — the panels that show the O(N²) baseline live as N ramps.
- **Config tree** checked into the repo (`ops/`): `docker-compose.yml`, `prometheus.yml`, Grafana datasource + dashboard provisioning, `ops/README.md` run recipe.

## Out of scope

- **Containerizing the server or bots.** They stay native on WSL so the measured latency path has zero container-NAT overhead (the reason we rejected k8s). Compose is observability-only.
- **Per-client metric labels** (a time series per endpoint). At N=100 that's 100 label sets churning — cardinality blowup for no P2 value. Per-client egress is shown as `total / clients`.
- **Alerting / Alertmanager.** This is a measurement tool, not a production service.
- **Metrics on the bots or the P0 `client/`.** Server-side egress is the headline.
- **Histograms** (e.g. RTT distribution). A single gauge is enough for P2; a histogram is a clean later add if P5 wants tail latency.
- **Auth / TLS on `/metrics`.** Local measurement only.
- **Removing the stdout printf.** Kept as a zero-dependency fallback; metrics export is additive alongside it.

## API sketch

```cpp
prometheus::Exposer exposer{"0.0.0.0:8080"};          // civetweb serves /metrics on its own thread
auto registry = std::make_shared<prometheus::Registry>();
exposer.RegisterCollectable(registry);

auto& egress_bytes = prometheus::BuildCounter()
    .Name("netcode_egress_bytes_total").Help("Total bytes sent to all clients")
    .Register(*registry).Add({});
// ... packets_total, reliable_events_total (counters); connected_clients, entities, tick_rate_hz (gauges)

// in the 1 Hz report block (lock-free; the Exposer thread reads concurrently):
egress_bytes.Increment(static_cast<double>(bytes - last_bytes));
clients_gauge.Set(static_cast<double>(mgr.size()));
```

```promql
rate(netcode_egress_bytes_total[10s])                                          # total bytes/s
rate(netcode_egress_bytes_total[10s]) / clamp_min(netcode_connected_clients,1) # per-client bytes/s
rate(netcode_egress_packets_total[10s])                                        # packets/s
```

---

## Design

### Data structures

No new project types. The metric objects (`prometheus::Counter&`, `prometheus::Gauge&`) live as locals in `main()` next to the existing counters, owned by a `shared_ptr<Registry>`. The `Exposer` owns its own HTTP server + thread. Metric updates derive from the same values the `printf` already uses — a second sink for numbers already computed. Counters are monotonic (`Increment(delta)`); gauges are set each report (`Set(value)`). Tick rate is derived: ticks since last report ÷ elapsed seconds.

### Module touchpoints

- **`CMakeLists.txt`** — `FetchContent` for `jupp0r/prometheus-cpp` (pinned tag), tests/push/compression off.
- **`server/CMakeLists.txt`** — link `prometheus-cpp::core` + `prometheus-cpp::pull` (the Exposer lives in `pull`).
- **`server/main.cpp`** — construct `Exposer` + `Registry`, register the metric set, update them in the existing 1 Hz report block.
- **New `ops/` tree** — `docker-compose.yml`, `prometheus.yml`, `grafana/provisioning/{datasources,dashboards}/*`, dashboard JSON, `ops/README.md`.
- **No change** to transport, sim, bots, or `client/`. `PeerManager` already exposes `total_bytes_sent()` / `total_packets_sent()`.

### Lifecycle / control flow

1. **Startup:** build `Registry`, register metrics, start `Exposer` on `:8080` (background civetweb thread).
2. **Per report tick (~1 Hz):** `Increment` counters by delta, `Set` gauges to current values. The existing `printf` stays.
3. **Scrape:** Prometheus (container) GETs `/metrics` each interval; the server does nothing special.
4. **Visualize:** Grafana queries Prometheus with the PromQL above.
5. **Concurrency:** metric objects are atomic/lock-free; the game loop writes, the Exposer thread reads — no locking, no game-loop stall.

### WSL2 / host-networking detail

Prometheus (container) must reach the native server. `extra_hosts: ["host.docker.internal:host-gateway"]` on the Prometheus service, scrape target `host.docker.internal:8080`; server binds `0.0.0.0:8080`. (Fallback if needed: `network_mode: host` + `localhost:8080`.)

### Edge cases

- **Scrape before any client:** counters 0, gauges 0 — valid baseline.
- **Server restart:** counters reset; Prometheus `rate()` handles counter resets natively.
- **`rate()` window vs scrape interval:** `[10s]` over 1s scrapes is stable; documented so the window isn't read as the scrape rate.
- **Port 8080 in use:** Exposer ctor throws; clear startup error rather than running blind.
- **Docker absent:** native server + stdout unaffected; only the dashboard is unavailable.

---

## Why this approach

- **prometheus-cpp `Exposer` (vs hand-rolled `/metrics`).** Naive-first applies to the netcode, not the observability plumbing — "integrated the standard Prometheus client + Grafana dashboard" is the portfolio signal; re-deriving the exposition format + a non-blocking accept loop is yak-shaving. The Exposer's thread keeps scrape handling off the game loop. Cost: a new (FetchContent-contained) dependency and one background thread.
- **Raw counters + PromQL rates (vs pre-computed gauges).** Idiomatic: counters survive restarts (rate() handles resets) and scrape-interval changes, and re-aggregate. Pre-computing bytes/s bakes in our 1s window and loses information.
- **Compose = Prometheus + Grafana only; server/bots native.** The measured path must not cross Docker's NAT/bridge hop or the numbers measure the container network — same reason k8s was rejected. Observability tools aren't on the latency path.
- **No per-client labels.** Per-endpoint label sets at N=100 with churn is a cardinality problem for zero P2 benefit; `total / clients` answers "per-client cost."
- **Config-as-code (provisioned Grafana).** The dashboard is part of the artifact and must come up identically from the repo, not a hand-clicked dashboard living on one machine.

## Risks / what could go wrong

- **prometheus-cpp's CMake fighting the project / lengthening the build.** Mitigation: pin a tag, disable its tests/push/compression, clean from-scratch build once before committing. (Hit a real instance — see Decisions: zlib.)
- **WSL2 container→host networking is the classic flaky bit.** Mitigation: the two documented wirings; verify the target is UP before building the dashboard; log which worked.
- **Threading:** Exposer reads while the loop writes — safe only because prometheus-cpp counters are atomic. Keep updates to the library's objects.
- **`rate()` window vs scrape confusion** could misread a graph. Mitigation: 1s scrape, `[10s]` rate, annotate.
- **Dashboard JSON drift** if edited in the UI without exporting back. Mitigation: provision from file; note in `ops/README.md`.
- **Metric naming** is a contract once dashboards depend on it. Picked `netcode_*` deliberately.

## Success criteria

- **Build:** clean from-scratch build with prometheus-cpp via FetchContent; existing 55 tests green; no new warnings.
- **Endpoint:** `curl http://localhost:8080/metrics` returns Prometheus text with every metric and sane values (clients matches bot count).
- **Pipeline:** `docker compose up` brings Prometheus + Grafana online; Prometheus target **UP**; Grafana loads the provisioned dashboard with the datasource, no clicking.
- **The money shot:** with the stack up, ramp bots (1 → 50 → 100); the dashboard shows total egress on the O(N²) curve live, per-client egress, packets/s, client/entity counts, tick ~60 Hz. A screenshot is the P2 README artifact.
- **Repro:** `ops/README.md` reproduces the above from a clean checkout (Docker installed).

## Test extensions required

No unit tests for the metrics wiring — it's I/O-bound glue around a third-party library plus Docker/Prometheus/Grafana config, with no project logic to assert (the "infra glue" the workflow allows to skip). The smoke is `curl /metrics` + Prometheus target UP + dashboard renders, run manually and recorded. Existing 55 tests must stay green (proves the instrumentation didn't break the build).

---

## Decisions during implementation

### 2026-06-01 — Disable find_package(ZLIB) explicitly
With `ENABLE_COMPRESSION OFF`, prometheus-cpp still ran `find_package(ZLIB)` and the box has the runtime `libz.so.1` but not the `-dev` symlink, which risked a config/link snag. Added `set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON CACHE BOOL "" FORCE)` so the build never reaches for zlib it doesn't use. Clean from-scratch configure + build after that; 55/55 tests still green.

### 2026-06-01 — WSL2 host networking: host.docker.internal works
Of the two speced wirings, `extra_hosts: ["host.docker.internal:host-gateway"]` on the Prometheus service (scrape target `host.docker.internal:8080`) works on this box — target reports `health: up`, no scrape error. Did not need `network_mode: host`. Server binds `0.0.0.0:8080`.

### 2026-06-01 — Pipeline verified end-to-end (the success criteria)
With the stack up and 20 bots driving the native server: `/metrics` serves all six metrics; Prometheus target UP; `netcode_connected_clients` = 20, `netcode_tick_rate_hz` = 60, egress on the slice-2 O(N²) curve. Grafana provisioned the Prometheus datasource (uid `prometheus`) and the "Netcode — P2 baseline" dashboard (uid `netcode-p2`) from checked-in files, both confirmed present via its API. Note: a `rate[10s]` read taken <10 s after start reads artificially low (window still spanning pre-traffic zeros) — read at steady state.

### 2026-06-01 — Grafana anonymous admin
Set `GF_AUTH_ANONYMOUS_ENABLED` + `ANONYMOUS_ORG_ROLE=Admin` + `DISABLE_LOGIN_FORM` so the dashboard opens with no login — it's a local measurement tool, not a service (matches the "no auth on /metrics" scope decision).

## Spec amendments

_None._

---

## Future work (out-of-scope ideas surfaced during this feature)

- RTT histogram (tail latency) when P5 needs it.
- Per-client labels / drilldown if a later phase needs per-connection diagnosis.
- Bot-side metrics (received snapshot rate, perceived loss) for a client-perspective panel.
- Alerting once there's a notion of "bad" worth alerting on.
- Bake the measured server into Compose with `network_mode: host` if one-command full repro is wanted and host-net is confirmed not to skew numbers.
