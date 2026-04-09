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
from nats.errors import NoRespondersError, TimeoutError
from typing import Iterable
from urllib.parse import urlsplit
from zipfian_generator import ZipfGenerator
from timeit import default_timer as timer
import pandas as pd
import calculate_metrics
from tqdm import tqdm
import asyncio
import nats
from nats.errors import TimeoutError
from multiprocessing import Pool
from concurrent.futures import ProcessPoolExecutor

threads = int(sys.argv[1])
N_ENTITIES = int(sys.argv[2])
STARTING_MONEY = 1_000_000
ZIPF_CONST = float(sys.argv[3])
messages_per_second = int(sys.argv[4])
sleeps_per_second = 10
sleep_time = 0.0085
seconds = int(sys.argv[5])
key_list: list[int] = list(range(N_ENTITIES))
DETERSL_SERVER: str = "http://0.0.0.0:8080"
DEFAULT_TIMEOUT : int = 10
SAVE_DIR: str = sys.argv[6]
warmup_seconds: int = int(sys.argv[7])
run_with_validation = sys.argv[8].lower() == "true"
poll_sleep = 0.001
batch_size = 5000
metrics_pull_batch = 1000
metrics_pull_timeout_s = 0.1
metrics_collect_timeout_s = 600

metrics_by_id: dict[int, dict] = {}
metrics_events: dict[int, asyncio.Event] = {}
metrics_batch_waiters: list[tuple[set[int], asyncio.Event]] = []

REGISTRATION_TIMEOUT_S = float(os.environ.get("REGISTRATION_TIMEOUT_S", "20"))
REGISTRATION_MAX_RETRIES = max(1, int(os.environ.get("REGISTRATION_MAX_RETRIES", "5")))
REGISTRATION_RETRY_BACKOFF_S = float(os.environ.get("REGISTRATION_RETRY_BACKOFF_S", "1"))

def epoch_ms() -> int:
    return time.time_ns() // 1_000_000
        
async def start_metrics_sub(js):
    sub = await js.pull_subscribe("detersl.worker.metrics", durable="detersl-metrics")

    async def pull_loop():
        while True:
            try:
                msgs = await sub.fetch(metrics_pull_batch, timeout=metrics_pull_timeout_s)
            except TimeoutError:
                continue
            for msg in msgs:
                try:
                    data = json.loads(msg.data.decode())
                    req_id = data["request_id"]
                    metrics_by_id[req_id] = data
                    evt = metrics_events.get(req_id)
                    if evt:
                        evt.set()
                    for pending, batch_evt in metrics_batch_waiters:
                        pending.discard(req_id)
                        if not pending:
                            batch_evt.set()
                finally:
                    await msg.ack()
        
    asyncio.create_task(pull_loop())
    

def utc_now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()

async def request_with_retries(nc, subject: str, payload: dict, label: str):
    encoded = json.dumps(payload).encode("utf-8")

    for attempt in range(1, REGISTRATION_MAX_RETRIES + 1):
        try:
            response = await nc.request(subject, encoded, timeout=REGISTRATION_TIMEOUT_S)
            raw_response = response.data.decode(errors="replace")
            try:
                body = json.loads(raw_response)
            except json.JSONDecodeError:
                body = None

            if isinstance(body, dict) and body.get("status") == "error":
                error = str(body.get("error", raw_response))
                if "already registered" in error:
                    print(f"{label} already registered: {error}")
                    return response
                raise RuntimeError(f"{label} registration failed: {error}")

            print(f"{label} registered: {raw_response or '<empty response>'}")
            return response
        except (NoRespondersError, TimeoutError) as exc:
            failure = "had no responders" if isinstance(exc, NoRespondersError) else "timed out"
            if attempt == REGISTRATION_MAX_RETRIES:
                raise RuntimeError(
                    f"{label} {failure} after {REGISTRATION_MAX_RETRIES} attempts "
                    f"with a {REGISTRATION_TIMEOUT_S:g}s timeout"
                ) from exc

            delay = REGISTRATION_RETRY_BACKOFF_S * attempt
            print(
                f"{label} registration {failure} "
                f"(attempt {attempt}/{REGISTRATION_MAX_RETRIES}); retrying in {delay:g}s"
            )
            await asyncio.sleep(delay)

