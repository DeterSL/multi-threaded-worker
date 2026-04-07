#!/usr/bin/env bash
set -euo pipefail

runtime_threads="${1:-${DETERSL_THREADS:-4}}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compose_cmd=(docker compose -f "$repo_root/docker-compose.yml")

echo "============= Starting DeterSL Cluster ============="
echo "runtime_threads: $runtime_threads"
echo "compose_file: $repo_root/docker-compose.yml"
echo "build: false"
echo "===================================================="

DETERSL_THREADS="$runtime_threads" "${compose_cmd[@]}" up -d --no-build
