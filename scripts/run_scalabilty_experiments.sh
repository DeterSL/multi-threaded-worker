#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run_scalabilty_experiments.sh <config_csv> <saving_dir> <runtime_threads...>

Examples:
  scripts/run_scalabilty_experiments.sh scripts/detersl_experiments_config.csv results/scalability 1 2 4 8
  scripts/run_scalabilty_experiments.sh scripts/detersl_experiments_config.csv results/scalability "1,2,4,8"

Notes:
  Runtime thread counts can be passed as repeated arguments and/or comma-separated lists.
  The CSV is expected to follow the same row format as run_batch_experiments.sh:
    workload,input_rate,n_keys,n_part,zipf,threads,time,warmup,epoch,compression,composite_keys,fallback_cache
  Each runtime-thread count is written to <saving_dir>/runtime_threads_<N>/ to avoid overwriting results.

Environment:
  CONTINUE_ON_FAILURE=true|false   default: false
  DETERSL_STOP_MODE=stop|down      default: down for each run
EOF
}

if [[ $# -lt 3 ]]; then
  usage >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
script_dir="$repo_root/scripts"

input="$1"
saving_dir="$2"
shift 2

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

declare -a runtime_thread_values=()
for raw_value in "$@"; do
  normalized_value="${raw_value//,/ }"
  read -r -a split_values <<< "$normalized_value"
  for thread_value in "${split_values[@]}"; do
    [[ -z "$thread_value" ]] && continue
    if ! [[ "$thread_value" =~ ^[0-9]+$ ]] || (( thread_value <= 0 )); then
      echo "ERROR: invalid runtime thread count: $thread_value" >&2
      exit 2
    fi
    runtime_thread_values+=("$thread_value")
  done
done

if [[ ${#runtime_thread_values[@]} -eq 0 ]]; then
  echo "ERROR: at least one runtime thread count is required" >&2
  exit 2
fi

echo "============ Running DeterSL Scalability Sweep ============"
echo "config_csv: $input"
echo "saving_dir: $saving_dir"
echo "runtime_threads: ${runtime_thread_values[*]}"
echo "stop_mode: $stop_mode"
echo "==========================================================="

ran_experiment=false
last_runtime_threads=""

for runtime_threads in "${runtime_thread_values[@]}"; do
  run_saving_dir="$saving_dir/runtime_threads_${runtime_threads}"
  mkdir -p "$run_saving_dir"

  printf 'Run DeterSL scalability experiments from %s with runtime_threads=%s saving_dir=%s\n' \
    "$input" \
    "$runtime_threads" \
    "$run_saving_dir"

  last_runtime_threads="$runtime_threads"

  while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" ]] && continue
    [[ "$line" == \#* ]] && continue

    ran_experiment=true
    printf 'Run DeterSL experiment: runtime_threads=%s row=%s\n' "$runtime_threads" "$line"

    IFS=',' read -r \
      workload_name \
      input_rate \
      n_keys \
      n_part \
      zipf_const \
      client_threads \
      total_time \
      warmup_seconds \
      epoch_size \
      enable_compression \
      use_composite_keys \
      use_fallback_cache <<< "$line"

    if ! DETERSL_STOP_MODE="$stop_mode" "$script_dir/run_experiment.sh" \
        "$workload_name" \
        "$input_rate" \
        "$n_keys" \
        "$n_part" \
        "$zipf_const" \
        "$client_threads" \
        "$total_time" \
        "$run_saving_dir" \
        "$warmup_seconds" \
        "$epoch_size" \
        "$runtime_threads" \
        "${enable_compression:-true}" \
        "${use_composite_keys:-true}" \
        "${use_fallback_cache:-true}"; then
      if [[ "$continue_on_failure" == "true" ]]; then
        echo "Experiment failed, continuing because CONTINUE_ON_FAILURE=true: runtime_threads=$runtime_threads row=$line" >&2
        continue
      fi
      exit 1
    fi
  done < "$input"
done

if [[ "$ran_experiment" == "true" ]]; then
  echo "Scalability sweep finished; removing DeterSL containers"
  DETERSL_STOP_MODE=down "$script_dir/stop_detersl_cluster.sh" "$last_runtime_threads"
else
  echo "No experiments to run from $input"
fi
