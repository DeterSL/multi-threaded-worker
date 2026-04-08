#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run_experiment.sh <workload> <input_rate> <n_keys> <n_part> <zipf_const> <client_threads> <total_time> <saving_dir> <warmup_seconds> <epoch_size> [runtime_threads]

Workloads:
  ycsbt   Run applications/ycsb/client-nats.py
  dhr     Run applications/hotel-reservation/client-nats.py

Environment:
  PYTHON_BIN=python3                           Python used for clients
  DETERSL_STOP_MODE=stop|down                  default: down; stop preserves container filesystem cache
EOF
}

if [[ $# -lt 10 ]]; then
  usage >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python_bin="${PYTHON_BIN:-python3}"

workload_name="$1"
input_rate="$2"
n_keys="$3"
n_part="$4"
zipf_const="$5"
client_threads="$6"
total_time="$7"
saving_dir="$8"
warmup_seconds="$9"
epoch_size="${10}"
runtime_threads="${11:-${DETERSL_THREADS:-4}}"

enable_compression="${12:-true}"
use_composite_keys="${13:-true}"
use_fallback_cache="${14:-true}"

if [[ "$saving_dir" != /* ]]; then
  saving_dir="$repo_root/$saving_dir"
fi
mkdir -p "$saving_dir"

cluster_started=false
cluster_stopped=false

cleanup() {
  local status=$?
  if [[ "$cluster_started" == true && "$cluster_stopped" == false ]]; then
    cluster_stopped=true
    "$repo_root/scripts/stop_detersl_cluster.sh" "$runtime_threads" || true
  fi
  exit "$status"
}
trap cleanup EXIT

echo "============= Running DeterSL Experiment ============="
echo "workload_name: $workload_name"
echo "input_rate: $input_rate"
echo "n_keys: $n_keys"
echo "n_part: $n_part"
echo "zipf_const: $zipf_const"
echo "client_threads: $client_threads"
echo "total_time: $total_time"
echo "saving_dir: $saving_dir"
echo "warmup_seconds: $warmup_seconds"
echo "epoch_size: $epoch_size"
echo "runtime_threads: $runtime_threads"
echo "ignored_styx_enable_compression: $enable_compression"
echo "ignored_styx_use_composite_keys: $use_composite_keys"
echo "ignored_styx_use_fallback_cache: $use_fallback_cache"
echo "======================================================="

run_ycsb() {
  local run_with_validation="${RUN_WITH_VALIDATION:-false}"
  (
    cd "$repo_root/applications/ycsb"
    "$python_bin" client-nats.py \
      "$client_threads" \
      "$n_keys" \
      "$zipf_const" \
      "$input_rate" \
      "$total_time" \
      "$saving_dir" \
      "$warmup_seconds" \
      "$run_with_validation"
  )
}

run_hotel_reservation() {
  (
    cd "$repo_root/applications/hotel-reservation"
    "$python_bin" client-nats.py \
      "$saving_dir" \
      "$client_threads" \
      "$n_part" \
      "$input_rate" \
      "$total_time" \
      "$warmup_seconds"
  )
}

"$repo_root/scripts/start_detersl_cluster.sh" "$runtime_threads"
cluster_started=true
sleep 10

case "$workload_name" in
  ycsbt|ycsb|ycsbt_uni|ycsbt_zipf)
    run_ycsb
    ;;
  dhr|hotel|hotel-reservation|d_hotel_reservation)
    run_hotel_reservation
    ;;
  *)
    echo "ERROR: benchmark not supported: $workload_name" >&2
    exit 2
    ;;
esac

stop_status=0
"$repo_root/scripts/stop_detersl_cluster.sh" "$runtime_threads" || stop_status=$?
cluster_stopped=true
trap - EXIT
exit "$stop_status"