async def message_handler(msg):
    subject = msg.subject
    reply = msg.reply
    data = msg.data.decode()
    print("Received a message on '{subject} {reply}': {data}".format(
        subject=subject, reply=reply, data=data))

async def register_workflow(nc, payload : dict):
    await request_with_retries(
        nc,
        "detersl.worker.core.register_workflow",
        payload,
        f"workflow {payload.get('id', '<unknown>')}",
    )

async def register_function(nc, func_name : str):
    with open(f'function/{func_name}.json') as file:
        func_reg = json.load(file)

    await request_with_retries(
        nc,
        "detersl.worker.core.register_wasm",
        func_reg,
        f"function {func_name}",
    )

    # print(response)

# def wait_for_wf(invocation_id : str):
#     while True:
#         res = http_call_json(
#             base_url= DETERSL_SERVER,
#             path=f"/workflow/status/{invocation_id}",
#             method="GET",
#             timeout_s= DEFAULT_TIMEOUT,
#             )
#         body = res.json_data
#         if body["done"]:
#             return body
#         time.sleep(poll_sleep)

async def wait_for_wf(req_id: str, timeout=30):
    if req_id in metrics_by_id:
        return metrics_by_id[req_id]

    evt = metrics_events.setdefault(req_id, asyncio.Event())
    try:
        await asyncio.wait_for(evt.wait(), timeout=timeout)
    except asyncio.TimeoutError:
        print(f"Timeout while waiting for workflow completion for request_id {req_id}")
        return None
    return metrics_by_id.get(req_id)

async def invoke_workflow_async(js, payload):
    # returns a Future; no per‑message ack wait here
    return await js.publish_async(
        "detersl.worker.invoke",
        json.dumps(payload).encode()
    )

async def wait_for_wfs(req_ids: Iterable[int], timeout=metrics_collect_timeout_s):
    req_ids = list(req_ids)
    if not req_ids:
        return {}

    pending = {req_id for req_id in req_ids if req_id not in metrics_by_id}
    if pending:
        evt = asyncio.Event()
        waiter = (pending, evt)
        metrics_batch_waiters.append(waiter)
        try:
            await asyncio.wait_for(evt.wait(), timeout=timeout)
        except asyncio.TimeoutError as exc:
            missing = list(pending)
            sample = ", ".join(str(req_id) for req_id in missing[:10])
            raise RuntimeError(
                f"Timed out after {timeout}s waiting for "
                f"{len(missing)}/{len(req_ids)} workflow completions"
                + (f"; first missing request_ids: {sample}" if sample else "")
            ) from exc
        finally:
            for idx, current in enumerate(metrics_batch_waiters):
                if current is waiter:
                    del metrics_batch_waiters[idx]
                    break

    return {req_id: metrics_by_id[req_id] for req_id in req_ids}

