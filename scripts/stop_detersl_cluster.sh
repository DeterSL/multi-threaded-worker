#!/usr/bin/env bash
set -euo pipefail

runtime_threads="${1:-${DETERSL_THREADS:-4}}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compose_cmd=(docker compose -f "$repo_root/docker-compose.yml")
ts="$(date +"%Y%m%d-%H%M%S")"

echo "============== Stopping DeterSL Cluster =============="
echo "runtime_threads: $runtime_threads"
echo "compose_file: $repo_root/docker-compose.yml"
echo "======================================================"

mkdir -p "$repo_root/logs"
DETERSL_THREADS="$runtime_threads" "${compose_cmd[@]}" logs runtime > "$repo_root/logs/detersl-runtime-${ts}.log" 2>/dev/null || true
DETERSL_THREADS="$runtime_threads" "${compose_cmd[@]}" logs nats > "$repo_root/logs/detersl-nats-${ts}.log" 2>/dev/null || true
DETERSL_THREADS="$runtime_threads" "${compose_cmd[@]}" down --volumes --remove-orphans
