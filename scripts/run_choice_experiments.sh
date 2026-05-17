#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run_choice_experiments.sh <config_csv> <saving_dir>

Examples:
  scripts/run_choice_experiments.sh path/to/choice_bench_config.csv results/choice-bench

Notes:
  CSV rows are expected to use:
    messages_per_second,client_threads,total_time,warmup_seconds,variant,manual_review_ratio
  variant accepts:
    no-choice | with-choice
  Each row writes into the same <saving_dir>. Output filenames include the
  variant type, ratio, and total throughput.
  The DeterSL cluster uses the default runtime thread count from
  start_detersl_cluster.sh unless DETERSL_THREADS is set in the environment.

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

trim() {
  local value="$1"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

normalize_variant() {
  local variant
  variant="$(trim "$1")"
  variant="${variant,,}"

  case "$variant" in
    no-choice|no_choice|without-choice|without_choice|false|0)
      printf 'no-choice'
      ;;
    with-choice|with_choice|true|1)
      printf 'with-choice'
      ;;
    *)
      return 1
      ;;
  esac
}

variant_to_bool() {
  case "$1" in
    no-choice)
      printf 'false'
      ;;
    with-choice)
      printf 'true'
      ;;
    *)
      return 1
      ;;
  esac
}

run_choice_row() {
  local client_threads="$1"
  local messages_per_second="$2"
  local total_time="$3"
  local warmup_seconds="$4"
  local variant="$5"
  local manual_review_ratio="$6"
  local client_status=0
  local stop_status=0
  local run_with_choice
  local total_throughput

  run_with_choice="$(variant_to_bool "$variant")"
  total_throughput=$(( client_threads * messages_per_second ))

  printf 'Run choice benchmark: runtime_threads=%s variant=%s manual_review_ratio=%s total_throughput=%s saving_dir=%s\n' \
    "$runtime_threads" \
    "$variant" \
    "$manual_review_ratio" \
    "$total_throughput" \
    "$saving_dir"

  "$script_dir/start_detersl_cluster.sh" "$runtime_threads"
  cluster_started=true
  cluster_stopped=false

  sleep "$startup_sleep_s"

  if (
    cd "$repo_root/applications/microbenchmarks/choice-bench"
    "$python_bin" client-nats.py \
      "$run_with_choice" \
      "$saving_dir" \
      "$client_threads" \
      "$messages_per_second" \
      "$total_time" \
      "$warmup_seconds" \
      "$manual_review_ratio"
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

echo "=========== Running DeterSL Choice Sweep ==========="
echo "config_csv: $input"
echo "saving_dir: $saving_dir"
echo "runtime_threads: $runtime_threads"
echo "stop_mode: $stop_mode"
echo "startup_sleep_s: $startup_sleep_s"
echo "===================================================="

ran_experiment=false

printf 'Run choice benchmark sweep from %s with runtime_threads=%s saving_dir=%s\n' \
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
    variant \
    manual_review_ratio \
    extra <<< "$line"

  messages_per_second="$(trim "${messages_per_second:-}")"
  client_threads="$(trim "${client_threads:-}")"
  total_time="$(trim "${total_time:-}")"
  warmup_seconds="$(trim "${warmup_seconds:-}")"
  variant="$(trim "${variant:-}")"
  manual_review_ratio="$(trim "${manual_review_ratio:-}")"
  extra="$(trim "${extra:-}")"

  if [[ "$messages_per_second" == "messages_per_second" ]]; then
    continue
  fi

  if [[ -n "$extra" ]]; then
    echo "ERROR: expected 6 columns, got extra data in row: $line" >&2
    exit 2
  fi

  if [[ -z "$messages_per_second" || -z "$client_threads" || -z "$total_time" || -z "$warmup_seconds" || -z "$variant" || -z "$manual_review_ratio" ]]; then
    echo "ERROR: incomplete row in $input: $line" >&2
    exit 2
  fi

  if ! variant="$(normalize_variant "$variant")"; then
    echo "ERROR: invalid variant in row: $line" >&2
    echo "Expected variant to be one of: no-choice, with-choice" >&2
    exit 2
  fi

  if [[ "$variant" == "no-choice" && ! "$manual_review_ratio" =~ ^0+([.]0+)?$ ]]; then
    echo "ERROR: manual_review_ratio must be 0 for no-choice rows: $line" >&2
    exit 2
  fi

  ran_experiment=true

  if ! run_choice_row \
      "$client_threads" \
      "$messages_per_second" \
      "$total_time" \
      "$warmup_seconds" \
      "$variant" \
      "$manual_review_ratio"; then
    if [[ "$continue_on_failure" == "true" ]]; then
      echo "Choice benchmark failed, continuing because CONTINUE_ON_FAILURE=true: runtime_threads=$runtime_threads row=$line" >&2
      continue
    fi
    exit 1
  fi
done < "$input"

if [[ "$ran_experiment" == "true" ]]; then
  echo "Choice sweep finished; removing DeterSL containers"
  DETERSL_STOP_MODE=down "$script_dir/stop_detersl_cluster.sh" "$runtime_threads"
else
  echo "No experiments to run from $input"
fi