async def ycsb_init(keys: list[int]):
    nc = await nats.connect("nats://localhost:4222")
    js = nc.jetstream(publish_async_max_pending=50000)

    await start_metrics_sub(js)

    await register_function(nc, "write")
    await register_function(nc, "transfer")

    await register_workflow(nc, {
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

    await register_workflow(nc, {
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
    futures = []
    for key in tqdm(keys):
        payload = {
            "workflow_id" : "yscb_write",
            "input" : {
                "key" : str(key),
                "value" : STARTING_MONEY 
            }
        }
        futures.append(await js.publish_async("detersl.worker.invoke",
                                           json.dumps(payload).encode()))
    
    for fut in asyncio.as_completed(futures):
        ack = await fut
        req_ids.append(ack.seq)

    await wait_for_wfs(req_ids)
        
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

    nc = await nats.connect("nats://localhost:4222")
    js = nc.jetstream(publish_async_max_pending=50000)

    ycsb_generator = transactional_ycsb_generator(key_list, N_ENTITIES, zipf_const=ZIPF_CONST)
    timestamp_futures: dict[str, dict] = {}
    req_ts: dict[str, dict] = {}
    errors = 0
    batch_payloads: list[dict] = []
    batch_ops: list[str] = []

    tasks = []

    for second in range(seconds):
        sec_start = timer()
        step = max(1, messages_per_second // sleeps_per_second)
        for i in tqdm(range(messages_per_second)):
            if i % step == 0:
                await asyncio.sleep(sleep_time)
            wf_id, key1, key2 = next(ycsb_generator)
            resp = await js.publish_async("detersl.worker.invoke",
                                           json.dumps({
                "workflow_id" : wf_id,
                "input" : {"from" : key1, "to" : key2}
            }).encode())
            meta = {
                "op": f"{wf_id} {key1}->{key2}",
                "started_at_ms": epoch_ms(),
            }
            #timestamp_futures[resp] = {"op": f"{wf_id} {key1}->{key2}"}
            tasks.append(asyncio.create_task(ack_with_meta(resp, meta)))

        sec_end = timer()
        lps = sec_end - sec_start
        if lps < 1:
            await asyncio.sleep(1 - lps)
        sec_end2 = timer()
        print(f"{second} | Latency per second: {sec_end2 - sec_start}")

    # for fut in asyncio.as_completed(timestamp_futures):
    #     ack = await fut
    #     req_ts[ack.seq] = timestamp_futures[fut]

    for t in asyncio.as_completed(tasks):
        ack, meta = await t
        req_ts[ack.seq] = meta

    # def flush_batch() -> None:
    #     nonlocal errors, batch_payloads, batch_ops
    #     if not batch_payloads:
    #         return
    #     resp = invoke_workflow_batch(batch_payloads)
    #     if (not resp.ok) or (not resp.json_data) or ("request_ids" not in resp.json_data):
    #         errors += 1
    #         batch_payloads = []
    #         batch_ops = []
    #         return
    #     request_ids = resp.json_data["request_ids"]
    #     if len(request_ids) != len(batch_ops):
    #         errors += 1
    #         batch_payloads = []
    #         batch_ops = []
    #         return
    #     print(f"batch processed in {resp.latency_ms} ms")
    #     for request_id, op in zip(request_ids, batch_ops):
    #         timestamp_futures[request_id] = {"op": op}
    #     batch_payloads = []
    #     batch_ops = []

    # total_requests = messages_per_second * seconds
    # total_batches = max(1, (total_requests + batch_size - 1) // batch_size)

    # start = timer()
    # for batch_index in range(total_batches):
    #     batch_target = start + (batch_index / (total_batches / seconds))
    #     while True:
    #         now = timer()
    #         delay = batch_target - now
    #         if delay <= 0:
    #             break
    #         time.sleep(min(delay, 0.001))

    #     remaining = total_requests - (batch_index * batch_size)
    #     current_batch = min(batch_size, remaining)
    #     for _ in range(current_batch):
    #         wf_id, key1, key2 = next(ycsb_generator)
    #         batch_payloads.append({
    #             "workflow_id" : wf_id,
    #             "input" : {"from" : key1, "to" : key2}
    #         })
    #         batch_ops.append(f"{wf_id} {key1}->{key2}")
    #     flush_batch()

    #     now = timer()
    #     if now - start >= (batch_index + 1):
    #         print(f"{batch_index + 1} | Batches sent: {batch_index + 1}")

    # end = timer()
    # print(f"Average batch interval: {(end - start) / total_batches}")
    # print(f"Average latency per second: {(end - start) / seconds}")
    # if errors:
    #     print(f"Batch errors: {errors}")
    return req_ts


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

    await ycsb_init(key_list)

    #asyncio.create_task(

    # await js.add_stream(name="DETERSL", subjects=["detersl.worker.invoke", "detersl.worker.invoke_batch"])

    # sub = await nc.subscribe("foo", cb=message_handler)

    # results = await benchmark_runner(js)
    results = await run_pool(threads)
    # with Pool(threads) as p:
    #     results = p.map(run_worker, range(threads))

    results = {k: v for d in results for k, v in d.items()}

    workflow_metrics = await wait_for_wfs(results.keys())
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
