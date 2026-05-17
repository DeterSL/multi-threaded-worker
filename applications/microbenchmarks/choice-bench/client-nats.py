import argparse
import asyncio
import csv
import json
import math
import multiprocessing
import random
import statistics
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from timeit import default_timer as timer

APP_DIR = Path(__file__).resolve().parent
FUNCTION_DIR = APP_DIR / "function"
REPO_ROOT = APP_DIR.parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from detersl_client import AsyncDeterSLClient

multiprocessing.set_start_method("fork", force=True)

METRICS_PULL_BATCH = 1000
METRICS_PULL_TIMEOUT_S = 0.1
METRICS_COLLECT_TIMEOUT_S = 600.0
SLEEPS_PER_SECOND = 10
SLEEP_TIME_S = 0.0085

RUN_WITH_CHOICE = sys.argv[1].lower() == "true"
SAVE_DIR: str = sys.argv[2]
THREADS = int(sys.argv[3])
MESSAGES_PER_SECOND = int(sys.argv[4])
SECONDS = int(sys.argv[5])
WARMUP_SECONDS = int(sys.argv[6])
MANUAL_REVIEW_RATIO = float(sys.argv[7])
TOTAL_THROUGHPUT = THREADS * MESSAGES_PER_SECOND
NUM_HOTELS = 200
NUM_FLIGHTS = 200
SEED = 230143093
INIT_CAPACITY = 1000
RUN_TYPE = "with-choice" if RUN_WITH_CHOICE else "no-choice"


def format_filename_number(value: float) -> str:
    text = f"{value:.6f}".rstrip("0").rstrip(".")
    if not text:
        text = "0"
    return text.replace(".", "_")


RUN_FILE_TAG = (
    f"type_{RUN_TYPE.replace('-', '_')}_"
    f"ratio_{format_filename_number(MANUAL_REVIEW_RATIO)}_"
    f"total_tput_{TOTAL_THROUGHPUT}"
)


def parse_args() -> argparse.Namespace:
    return argparse.Namespace(
        variant="with-choice" if RUN_WITH_CHOICE else "no-choice",
        save_dir=SAVE_DIR,
        threads=THREADS,
        messages_per_second=MESSAGES_PER_SECOND,
        seconds=SECONDS,
        warmup_seconds=WARMUP_SECONDS,
        manual_review_ratio=MANUAL_REVIEW_RATIO,
        num_hotels=NUM_HOTELS,
        num_flights=NUM_FLIGHTS,
        seed=SEED,
        init_capacity=INIT_CAPACITY,
        can_abort=None,
    )

def epoch_ms() -> int:
    return time.time_ns() // 1_000_000

def make_detersl_client() -> AsyncDeterSLClient:
    return AsyncDeterSLClient(
        publish_async_max_pending=50000,
        metrics_pull_batch=METRICS_PULL_BATCH,
        metrics_pull_timeout_s=METRICS_PULL_TIMEOUT_S,
        metrics_wait_timeout_s=METRICS_COLLECT_TIMEOUT_S,
    )


def checkout_tail() -> list[dict]:
    return [
        {
            "type": "Task",
            "func_id": "reserve-hotel",
            "resources": {
                "hotel_data": "&hotel_id:w",
            },
        },
        {
            "type": "Task",
            "func_id": "reserve-flight",
            "resources": {
                "flight_data": "&flight_id:w",
            },
        },
        {
            "type": "Task",
            "func_id": "create-order",
            "resources": {
                "hotel_id": "$hotel_id",
                "flight_id": "$flight_id",
                "user_id": "$user_id",
                "order_key": "&order_key:w",
            },
        },
    ]


async def init_data(client: AsyncDeterSLClient, entries) -> None:
    request_ids = await client.publish_invocations(
        {
            "workflow_id": "cb_write_string",
            "input": {
                "key": key,
                "value": value,
            },
        }
        for key, value in entries
    )
    await client.wait_for_requests(request_ids)


