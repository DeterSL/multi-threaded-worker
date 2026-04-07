import json
import math
import sys

import numpy as np
import pandas as pd

# A run with sensible defaults:
# python calculate_metrics.py results 0 10 100 0.0 1 true

def main(
        save_dir,
        input_rate,        
        warmup_seconds,
        client_threads,
):
    print("Calculating metrics...")

    exp_name = f"d_hotel_reservation_{input_rate * client_threads}"

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

    print(f"Done. Persisted metrics in {save_dir}/{exp_name}.json")
    with open(f"{save_dir}/{exp_name}.json", "w", encoding="utf-8") as f:
        json.dump(res_dict, f, ensure_ascii=False, indent=4)


if __name__ == "__main__":
    # Single-partition CLI:
    # save_dir warmup n_keys input_rate zipf client_threads run_with_validation
    main(
        sys.argv[1],
        int(sys.argv[2]),
        int(sys.argv[3]),
        int(sys.argv[4]),
    )
