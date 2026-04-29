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
from dataclasses import asdict, dataclass
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
ORDER_INSPECTION_CONCURRENCY = 256
WRITE_WORKFLOW_ID = "non_abort_hotel_bench_write_string"
CHECKOUT_WORKFLOW_ID = "non_abort_hotel_bench_checkout"

SAVE_DIR = Path(sys.argv[1])
THREADS = int(sys.argv[2])
MESSAGES_PER_SECOND = int(sys.argv[3])
SECONDS = int(sys.argv[4])
WARMUP_SECONDS = int(sys.argv[5])
FAILURE_RATE = float(sys.argv[6])
NUM_HOTELS = 1000
NUM_FLIGHTS = 1000

def epoch_ms() -> int:
    return time.time_ns() // 1_000_000


RUN_ID = f"run-{epoch_ms()}"

CAPACITY = 1000
SEED = 230143093
USER_ID = "benchmark-user"

@dataclass(frozen=True)
class CaseConfig:
    name: str
    can_abort: bool


ALL_CASES = {
    "aborting": CaseConfig(
        name="aborting",
        can_abort=True,
    ),
    "non-aborting": CaseConfig(
        name="non-aborting",
        can_abort=False,
    ),
}

def make_detersl_client() -> AsyncDeterSLClient:
    return AsyncDeterSLClient(
        publish_async_max_pending=50000,
        metrics_pull_batch=METRICS_PULL_BATCH,
        metrics_pull_timeout_s=METRICS_PULL_TIMEOUT_S,
        metrics_wait_timeout_s=METRICS_COLLECT_TIMEOUT_S,
    )


def hotel_keys() -> list[str]:
    return [f"hotel{i}" for i in range(NUM_HOTELS)]


def flight_keys() -> list[str]:
    return [f"flight{i}" for i in range(NUM_FLIGHTS)]


