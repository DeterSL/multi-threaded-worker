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

threads = int(sys.argv[1])
N_ENTITIES = int(sys.argv[2])
STARTING_MONEY = 1_000_000
ZIPF_CONST = float(sys.argv[3])
messages_per_second = int(sys.argv[4])
sleeps_per_second = 100
sleep_time = 0.0085
seconds = int(sys.argv[5])
key_list: list[int] = list(range(N_ENTITIES))
DETERSL_HOST: str = "http://0.0.0.0:6666"
DEFAULT_TIMEOUT : int = 10
SAVE_DIR: str = sys.argv[6]
warmup_seconds: int = int(sys.argv[7])
run_with_validation = sys.argv[8].lower() == "true"

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
    completed = False
    while completed is not True:
        res = http_call_json(
            base_url= DETERSL_HOST,
            path=f"/workflow/status/{invocation_id}",
            method="GET",
            timeout_s= DEFAULT_TIMEOUT,
            )
        body = res.json_data
        completed = body["done"]
        time.sleep(sleep_time)
    
    return body

def invoke_workflow(payload):
    resp_body = http_call_json(
                base_url= DETERSL_HOST,
                path="/workflow/invoke",
                method="POST",
                timeout_s= DEFAULT_TIMEOUT,
                payload=payload,
                )
    return resp_body.json_data

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

    for key in keys:
        payload = {
            "workflow_id" : "yscb_write",
            "input" : {
                "key" : str(key),
                "value" : STARTING_MONEY 
            }
        }
        resp = invoke_workflow(payload)
        invocation_id = resp["request_id"]
        wait_for_wf(invocation_id)            
    
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
        yield {
            "workflow_id" : "yscb_transfer",
            "input" : {
                "from" : str(key),
                "to" : str(key2)
            }
        }

def benchmark_runner() -> dict[bytes, dict]:
    print("Benchmark starting")
    ycsb_generator = transactional_ycsb_generator(key_list, N_ENTITIES, zipf_const=ZIPF_CONST)
    timestamp_futures: dict[bytes, dict] = {}

    start = timer()
    for second in range(seconds):
        sec_start = timer()
        step = max(1, messages_per_second // sleeps_per_second)
        for i in range(messages_per_second):
            if i % step == 0:
                time.sleep(sleep_time)
            payload = next(ycsb_generator)
            future = invoke_workflow(payload)
            timestamp_futures[future["request_id"]] = {"op": f"{payload["workflow_id"]} {payload["input"]["from"]}->{payload["input"]["to"]}"}
        sec_end = timer()
        lps = sec_end - sec_start
        if lps < 1:
            time.sleep(1 - lps)
        sec_end2 = timer()
        print(f"{second} | Latency per second: {sec_end2 - sec_start}")
    end = timer()
    print(f"Average latency per second: {(end - start) / seconds}")

    for key in timestamp_futures.keys():
        resp = wait_for_wf(key)
        timestamp_futures[key]["latency_ms"] = resp["latency_ms"]
        timestamp_futures[key]["completed_at_ms"] = resp["completed_at"]

    return timestamp_futures
        
def main():
    print("Generate and push workload to DeterSL")

    ycsb_init(key_list)

    results = benchmark_runner()

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