async def choice_bench_init(client: AsyncDeterSLClient, args: argparse.Namespace) -> None:
    await client.register_functions(
        ["write-string", "reserve-hotel", "reserve-flight", "create-order"],
        function_dir=FUNCTION_DIR,
    )

    await client.register_workflow(
        {
            "id": "cb_write_string",
            "tasks": [
                {
                    "type": "Task",
                    "func_id": "write-string",
                    "resources": {
                        "key": "&key:w",
                        "value": "$value",
                    },
                }
            ],
        }
    )

    await client.register_workflow(
        {
            "id": "checkout_no_choice",
            "tasks": checkout_tail(),
        }
    )

    await client.register_workflow(
        {
            "id": "checkout_with_choice",
            "tasks": [
                {
                    "type": "Choice",
                    "choices": [
                        {
                            "variable": "$manual_review",
                            "bool_eq": True,
                            "tasks": [
                                {
                                    "type": "Task",
                                    "func_id": "write-string",
                                    "resources": {
                                        "key": "&_review_log:w",
                                        "value": "$review_payload",
                                    },
                                },
                                *checkout_tail(),
                            ],
                        },
                        {
                            "variable": "$manual_review",
                            "bool_eq": False,
                            "tasks": checkout_tail(),
                        },
                    ],
                }
            ],
        }
    )

    capacity = INIT_CAPACITY
    if capacity is None:
        capacity = max(
            1000,
            THREADS * MESSAGES_PER_SECOND * (SECONDS + max(WARMUP_SECONDS, 1)) * 2,
        )

    hotel_entries = (
        (f"hotel{i}", {"Cap": capacity, "Customers": []})
        for i in range(NUM_HOTELS)
    )
    flight_entries = (
        (f"flight{i}", {"Cap": capacity, "Customers": []})
        for i in range(NUM_FLIGHTS)
    )
    await init_data(client, hotel_entries)
    await init_data(client, flight_entries)


def make_request_input(worker_id: int, seq_no: int, args_dict: dict, rng: random.Random) -> tuple[str, dict]:
    workflow_id = "checkout_no_choice" if args_dict["variant"] == "no-choice" else "checkout_with_choice"
    manual_review = rng.random() < args_dict["manual_review_ratio"]
    hotel_id = f"hotel{rng.randrange(args_dict['num_hotels'])}"
    flight_id = f"flight{rng.randrange(args_dict['num_flights'])}"
    order_key = f"order-{worker_id}-{seq_no}"
    payload = {
        "hotel_id": hotel_id,
        "flight_id": flight_id,
        "user_id": "benchmark-user",
        "order_key": order_key,
        "manual_review": manual_review,
        "_review_log": f"review-log-{worker_id}-{seq_no}",
        "review_payload": {
            "order_key": order_key,
            "reason": "manual-review",
            "worker": worker_id,
            "sequence": seq_no,
        },
    }
    return workflow_id, payload


async def ack_with_meta(ack_fut, meta: dict) -> tuple[object, dict]:
    ack = await ack_fut
    return ack, meta