def checkout_workflow() -> dict:
    return {
        "id": CHECKOUT_WORKFLOW_ID,
        "tasks": [
            {
                "type": "Task",
                "func_id": "reserve-hotel",
                "resources": {
                    "hotel_data": "&hotel_id:w",
                    "should_fail": "$should_fail",
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
        ],
    }


async def init_data(client: AsyncDeterSLClient, entries) -> None:
    request_ids = await client.publish_invocations(
        {
            "workflow_id": WRITE_WORKFLOW_ID,
            "input": {
                "key": key,
                "value": value,
            },
        }
        for key, value in entries
    )
    await client.wait_for_requests(request_ids)


async def register_application(client: AsyncDeterSLClient) -> None:
    await client.register_functions(
        ["write-string", "reserve-hotel", "reserve-flight", "create-order"],
        function_dir=FUNCTION_DIR,
    )

    await client.register_workflow(
        {
            "id": WRITE_WORKFLOW_ID,
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

    await client.register_workflow(checkout_workflow())


async def prepare_case_data(
    client: AsyncDeterSLClient,
) -> None:
    await init_data(
        client,
        (
            (key, {"Cap": CAPACITY, "Customers": []})
            for key in hotel_keys()
        ),
    )
    await init_data(
        client,
        (
            (key, {"Cap": CAPACITY, "Customers": []})
            for key in flight_keys()
        ),
    )


def make_request_input(
    worker_id: int,
    seq_no: int,
    case_dict: dict,
    rng: random.Random,
) -> dict:
    hotel_id = f"hotel{rng.randrange(NUM_HOTELS)}"
    flight_id = f"flight{rng.randrange(NUM_FLIGHTS)}"
    return {
        "hotel_id": hotel_id,
        "flight_id": flight_id,
        "user_id": USER_ID,
        "should_fail": rng.random() < FAILURE_RATE,
        "order_key": (
            f"{case_dict['name']}-{RUN_ID}-"
            f"order-{worker_id}-{seq_no}"
        ),
    }


async def ack_with_meta(ack_fut, meta: dict) -> tuple[object, dict]:
    ack = await ack_fut
    return ack, meta


async def benchmark_runner(worker_id: int, case_dict: dict) -> dict[int, dict]:
    client = make_detersl_client()
    await client.connect()
    rng = random.Random(SEED + worker_id)
    next_seq_no = 0
    tasks = []
    results: dict[int, dict] = {}
    try:
        for _second in range(SECONDS):
            sec_start = timer()
            step = max(1, MESSAGES_PER_SECOND // SLEEPS_PER_SECOND)
            for i in range(MESSAGES_PER_SECOND):
                if i % step == 0:
                    await asyncio.sleep(SLEEP_TIME_S)
                workflow_input = make_request_input(
                    worker_id,
                    next_seq_no,
                    case_dict,
                    rng,
                )
                ack_fut = await client.invoke_workflow(
                    CHECKOUT_WORKFLOW_ID,
                    workflow_input,
                    can_abort=case_dict["can_abort"],
                )
                started_at_ms = epoch_ms()
                tasks.append(
                    asyncio.create_task(
                        ack_with_meta(
                            ack_fut,
                            {
                                "case_name": case_dict["name"],
                                "can_abort": case_dict["can_abort"],
                                "worker_id": worker_id,
                                "sequence": next_seq_no,
                                "hotel_id": workflow_input["hotel_id"],
                                "flight_id": workflow_input["flight_id"],
                                "requested_failure": workflow_input["should_fail"],
                                "order_key": workflow_input["order_key"],
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


def run_worker(worker_id: int, case_dict: dict) -> dict[int, dict]:
    return asyncio.run(benchmark_runner(worker_id, case_dict))


async def run_pool(case_dict: dict) -> list[dict[int, dict]]:
    loop = asyncio.get_running_loop()
    with ProcessPoolExecutor(max_workers=THREADS) as executor:
        tasks = [
            loop.run_in_executor(executor, run_worker, worker_id, case_dict)
            for worker_id in range(THREADS)
        ]
        return await asyncio.gather(*tasks)


async def inspect_order_commits(
    client: AsyncDeterSLClient,
    order_keys: list[str],
) -> dict[str, bool]:
    semaphore = asyncio.Semaphore(ORDER_INSPECTION_CONCURRENCY)
    commits: dict[str, bool] = {}

    async def fetch_one(order_key: str) -> tuple[str, bool]:
        async with semaphore:
            data = await client.get_resource(order_key)
        return order_key, bool(data)

    tasks = [asyncio.create_task(fetch_one(order_key)) for order_key in order_keys]
    for task in asyncio.as_completed(tasks):
        order_key, committed = await task
        commits[order_key] = committed
    return commits


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


def build_summary(records: list[dict], case: CaseConfig) -> dict:
    summary = {
        "case_name": case.name,
        "can_abort": case.can_abort,
        "run_id": RUN_ID,
        "configured_failure_rate": FAILURE_RATE,
        "threads": THREADS,
        "messages_per_second": MESSAGES_PER_SECOND,
        "seconds": SECONDS,
        "warmup_seconds": WARMUP_SECONDS,
        "num_hotels": NUM_HOTELS,
        "num_flights": NUM_FLIGHTS,
        "capacity": CAPACITY,
        "requests_total": len(records),
        "requested_failures_total": sum(1 for record in records if record["requested_failure"]),
        "orders_committed_total": sum(1 for record in records if record["order_committed"]),
        "workflow_failed_total": sum(1 for record in records if record["failed"]),
    }

    if not records:
        summary["requests_after_warmup"] = 0
        return summary

    first_start_ms = min(record["started_at_ms"] for record in records)
    warmup_cutoff_ms = first_start_ms + WARMUP_SECONDS * 1000
    filtered = [record for record in records if record["started_at_ms"] >= warmup_cutoff_ms]
    summary["first_started_at_ms"] = first_start_ms
    summary["warmup_cutoff_ms"] = warmup_cutoff_ms
    summary["requests_after_warmup"] = len(filtered)
    summary["requested_failures_after_warmup"] = sum(
        1 for record in filtered if record["requested_failure"]
    )
    summary["orders_committed_after_warmup"] = sum(
        1 for record in filtered if record["order_committed"]
    )
    summary["workflow_failed_after_warmup"] = sum(
        1 for record in filtered if record["failed"]
    )

    if records:
        summary["requested_failure_rate_sampled_total"] = (
            summary["requested_failures_total"] / len(records)
        )
        summary["observed_workflow_failure_rate_total"] = (
            summary["workflow_failed_total"] / len(records)
        )
    if filtered:
        summary["requested_failure_rate_sampled_after_warmup"] = (
            summary["requested_failures_after_warmup"] / len(filtered)
        )
        summary["observed_workflow_failure_rate_after_warmup"] = (
            summary["workflow_failed_after_warmup"] / len(filtered)
        )

    latencies = sorted(record["latency_ms"] for record in filtered)
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
    return summary


def write_case_outputs(save_dir: Path, records: list[dict], summary: dict) -> None:
    save_dir.mkdir(parents=True, exist_ok=True)

    csv_path = save_dir / "client_requests.csv"
    fieldnames = [
        "request_id",
        "case_name",
        "can_abort",
        "worker_id",
        "sequence",
        "hotel_id",
        "flight_id",
        "requested_failure",
        "order_key",
        "started_at_ms",
        "completed_at_ms",
        "latency_ms",
        "failed",
        "order_committed",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        for record in sorted(records, key=lambda item: item["completed_at_ms"]):
            writer.writerow({name: record[name] for name in fieldnames})

    summary_path = save_dir / "summary.json"
    with summary_path.open("w", encoding="utf-8") as file:
        json.dump(summary, file, indent=2, sort_keys=True)


def build_suite_summary(case_summaries: list[dict]) -> dict:
    by_name = {summary["case_name"]: summary for summary in case_summaries}
    aborting = by_name.get("aborting")
    non_aborting = by_name.get("non-aborting")
    comparison: dict[str, float | int | None] = {}

    if aborting is not None and non_aborting is not None:
        comparison = {
            "aborting_requests_after_warmup": aborting.get("requests_after_warmup"),
            "non_aborting_requests_after_warmup": non_aborting.get("requests_after_warmup"),
            "aborting_requested_failures_after_warmup": aborting.get("requested_failures_after_warmup"),
            "non_aborting_requested_failures_after_warmup": non_aborting.get("requested_failures_after_warmup"),
            "aborting_workflow_failed_after_warmup": aborting.get("workflow_failed_after_warmup"),
            "non_aborting_workflow_failed_after_warmup": non_aborting.get("workflow_failed_after_warmup"),
            "aborting_orders_committed_after_warmup": aborting.get("orders_committed_after_warmup"),
            "non_aborting_orders_committed_after_warmup": non_aborting.get("orders_committed_after_warmup"),
        }

        aborting_latency = aborting.get("latency_ms")
        non_aborting_latency = non_aborting.get("latency_ms")
        if aborting_latency and non_aborting_latency:
            comparison["latency_mean_delta_ms"] = (
                non_aborting_latency["mean"] - aborting_latency["mean"]
            )
            if aborting_latency["mean"]:
                comparison["latency_mean_ratio"] = (
                    non_aborting_latency["mean"] / aborting_latency["mean"]
                )

        aborting_throughput = aborting.get("throughput_rps_after_warmup")
        non_aborting_throughput = non_aborting.get("throughput_rps_after_warmup")
        if aborting_throughput is not None and non_aborting_throughput is not None:
            comparison["throughput_delta_rps"] = non_aborting_throughput - aborting_throughput
            if aborting_throughput:
                comparison["throughput_ratio"] = non_aborting_throughput / aborting_throughput

    return {
        "run_id": case_summaries[0]["run_id"] if case_summaries else None,
        "configured_failure_rate": (
            case_summaries[0]["configured_failure_rate"] if case_summaries else None
        ),
        "case_summaries": case_summaries,
        "comparison": comparison,
    }


async def run_case(
    client: AsyncDeterSLClient,
    case: CaseConfig,
) -> dict:
    if not isinstance(case, CaseConfig):
        raise TypeError(
            f"run_case expected CaseConfig, got {type(case).__name__}: {case!r}"
        )
    await prepare_case_data(client)
    case_dict = asdict(case)

    worker_results = await run_pool(case_dict)
    request_meta = {
        request_id: meta
        for result in worker_results
        for request_id, meta in result.items()
    }

    workflow_metrics = await client.wait_for_requests(request_meta.keys())
    order_commits = await inspect_order_commits(
        client,
        [meta["order_key"] for meta in request_meta.values()],
    )

    records = []
    for request_id, meta in request_meta.items():
        metric = workflow_metrics[request_id]
        completed_at_ms = int(metric["completed_at"])
        records.append(
            {
                "request_id": request_id,
                "case_name": meta["case_name"],
                "can_abort": meta["can_abort"],
                "worker_id": meta["worker_id"],
                "sequence": meta["sequence"],
                "hotel_id": meta["hotel_id"],
                "flight_id": meta["flight_id"],
                "requested_failure": meta["requested_failure"],
                "order_key": meta["order_key"],
                "started_at_ms": meta["started_at_ms"],
                "completed_at_ms": completed_at_ms,
                "latency_ms": completed_at_ms - meta["started_at_ms"],
                "failed": bool(metric.get("failed", False)),
                "order_committed": order_commits.get(meta["order_key"], False),
            }
        )

    summary = build_summary(records, case)
    write_case_outputs(SAVE_DIR / case.name, records, summary)
    return summary


async def main() -> None:
    save_dir = SAVE_DIR
    save_dir.mkdir(parents=True, exist_ok=True)

    client = make_detersl_client()
    await client.connect(start_metrics=True)
    try:
        await register_application(client)

        case_summaries = []
        for case in ALL_CASES.values():
            summary = await run_case(client, case)
            case_summaries.append(summary)

        suite_summary = build_suite_summary(case_summaries)
        suite_path = save_dir / "comparison.json"
        with suite_path.open("w", encoding="utf-8") as file:
            json.dump(suite_summary, file, indent=2, sort_keys=True)
    finally:
        await client.close()


if __name__ == "__main__":
    asyncio.run(main())
