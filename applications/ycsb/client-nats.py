import argparse
import copy
import csv
import datetime as dt
import http.client
import json
import os
import random
import re
import statistics
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed, ProcessPoolExecutor
from dataclasses import dataclass, asdict
from pathlib import Path
from urllib.parse import urlsplit
from timeit import default_timer as timer
import asyncio
from multiprocessing import Pool
from concurrent.futures import ProcessPoolExecutor
import pandas as pd
import calculate_metrics
from tqdm import tqdm
from zipfian_generator import ZipfGenerator

APP_DIR = Path(__file__).resolve().parent
FUNCTION_DIR = APP_DIR / "function"
REPO_ROOT = APP_DIR.parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from detersl_client import AsyncDeterSLClient

threads = int(sys.argv[1])
N_ENTITIES = int(sys.argv[2])
STARTING_MONEY = 1_000_000
ZIPF_CONST = float(sys.argv[3])
messages_per_second = int(sys.argv[4])
sleeps_per_second = 10
sleep_time = 0.0085
seconds = int(sys.argv[5])
key_list: list[int] = list(range(N_ENTITIES))
SAVE_DIR: str = sys.argv[6]
warmup_seconds: int = int(sys.argv[7])
run_with_validation = sys.argv[8].lower() == "true"
metrics_pull_batch = 1000
metrics_pull_timeout_s = 0.1
metrics_collect_timeout_s = 600

def epoch_ms() -> int:
    return time.time_ns() // 1_000_000
    
def make_detersl_client() -> AsyncDeterSLClient:
    return AsyncDeterSLClient(
        publish_async_max_pending=50000,
        metrics_pull_batch=metrics_pull_batch,
        metrics_pull_timeout_s=metrics_pull_timeout_s,
        metrics_wait_timeout_s=metrics_collect_timeout_s,
    )


async def ycsb_init(client: AsyncDeterSLClient, keys: list[int]):
    await client.register_functions(["write", "transfer"], function_dir=FUNCTION_DIR)

    await client.register_workflow({
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

    await client.register_workflow({
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

    req_ids = await client.publish_invocations(
        {
            "workflow_id" : "yscb_write",
            "input" : {
                "key" : str(key),
                "value" : STARTING_MONEY 
            }
        }
        for key in tqdm(keys)
    )

    await client.wait_for_requests(req_ids)
        
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

async def ack_with_meta(ack_fut, meta):
    ack = await ack_fut
    return ack, meta

async def benchmark_runner(thread_num) -> dict[str, dict]:
    print(f"Benchmark starting from thread {thread_num}")

    client = make_detersl_client()
    await client.connect()
    try:
        ycsb_generator = transactional_ycsb_generator(key_list, N_ENTITIES, zipf_const=ZIPF_CONST)
        req_ts: dict[str, dict] = {}
        tasks = []

        for second in range(seconds):
            sec_start = timer()
            step = max(1, messages_per_second // sleeps_per_second)
            for i in tqdm(range(messages_per_second)):
                if i % step == 0:
                    await asyncio.sleep(sleep_time)
                wf_id, key1, key2 = next(ycsb_generator)
                resp = await client.invoke_workflow(
                    wf_id,
                    {"from": key1, "to": key2},
                )
                meta = {
                    "op": f"{wf_id} {key1}->{key2}",
                    "started_at_ms": epoch_ms(),
                }
                tasks.append(asyncio.create_task(ack_with_meta(resp, meta)))

            sec_end = timer()
            lps = sec_end - sec_start
            if lps < 1:
                await asyncio.sleep(1 - lps)
            sec_end2 = timer()
            print(f"{second} | Latency per second: {sec_end2 - sec_start}")

        for t in asyncio.as_completed(tasks):
            ack, meta = await t
            req_ts[ack.seq] = meta

        return req_ts
    finally:
        await client.close()


# def get_value(key : str):
#     resp = http_call_json(
#             base_url= DETERSL_SERVER,
#             path=f"/resource/{key}",
#             method="POST",
#             timeout_s= DEFAULT_TIMEOUT,
#             )
#     return resp.text

async def message_handler(msg):
    subject = msg.subject
    reply = msg.reply
    data = msg.data.decode()
    print("Received a message on '{subject} {reply}': {data}".format(
        subject=subject, reply=reply, data=data))

def run_worker(i):
    return asyncio.run(benchmark_runner(i))

async def run_pool(threads):
    loop = asyncio.get_running_loop()
    with ProcessPoolExecutor(max_workers=threads) as ex:
        tasks = [loop.run_in_executor(ex, run_worker, i) for i in range(threads)]
        return await asyncio.gather(*tasks)
        
async def main():
    print("Generate and push workload to DeterSL")

    client = make_detersl_client()
    await client.connect(start_metrics=True)
    try:
        await ycsb_init(client, key_list)

        results = await run_pool(threads)
        results = {k: v for d in results for k, v in d.items()}

        workflow_metrics = await client.wait_for_requests(results.keys())
        for key, resp in tqdm(workflow_metrics.items()):
            completed_at_ms = resp["completed_at"]
            results[key]["completed_at_ms"] = completed_at_ms
            results[key]["latency_ms"] = completed_at_ms - results[key]["started_at_ms"]

        # if run_with_validation:
        #     records = []
        #     for key in key_list:
        #         status, raw = _get_client(DETERSL_SERVER).request(
        #             "GET",
        #             f"/resource/{str(key)}",
        #             None,
        #             {},
        #             DEFAULT_TIMEOUT,
        #         )
        #         if(status == 200):
        #             records.append((f"[{key}, {int.from_bytes(raw)}]", int(time.time() * 1000)))

        #     pd.DataFrame.from_records(records,
        #                               columns=["KeyVal", "timestamp"]).to_csv(f"{SAVE_DIR}/output.csv",
        #                                                                                       index=False)

        pd.DataFrame({"request_id": list(results.keys()),
                    "latency_ms": [res["latency_ms"] for res in results.values()],
                    "completed_at_ms": [res["completed_at_ms"] for res in results.values()],
                    "op": [res["op"] for res in results.values()]
                    }).sort_values("completed_at_ms").to_csv(f"{SAVE_DIR}/client_requests.csv", index=False)

        print("Workload completed")
    finally:
        await client.close()

if __name__ == "__main__":
    asyncio.run(main())
    calculate_metrics.main(
        N_ENTITIES,
        messages_per_second,
        ZIPF_CONST,
        threads,
        warmup_seconds,
        SAVE_DIR,
        run_with_validation
    )
