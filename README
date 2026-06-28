# DeterSL — Deterministic Multithreaded Serverless Worker

DeterSL is a deterministic serverless runtime for durable, multi-step workflows over
shared state. Instead of recovering from fine-grained checkpoints, a workflow's
execution can be reconstructed from an ordered input log plus explicit resource
dependencies. The worker combines:

- **Ordered delivery** through NATS JetStream,
- **Explicit resource binding** in a small workflow DSL,
- **Verona-based scheduling** over read-only / read-write `cown` sets,
- **WebAssembly execution** via a Wasmtime-based runtime (the `wasm-exec-env` crate).

This repo contains the C++ worker, a Python client library, two example applications
(YCSB-T and a hotel/travel reservation workflow), microbenchmarks, and experiment scripts.

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | C++ worker: `nats/` (JetStream I/O), `workflow/` (registry, scheduler, graph, DSL), `execution/` (Wasm runner), `storage/` |
| `external/` | Submodules: `verona-rt`, `snmalloc`, `wasm-exec-env` (Rust) |
| `detersl_client/` | Async Python client (register functions/workflows, invoke, pull metrics) |
| `applications/` | `ycsb/`, `hotel-reservation/`, `microbenchmarks/` — functions (`.wasm` + `.json`) and clients |
| `scripts/` | Cluster start/stop and experiment-runner scripts |
| `config.json` | Wasmtime engine config (cache, strategy, allocation) |
| `docker/`, `Dockerfile`, `docker-compose.yml` | Containerized worker + NATS |

## Prerequisites

- A C++20 compiler, CMake ≥ 3.18
- OpenSSL, and the [NATS C client](https://github.com/nats-io/nats.c) (`nats.c`, headers + lib)
- A Rust toolchain (builds the `wasm-exec-env` crate)
- A running NATS server with JetStream enabled
- Python 3 for the clients (`pandas`, `tqdm`)

`nlohmann_json`, `simdjson`, `cpp-httplib`, and `atomic_queue` are fetched
automatically by CMake.

Initialize submodules first:

```bash
git submodule update --init --recursive
```

## Quick start (Docker)

The simplest way to run the worker together with NATS:

```bash
# Build images and start NATS + the worker
DETERSL_THREADS=8 scripts/start_detersl_cluster.sh 8

# ... run a client (see below) ...

scripts/stop_detersl_cluster.sh 8
```

`docker-compose.yml` brings up a `nats:2.11` service (ports 4222/8222/6222) and the
worker. The number of Verona runtime threads is controlled by `DETERSL_THREADS`.

## Building from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the worker; the single optional argument is the number of runtime threads
NATSURL=nats://127.0.0.1:4222 ./build/detersl-worker 8
```

The worker reads engine settings from `./config.json` in its working directory.

### Configuration

The worker is configured via environment variables:

| Variable | Default | Purpose |
|----------|---------|---------|
| `NATSURL` | `nats://127.0.0.1:4222` | NATS server URL |
| `SUBJECT` | `detersl.worker` | Base subject (control = `<subject>.core.*`, invoke = `<subject>.invoke`, status = `<subject>.status`) |
| `STREAM` | `DETERSL` | JetStream stream name |
| `DURABLE` | `detersl-mt-worker` | Durable consumer name |
| `BATCH_SIZE` | `500` | Invocation pull batch size |

The CLI argument (default `8`) sets the runtime thread count; the Docker entrypoint
uses `DETERSL_THREADS` (default `6`).

## Using the worker

Workflows and functions are registered and invoked over NATS, using the
`detersl_client` Python library. Each function is a `.wasm` module described by a
sibling `.json` manifest (binary source, I/O events, host-capability links, and
execution policy — see `applications/ycsb/function/transfer.json`).

Typical flow (from `AsyncDeterSLClient`):

```python
from detersl_client import AsyncDeterSLClient

async with AsyncDeterSLClient() as client:
    # Register a Wasm function (loads <name>.wasm + <name>.json from function_dir)
    await client.register_function("transfer", function_dir="applications/ycsb/function")

    # Register a workflow (resource bindings + function graph)
    await client.register_workflow(workflow_payload)

    # Send invocations on <subject>.invoke and collect results/metrics
```

The control plane accepts `register_wasm`, `register_workflow`, and `get_resource`
operations on `<subject>.core.*`; invocations are published to the JetStream
`<subject>.invoke` subject and per-invocation status is emitted on `<subject>.status`.

## Running the example workloads

Use the experiment runner, which starts the cluster, runs a client, and tears down:

```bash
scripts/run_experiment.sh <workload> <input_rate> <n_keys> <n_part> \
  <zipf_const> <client_threads> <total_time> <saving_dir> \
  <warmup_seconds> <epoch_size> [runtime_threads]
```

Supported `<workload>` values:

- `ycsbt` — YCSB-T transactional transfer workload (`applications/ycsb/client-nats.py`)
- `dhr` — deterministic hotel/travel reservation workflow (`applications/hotel-reservation/client-nats.py`)

Example:

```bash
scripts/run_experiment.sh ycsbt 20000 1000000 10 0.0 10 60 results 10 1000 8
```

Results are written to `<saving_dir>` and summarized by each application's
`calculate_metrics.py`. Higher-level drivers
(`run_scalabilty_experiments.sh`, `run_choice_experiments.sh`,
`run_abort_experiments.sh`, `run_batch_experiments.sh`) and
`scripts/create_config.py` generate parameter sweeps for the experiments reported in
the thesis.

## Recompiling Wasm functions

Each application keeps function sources alongside their compiled `.wasm`. To rebuild,
for example, the hotel-reservation functions:

```bash
cd applications/hotel-reservation/function
./compile-all.sh
```
