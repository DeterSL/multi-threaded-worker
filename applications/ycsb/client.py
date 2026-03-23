import json
import argparse
import copy
import csv
import datetime as dt
import json
import random
import re
import statistics
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple
from urllib import error, request
import time
import sys
from zipfian_generator import ZipfGenerator
from timeit import default_timer as timer
import pandas as pd
import calculate_metrics
from tqdm import tqdm

threads = int(sys.argv[1])
N_ENTITIES = int(sys.argv[2])
STARTING_MONEY = 1_000_000
ZIPF_CONST = float(sys.argv[3])
messages_per_second = int(sys.argv[4])
sleeps_per_second = 10
sleep_time = 0.005
seconds = int(sys.argv[5])
key_list: list[int] = list(range(N_ENTITIES))
DETERSL_HOST: str = "http://0.0.0.0:6666"
DEFAULT_TIMEOUT : int = 10
SAVE_DIR: str = sys.argv[6]
warmup_seconds: int = int(sys.argv[7])
run_with_validation = sys.argv[8].lower() == "true"
poll_sleep = 0.001
batch_size = 50000

@dataclass
class HttpResult:
    status: int
    ok: bool
    latency_ms: float
    text: str
    json_data: Optional[Any]
    error: str

    def __str__(self):
        return json.dumps({
            'status' : self.status,
            'ok' : self.ok,
            'latency_ms' : self.latency_ms,
            'text' : self.text,
            'json_data' : self.json_data,
            'error' : self.error
        })

def utc_now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()

def http_call_json(
    base_url: str,
    path: str,
    method: str,
    timeout_s: float,
    payload: Optional[Dict[str, Any]] = None,
) -> HttpResult:
    url = f"{base_url.rstrip('/')}{path}"
    body: Optional[bytes] = None
    headers: Dict[str, str] = {}
    if payload is not None:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"

    req = request.Request(url=url, method=method, data=body, headers=headers)
    t0 = time.perf_counter()
    try:
        with request.urlopen(req, timeout=timeout_s) as resp:
            raw = resp.read()
            status = int(resp.status)
    except error.HTTPError as exc:
        raw = exc.read() if exc.fp is not None else b""
        latency_ms = (time.perf_counter() - t0) * 1000.0
        text = raw.decode("utf-8", errors="replace")
        parsed = None
        try:
            parsed = json.loads(text) if text.strip() else None
        except json.JSONDecodeError:
            pass
        return HttpResult(
            status=exc.code,
            ok=False,
            latency_ms=latency_ms,
            text=text,
            json_data=parsed,
            error=f"HTTP {exc.code}",
        )
    except Exception as exc:
        latency_ms = (time.perf_counter() - t0) * 1000.0
        return HttpResult(
            status=0,
            ok=False,
            latency_ms=latency_ms,
            text="",
            json_data=None,
            error=str(exc),
        )

    latency_ms = (time.perf_counter() - t0) * 1000.0
    text = raw.decode("utf-8", errors="replace")
    parsed = None
    try:
        parsed = json.loads(text) if text.strip() else None
    except json.JSONDecodeError:
        pass
    return HttpResult(
        status=status,
        ok=(200 <= status < 300),
        latency_ms=latency_ms,
        text=text,
        json_data=parsed,
        error="",
    )

def register_workflow(payload : dict):
    reg_wf = http_call_json(
            base_url= DETERSL_HOST,
            path="/workflow/register",
            method="POST",
            timeout_s= DEFAULT_TIMEOUT,
            payload=payload,
            )
    print(reg_wf)

def register_function(func_name : str):
    with open(f'function/{func_name}.json') as file:
        func_reg = json.load(file)

    reg_func = http_call_json(
            base_url= DETERSL_HOST,
            path="/wasm",
            method="POST",
            timeout_s= DEFAULT_TIMEOUT,
            payload=func_reg,
            )
    print(reg_func)

def wait_for_wf(invocation_id : str):
    while True:
        res = http_call_json(
            base_url= DETERSL_HOST,
            path=f"/workflow/status/{invocation_id}",
            method="GET",
            timeout_s= DEFAULT_TIMEOUT,
            )
        body = res.json_data
        if body["done"]:
            return body
        time.sleep(poll_sleep)

def invoke_workflow(payload):
    resp_body = http_call_json(
                base_url= DETERSL_HOST,
                path="/workflow/invoke",
                method="POST",
                timeout_s= DEFAULT_TIMEOUT,
                payload=payload,
                )
    return resp_body.json_data

def invoke_workflow_batch(payloads: list[dict]) -> HttpResult:
    return http_call_json(
                base_url= DETERSL_HOST,
                path="/workflow/invoke_batch",
                method="POST",
                timeout_s= DEFAULT_TIMEOUT,
                payload={"invocations": payloads},
                )

