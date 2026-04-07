#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/run_batch_experiments.sh <config_csv> <saving_dir> <runtime_threads> <partitions> <n_keys> <experiment_time> <warmup_time> [scenarios]

Example:
  scripts/run_batch_experiments.sh scripts/detersl_experiments_config.csv results 4 1 1000 60 10 "ycsbt_uni dhr"
  docker compose -f docker-compose.yml build   # build once before a batch

Environment:
  GENERATE_CONFIG=true|false       default: true
  INCLUDE_EXISTING=true|false      default: false, passed to create_config.py
  CONTINUE_ON_FAILURE=true|false   default: false
EOF
}

if [[ $# -lt 7 ]]; then
  usage >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
script_dir="$repo_root/scripts"
python_bin="${PYTHON_BIN:-python3}"

input="$1"
saving_dir="$2"
runtime_threads="$3"
partitions="$4"
n_keys="$5"
experiment_time="$6"
warmup_time="$7"
scenarios="${8:-ycsbt_uni ycsbt_zipf dhr}"

if [[ "$input" != /* ]]; then
  input="$repo_root/$input"
fi
if [[ "$saving_dir" != /* ]]; then
  saving_dir="$repo_root/$saving_dir"
fi
mkdir -p "$saving_dir"

generate_config="${GENERATE_CONFIG:-true}"
include_existing="${INCLUDE_EXISTING:-false}"
continue_on_failure="${CONTINUE_ON_FAILURE:-false}"

echo "docker compose file: $repo_root/docker-compose.yml"
echo "saving_dir: $saving_dir"
echo "runtime_threads: $runtime_threads"
echo "scenarios: $scenarios"

if [[ "$generate_config" == "true" ]]; then
  scenario_args_string="${scenarios//,/ }"
  read -r -a scenario_args <<< "$scenario_args_string"

  create_config_args=(
    "$script_dir/create_config.py"
    --partitions "$partitions"
    --n_keys "$n_keys"
    --experiment_time "$experiment_time"
    --warmup_time "$warmup_time"
    --scenarios "${scenario_args[@]}"
    --output "$input"
    --results-dir "$saving_dir"
  )

  if [[ "$include_existing" == "true" ]]; then
    create_config_args+=(--include-existing)
  fi

  "$python_bin" "${create_config_args[@]}"
fi

if [[ ! -s "$input" ]]; then
  echo "No experiments to run from $input"
  exit 0
fi

while IFS= read -r line || [[ -n "$line" ]]; do
  [[ -z "$line" ]] && continue
  [[ "$line" == \#* ]] && continue

  printf 'Run DeterSL experiment: %s\n' "$line"
  IFS=',' read -r \
    workload_name \
    input_rate \
    row_n_keys \
    n_part \
    zipf_const \
    client_threads \
    total_time \
    warmup_seconds \
    epoch_size \
    enable_compression \
    use_composite_keys \
    use_fallback_cache <<< "$line"

  if ! "$script_dir/run_experiment.sh" \
      "$workload_name" \
      "$input_rate" \
      "$row_n_keys" \
      "$n_part" \
      "$zipf_const" \
      "$client_threads" \
      "$total_time" \
      "$saving_dir" \
      "$warmup_seconds" \
      "$epoch_size" \
      "$runtime_threads" \
      "${enable_compression:-true}" \
      "${use_composite_keys:-true}" \
      "${use_fallback_cache:-true}"; then
    if [[ "$continue_on_failure" == "true" ]]; then
      echo "Experiment failed, continuing because CONTINUE_ON_FAILURE=true: $line" >&2
      continue
    fi
    exit 1
  fi
done < "$input"
