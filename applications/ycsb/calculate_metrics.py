import json
import math
import sys
from urllib import error, request
import numpy as np
import pandas as pd
from tqdm import tqdm

# A run with sensible defaults:
# python calculate_metrics.py results 0 10 100 0.0 1 true

def main(
        n_keys,
        input_rate,
        zipf_const,
        client_threads,
        warmup_seconds,
        save_dir,
        run_with_validation
):
    print("Calculating metrics...")

    if zipf_const > 0:
        exp_name = f"ycsbt_zipf_{zipf_const}_{input_rate * client_threads}"
    else:
        exp_name = f"ycsbt_uni_{input_rate * client_threads}"

    starting_money = 1_000_000

    origin_input_msgs = pd.read_csv(
        f"{save_dir}/client_requests.csv",
        dtype={
            "request_id": bytes,
            "latency_ms": np.float64,
            "completed_at_ms": np.uint64,
        },
    ).sort_values("completed_at_ms")

    duplicate_requests = not origin_input_msgs["request_id"].is_unique
    exactly_once_output = origin_input_msgs["request_id"].is_unique

    # remove warmup from input
    input_msgs = origin_input_msgs.loc[
        (
            origin_input_msgs["completed_at_ms"]
            - origin_input_msgs["completed_at_ms"].iloc[0]
            >= warmup_seconds * 1000
        )
    ]
    print(
        f"Removed {len(origin_input_msgs) - len(input_msgs)} input messages "
        f"due to an initial warmup period of {warmup_seconds} seconds."
    )

    print(
        f"Using {len(input_msgs)} client-side samples from client_requests.csv."
    )

    if input_msgs.empty:
        raise ValueError("No samples remain after warmup; cannot compute metrics.")

    # In this mode, `latency_ms` in client_requests.csv is already end-to-end request latency.
    runtime = input_msgs["latency_ms"]
    missed = 0

    start_time = -math.inf
    throughput = {}
    bucket_id = -1

    # 1 second (ms) bucket size.
    granularity = 1000

    for t in input_msgs["completed_at_ms"]:
        if t - start_time > granularity:
            bucket_id += 1
            start_time = t
            throughput[bucket_id] = 1
        else:
            throughput[bucket_id] += 1

    throughput_vals = list(throughput.values())

    req_ids = origin_input_msgs["request_id"]
    dup = origin_input_msgs[req_ids.isin(req_ids[req_ids.duplicated()])].sort_values("request_id")


    res_dict = {
        "duplicate_requests": duplicate_requests,
        "exactly_once_output": exactly_once_output,
        "latency (ms)": {10: np.percentile(runtime, 10),
                        20: np.percentile(runtime, 20),
                        30: np.percentile(runtime, 30),
                        40: np.percentile(runtime, 40),
                        50: np.percentile(runtime, 50),
                        60: np.percentile(runtime, 60),
                        70: np.percentile(runtime, 70),
                        80: np.percentile(runtime, 80),
                        90: np.percentile(runtime, 90),
                        95: np.percentile(runtime, 95),
                        99: np.percentile(runtime, 99),
                        "max": max(runtime),
                        "min": min(runtime),
                        "mean": np.average(runtime)
                        },
        "missed messages": missed,
        "throughput": {
            "max": max(throughput_vals),
            "avg": sum(throughput_vals) / len(throughput_vals),
            "TPS": throughput_vals
        },
        "duplicate_messages": len(dup)
    }

    if run_with_validation:
        output_msgs = pd.read_csv(f"{save_dir}/output.csv", dtype={"timestamp": np.uint64},
                            low_memory=False).sort_values("timestamp")
        # Consistency test
        verification_state_reads = {int(e[0]): int(e[1]) for e in [res.strip("][").split(", ")
                                                                for res in output_msgs["KeyVal"].tail(n_keys)]}
              
        verification_total = sum(verification_state_reads.values())

        total_consistent: bool = verification_total == n_keys * starting_money
        res_dict["total_consistent"] = total_consistent
        res_dict["total_money"] = verification_total
        transaction_operations = [(int(op[0]), int(op[1]))
                                for op in [op.split(" ")[1].split("->")
                                            for op in origin_input_msgs["op"]]]
        true_res = dict.fromkeys(range(n_keys), starting_money)

        for op in tqdm(transaction_operations):
            send_key, rcv_key = op
            true_res[send_key] -= 1
            true_res[rcv_key] += 1

        are_we_consistent = true_res == verification_state_reads
        res_dict["are_we_consistent"] = are_we_consistent

        missing_verification_keys = []
        wrong_values = []
        for res in true_res.items():
            key, value = res
            if key in verification_state_reads and verification_state_reads[key] != value:
                wrong_values.append(f"For key: {key}| the value should be {value}"
                                    f" but it is {verification_state_reads[key]} | "
                                    f"{verification_state_reads[key] - value}")
            elif key not in verification_state_reads:
                missing_verification_keys.append(key)
        missing_verification_keys = len(missing_verification_keys)
        res_dict["missing_verification_keys"] = missing_verification_keys
        res_dict["wrong_values"] = wrong_values

        if not are_we_consistent:
            print(f"{'\033[91m'}NOT CONSISTENT: {verification_total} != {n_keys * starting_money}{'\033[0m'}")

    print(f"Done. Persisted metrics in {save_dir}/{exp_name}.json")
    with open(f"{save_dir}/{exp_name}.json", "w", encoding="utf-8") as f:
        json.dump(res_dict, f, ensure_ascii=False, indent=4)


if __name__ == "__main__":
    # Single-partition CLI:
    # save_dir warmup n_keys input_rate zipf client_threads run_with_validation
    if len(sys.argv) != 8:
        raise ValueError(
            "Usage (single-partition): python calculate_metrics.py <save_dir> <warmup_seconds> "
            "<n_keys> <input_rate> <zipf_const> <client_threads> <run_with_validation>"
        )

    save_dir = sys.argv[1]
    warmup_seconds = int(sys.argv[2])
    n_keys = int(sys.argv[3])
    input_rate = int(sys.argv[4])
    zipf_const = float(sys.argv[5])
    client_threads = int(sys.argv[6])
    run_with_validation = sys.argv[7].lower() == "true"

    main(
        n_keys,
        input_rate,
        zipf_const,
        client_threads,
        warmup_seconds,
        save_dir,
        run_with_validation
    )