def ycsb_init(keys: list[int]):
    register_function("write")
    register_function("transfer")

    register_workflow({
        "id" : "yscb_write",
        "tasks" : [
            {
                "type" : "Task",
                "func_id" : "write",
                "resources" : {
                    "key" : "&key:w",
                    "value" : "$value"
                }
            }
        ]
    })

    register_workflow({
        "id" : "yscb_transfer",
        "tasks" : [
            {
                "type" : "Task",
                "func_id" : "transfer",
                "resources" : {
                    "from" : "&from:w",
                    "to" : "&to:w"
                }
            }
        ]
    })
    req_ids = []
    
    for key in tqdm(keys):
        payload = {
            "workflow_id" : "yscb_write",
            "input" : {
                "key" : str(key),
                "value" : STARTING_MONEY 
            }
        }
        resp = invoke_workflow(payload)
        req_ids.append(resp["request_id"])

    for key in tqdm(req_ids):
        wait_for_wf(key)
    
def transactional_ycsb_generator(keys,
                                 n: int,
                                 zipf_const: float):
    zipf_gen = ZipfGenerator(items=n, zipf_const=zipf_const)
    uniform_gen = ZipfGenerator(items=n, zipf_const=0.0)
    while True:
        key = keys[next(uniform_gen)]
        key2 = keys[next(zipf_gen)]
        while key2 == key:
            key2 = keys[next(zipf_gen)]
        yield "yscb_transfer", str(key), str(key2)

def benchmark_runner() -> dict[str, dict]:
    print("Benchmark starting")
    ycsb_generator = transactional_ycsb_generator(key_list, N_ENTITIES, zipf_const=ZIPF_CONST)
    timestamp_futures: dict[str, dict] = {}
    errors = 0
    batch_payloads: list[dict] = []
    batch_ops: list[str] = []

    def flush_batch() -> None:
        nonlocal errors, batch_payloads, batch_ops
        if not batch_payloads:
            return
        resp = invoke_workflow_batch(batch_payloads)
        if (not resp.ok) or (not resp.json_data) or ("request_ids" not in resp.json_data):
            errors += 1
            batch_payloads = []
            batch_ops = []
            return
        request_ids = resp.json_data["request_ids"]
        if len(request_ids) != len(batch_ops):
            errors += 1
            batch_payloads = []
            batch_ops = []
            return
        for request_id, op in zip(request_ids, batch_ops):
            timestamp_futures[request_id] = {"op": op}
        batch_payloads = []
        batch_ops = []

    total_requests = messages_per_second * seconds
    total_batches = max(1, (total_requests + batch_size - 1) // batch_size)

    start = timer()
    for batch_index in range(total_batches):
        batch_target = start + (batch_index / (total_batches / seconds))
        while True:
            now = timer()
            delay = batch_target - now
            if delay <= 0:
                break
            time.sleep(min(delay, 0.001))

        remaining = total_requests - (batch_index * batch_size)
        current_batch = min(batch_size, remaining)
        for _ in range(current_batch):
            wf_id, key1, key2 = next(ycsb_generator)
            batch_payloads.append({
                "workflow_id" : wf_id,
                "input" : {"from" : key1, "to" : key2}
            })
            batch_ops.append(f"{wf_id} {key1}->{key2}")
        flush_batch()

        now = timer()
        if now - start >= (batch_index + 1):
            print(f"{batch_index + 1} | Batches sent: {batch_index + 1}")

    end = timer()
    print(f"Average batch interval: {(end - start) / total_batches}")
    print(f"Average latency per second: {(end - start) / seconds}")
    if errors:
        print(f"Batch errors: {errors}")

    for key in tqdm(timestamp_futures.keys()):
        resp = wait_for_wf(key)
        timestamp_futures[key]["latency_ms"] = resp["latency_ms"]
        timestamp_futures[key]["completed_at_ms"] = resp["completed_at"]

    return timestamp_futures

def get_value(key : str):
    resp = http_call_json(
            base_url= DETERSL_HOST,
            path=f"/resource/{key}",
            method="POST",
            timeout_s= DEFAULT_TIMEOUT,
            )
    return resp.text
        
def main():
    print("Generate and push workload to DeterSL")

    ycsb_init(key_list)

    results = benchmark_runner()

    if run_with_validation:
        records = []
        for key in key_list:
            req = request.Request(url=f"{DETERSL_HOST}/resource/{str(key)}", method="GET")
            with request.urlopen(req, timeout=DEFAULT_TIMEOUT) as resp:
                raw = resp.read()
                status = int(resp.status)
            if(status == 200):
                records.append((f"[{key}, {int.from_bytes(raw)}]", int(time.time() * 1000)))

        pd.DataFrame.from_records(records,
                                  columns=["KeyVal", "timestamp"]).to_csv(f"{SAVE_DIR}/output.csv",
                                                                                          index=False)

    pd.DataFrame({"request_id": list(results.keys()),
                "latency_ms": [res["latency_ms"] for res in results.values()],
                "completed_at_ms": [res["completed_at_ms"] for res in results.values()],
                "op": [res["op"] for res in results.values()]
                }).sort_values("completed_at_ms").to_csv(f"{SAVE_DIR}/client_requests.csv", index=False)

    print("Workload completed")

if __name__ == "__main__":
    main()
    calculate_metrics.main(
        N_ENTITIES,
        messages_per_second,
        ZIPF_CONST,
        threads,
        warmup_seconds,
        SAVE_DIR,
        run_with_validation
    )