async def benchmark_runner(worker_id: int, args_dict: dict) -> dict[int, dict]:
    client = make_detersl_client()
    await client.connect()
    rng = random.Random(args_dict["seed"] + worker_id)
    next_seq_no = 0
    tasks = []
    results: dict[int, dict] = {}
    try:
        for _second in range(args_dict["seconds"]):
            sec_start = timer()
            step = max(1, args_dict["messages_per_second"] // SLEEPS_PER_SECOND)
            for i in range(args_dict["messages_per_second"]):
                if i % step == 0:
                    await asyncio.sleep(SLEEP_TIME_S)
                workflow_id, workflow_input = make_request_input(worker_id, next_seq_no, args_dict, rng)
                
                invoke_kwargs = {}
                if args_dict["can_abort"] is not None:
                    invoke_kwargs["can_abort"] = args_dict["can_abort"]
                ack_fut = await client.invoke_workflow(
                    workflow_id,
                    workflow_input,
                    **invoke_kwargs,
                )
                started_at_ms = epoch_ms()
                tasks.append(
                    asyncio.create_task(
                        ack_with_meta(
                            ack_fut,
                            {
                                "workflow_id": workflow_id,
                                "worker_id": worker_id,
                                "sequence": next_seq_no,
                                "order_key": workflow_input["order_key"],
                                "manual_review": workflow_input["manual_review"],
                                "started_at_ms": started_at_ms,
                            },
                        )
                    )
                )
                next_seq_no += 1

            elapsed_s = timer() - sec_start
            if elapsed_s < 1.0:
                await asyncio.sleep(1.0 - elapsed_s)

        for task in asyncio.as_completed(tasks):
            ack, meta = await task
            results[int(ack.seq)] = meta

        return results
    finally:
        await client.close()


def run_worker(worker_id: int, args_dict: dict) -> dict[int, dict]:
    return asyncio.run(benchmark_runner(worker_id, args_dict))


async def run_pool(args_dict: dict) -> list[dict[int, dict]]:
    loop = asyncio.get_running_loop()
    with ProcessPoolExecutor(max_workers=args_dict["threads"]) as executor:
        tasks = [
            loop.run_in_executor(executor, run_worker, worker_id, args_dict)
            for worker_id in range(args_dict["threads"])
        ]
        return await asyncio.gather(*tasks)


def percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    if len(values) == 1:
        return values[0]
    rank = (pct / 100.0) * (len(values) - 1)
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return values[low]
    weight = rank - low
    return values[low] * (1.0 - weight) + values[high] * weight


def build_summary(records: list[dict], args: argparse.Namespace) -> dict:
    if not records:
        return {
            "type": RUN_TYPE,
            "with_choice": RUN_WITH_CHOICE,
            "ratio": MANUAL_REVIEW_RATIO,
            "manual_review_ratio": MANUAL_REVIEW_RATIO,
            "total_throughput": TOTAL_THROUGHPUT,
            "requests_total": 0,
            "requests_after_warmup": 0,
        }

    first_start_ms = min(record["started_at_ms"] for record in records)
    warmup_cutoff_ms = first_start_ms + WARMUP_SECONDS * 1000
    filtered = [record for record in records if record["started_at_ms"] >= warmup_cutoff_ms]
    latencies = sorted(record["latency_ms"] for record in filtered)

    summary = {
        "type": RUN_TYPE,
        "with_choice": RUN_WITH_CHOICE,
        "ratio": MANUAL_REVIEW_RATIO,
        "manual_review_ratio": MANUAL_REVIEW_RATIO,
        "threads": THREADS,
        "messages_per_second": MESSAGES_PER_SECOND,
        "total_throughput": TOTAL_THROUGHPUT,
        "seconds": SECONDS,
        "warmup_seconds": WARMUP_SECONDS,
        "requests_total": len(records),
        "requests_after_warmup": len(filtered),
        "first_started_at_ms": first_start_ms,
        "warmup_cutoff_ms": warmup_cutoff_ms,
    }

    if not latencies:
        return summary

    completed_window_ms = max(record["completed_at_ms"] for record in filtered) - min(
        record["started_at_ms"] for record in filtered
    )
    throughput_rps = None
    if completed_window_ms > 0:
        throughput_rps = len(filtered) / (completed_window_ms / 1000.0)

    summary["latency_ms"] = {
        "mean": statistics.fmean(latencies),
        "min": latencies[0],
        "p50": percentile(latencies, 50.0),
        "p95": percentile(latencies, 95.0),
        "p99": percentile(latencies, 99.0),
        "max": latencies[-1],
    }
    summary["throughput_rps_after_warmup"] = throughput_rps
    summary["manual_review_taken_after_warmup"] = sum(
        1 for record in filtered if record["manual_review"]
    )
    return summary


def write_outputs(save_dir: Path, records: list[dict], summary: dict) -> None:
    save_dir.mkdir(parents=True, exist_ok=True)

    csv_path = save_dir / f"client_requests_{RUN_FILE_TAG}.csv"
    fieldnames = [
        "request_id",
        "workflow_id",
        "worker_id",
        "sequence",
        "order_key",
        "manual_review",
        "started_at_ms",
        "completed_at_ms",
        "latency_ms",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        for record in sorted(records, key=lambda item: item["completed_at_ms"]):
            writer.writerow({name: record[name] for name in fieldnames})

    summary_path = save_dir / f"summary_{RUN_FILE_TAG}.json"
    with summary_path.open("w", encoding="utf-8") as file:
        json.dump(summary, file, indent=2, sort_keys=True)


async def main() -> None:
    args = parse_args()
    args_dict = vars(args).copy()

    client = make_detersl_client()
    await client.connect(start_metrics=True)
    try:
        await choice_bench_init(client, args)

        worker_results = await run_pool(args_dict)
        request_meta = {request_id: meta for result in worker_results for request_id, meta in result.items()}

        workflow_metrics = await client.wait_for_requests(request_meta.keys())
        records = []
        for request_id, meta in request_meta.items():
            metric = workflow_metrics[request_id]
            completed_at_ms = metric["completed_at"]
            records.append(
                {
                    "request_id": request_id,
                    "workflow_id": meta["workflow_id"],
                    "worker_id": meta["worker_id"],
                    "sequence": meta["sequence"],
                    "order_key": meta["order_key"],
                    "manual_review": meta["manual_review"],
                    "started_at_ms": meta["started_at_ms"],
                    "completed_at_ms": completed_at_ms,
                    "latency_ms": completed_at_ms - meta["started_at_ms"],
                }
            )

        summary = build_summary(records, args)
        write_outputs(Path(SAVE_DIR), records, summary)
    finally:
        await client.close()


if __name__ == "__main__":
    asyncio.run(main())
