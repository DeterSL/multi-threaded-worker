#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run_abort_experiments.sh <config_csv> <saving_dir>

Examples:
  scripts/run_abort_experiments.sh scripts/abort_bench_config.csv results/abort-bench

Notes:
  CSV rows are expected to use:
    messages_per_second,client_threads,total_time,warmup_seconds,failure_rate
  The DeterSL cluster uses the default runtime thread count from start_detersl_cluster.sh
  unless DETERSL_THREADS is set in the environment.

Environment:
  PYTHON_BIN=python3                           Python used for the benchmark client
  CONTINUE_ON_FAILURE=true|false               default: false
  DETERSL_STOP_MODE=stop|down                  default: stop
  DETERSL_STARTUP_SLEEP_S=<seconds>            default: 10
EOF
}

if [[ $# -ne 2 ]]; then
  usage >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
script_dir="$repo_root/scripts"
python_bin="${PYTHON_BIN:-python3}"
runtime_threads="${DETERSL_THREADS:-10}"

input="$1"
saving_dir="$2"

if [[ "$input" != /* ]]; then
  input="$repo_root/$input"
fi

if [[ "$saving_dir" != /* ]]; then
  saving_dir="$repo_root/$saving_dir"
fi
mkdir -p "$saving_dir"

if [[ ! -f "$input" ]]; then
  echo "ERROR: config CSV not found: $input" >&2
  exit 2
fi

continue_on_failure="${CONTINUE_ON_FAILURE:-false}"
stop_mode="${DETERSL_STOP_MODE:-stop}"
startup_sleep_s="${DETERSL_STARTUP_SLEEP_S:-10}"

cluster_started=false
cluster_stopped=true

cleanup() {
  local status=$?
  if [[ "$cluster_started" == true && "$cluster_stopped" == false ]]; then
    DETERSL_STOP_MODE=down "$script_dir/stop_detersl_cluster.sh" "$runtime_threads" || true
  fi
  exit "$status"
}
trap cleanup EXIT

run_abort_row() {
  local client_threads="$1"
  local messages_per_second="$2"
  local total_time="$3"
  local warmup_seconds="$4"
  local failure_rate="$5"
  local client_status=0
  local stop_status=0

  "$script_dir/start_detersl_cluster.sh" "$runtime_threads"
  cluster_started=true
  cluster_stopped=false

  sleep "$startup_sleep_s"

  if (
    cd "$repo_root/applications/microbenchmarks/abort-bench"
    "$python_bin" client-nats.py \
      "$saving_dir" \
      "$client_threads" \
      "$messages_per_second" \
      "$total_time" \
      "$warmup_seconds" \
      "$failure_rate"
  ); then
    :
  else
    client_status=$?
  fi

  if DETERSL_STOP_MODE="$stop_mode" "$script_dir/stop_detersl_cluster.sh" "$runtime_threads"; then
    :
  else
    stop_status=$?
  fi
  cluster_stopped=true
  cluster_started=false

  if (( client_status != 0 )); then
    return "$client_status"
  fi
  return "$stop_status"
}

echo "=========== Running DeterSL Abort Sweep ==========="
echo "config_csv: $input"
echo "saving_dir: $saving_dir"
echo "runtime_threads: $runtime_threads"
echo "stop_mode: $stop_mode"
echo "startup_sleep_s: $startup_sleep_s"
echo "==================================================="

ran_experiment=false

printf 'Run abort benchmark sweep from %s with runtime_threads=%s saving_dir=%s\n' \
  "$input" \
  "$runtime_threads" \
  "$saving_dir"

while IFS= read -r line || [[ -n "$line" ]]; do
  [[ -z "${line//[[:space:]]/}" ]] && continue
  [[ "$line" =~ ^[[:space:]]*# ]] && continue

  IFS=',' read -r \
    messages_per_second \
    client_threads \
    total_time \
    warmup_seconds \
    failure_rate \
    extra <<< "$line"

  if [[ "$messages_per_second" == "messages_per_second" ]]; then
    continue
  fi

  if [[ -n "${extra:-}" ]]; then
    echo "ERROR: expected 5 columns, got extra data in row: $line" >&2
    exit 2
  fi

  if [[ -z "${messages_per_second:-}" || -z "${client_threads:-}" || -z "${total_time:-}" || -z "${warmup_seconds:-}" || -z "${failure_rate:-}" ]]; then
    echo "ERROR: incomplete row in $input: $line" >&2
    exit 2
  fi

  ran_experiment=true
  printf 'Run abort benchmark: runtime_threads=%s row=%s\n' "$runtime_threads" "$line"

  if ! run_abort_row \
      "$client_threads" \
      "$messages_per_second" \
      "$total_time" \
      "$warmup_seconds" \
      "$failure_rate"; then
    if [[ "$continue_on_failure" == "true" ]]; then
      echo "Abort benchmark failed, continuing because CONTINUE_ON_FAILURE=true: runtime_threads=$runtime_threads row=$line" >&2
      continue
    fi
    exit 1
  fi
done < "$input"

if [[ "$ran_experiment" == "true" ]]; then
  echo "Abort sweep finished; removing DeterSL containers"
  DETERSL_STOP_MODE=down "$script_dir/stop_detersl_cluster.sh" "$runtime_threads"
else
  echo "No experiments to run from $input"
fi
