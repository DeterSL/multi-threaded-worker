import datetime as dt
import json
import multiprocessing
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
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple
from urllib.parse import urlsplit
from timeit import default_timer as timer
import pandas as pd
import calculate_metrics
from tqdm import tqdm
import asyncio
import nats
from nats.errors import NoRespondersError, TimeoutError
from multiprocessing import Pool
from concurrent.futures import ProcessPoolExecutor

multiprocessing.set_start_method("fork", force=True)
from multiprocessing import Pool
import random
import sys
import time
from timeit import default_timer as timer

import calculate_metrics
import pandas as pd

SAVE_DIR: str = sys.argv[1]
threads = int(sys.argv[2])
barrier = multiprocessing.Barrier(threads)
N_PARTITIONS = int(sys.argv[3])
messages_per_second = int(sys.argv[4])
sleeps_per_second = 10
sleep_time = 0.0085
seconds = int(sys.argv[5])
warmup_seconds = int(sys.argv[6])
DETERSL_SERVER: str = "http://0.0.0.0:8080"
DEFAULT_TIMEOUT : int = 10
REGISTRATION_TIMEOUT_S = float(os.environ.get("REGISTRATION_TIMEOUT_S", "20"))
REGISTRATION_MAX_RETRIES = max(1, int(os.environ.get("REGISTRATION_MAX_RETRIES", "5")))
REGISTRATION_RETRY_BACKOFF_S = float(os.environ.get("REGISTRATION_RETRY_BACKOFF_S", "1"))

NUM_GEO_ITEMS = 80
NUM_RATE_ITEMS = 80
NUM_REC_ITEMS = 80
NUM_USER_ITEMS = 501
NUM_HOTEL_ITEMS = 100
NUM_FLIGHT_ITEMS = 100
metrics_pull_batch = 1000
metrics_pull_timeout_s = 0.1
metrics_collect_timeout_s = 600

geo_keys = ["geo"+str(i) for i in range(1, NUM_GEO_ITEMS + 1)]
rate_keys = ["rate"+str(i) for i in range(1, NUM_RATE_ITEMS + 1)]
rec_keys = ["rec"+str(i) for i in range(1, NUM_REC_ITEMS + 1)]
user_keys = [f"Cornell_{i}" for i in range(NUM_USER_ITEMS)]
hotel_keys = ["hotel"+str(i) for i in range(NUM_HOTEL_ITEMS)]
flight_keys = ["flight"+str(i) for i in range(NUM_FLIGHT_ITEMS)]

metrics_by_id: dict[int, dict] = {}
metrics_events: dict[int, asyncio.Event] = {}
metrics_batch_waiters: list[tuple[set[int], asyncio.Event]] = []

def epoch_ms() -> int:
    return int(time.time() * 1000)

####################################################################################################################
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

async def register_function(nc, func_name : str):
    with open(f'function/{func_name}.json') as file:
        func_reg = json.load(file)

    await request_with_retries(
        nc,
        "detersl.worker.core.register_wasm",
        func_reg,
        f"function {func_name}",
    )

async def register_workflow(nc, payload : dict):
    await request_with_retries(
        nc,
        "detersl.worker.core.register_workflow",
        payload,
        f"workflow {payload.get('id', '<unknown>')}",
    )

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

async def init_data(js, data):
    req_ids = []
    futures = []
    for key, value in tqdm(data):
        payload = {
            "workflow_id" : "write_string",
            "input" : {
                "key" : str(key),
                "value" : value 
            }
        }
        futures.append(await js.publish_async("detersl.worker.invoke",
                                        json.dumps(payload).encode()))
    
    for fut in asyncio.as_completed(futures):
        ack = await fut
        req_ids.append(ack.seq)

    await wait_for_wfs(req_ids)


async def populate_with_init_data(js):
    # GEO: ctx.put({"Plat": lat, "Plon": lon})
    geo_items = [
        ("geo"+str(1), {"Plat": 37.7867, "Plon": 0}),
        ("geo"+str(2), {"Plat": 37.7854, "Plon": -122.4005}),
        ("geo"+str(3), {"Plat": 37.7867, "Plon": -122.4071}),
        ("geo"+str(4), {"Plat": 37.7936, "Plon": -122.3930}),
        ("geo"+str(5), {"Plat": 37.7831, "Plon": -122.4181}),
        ("geo"+str(6), {"Plat": 37.7863, "Plon": -122.4015}),
    ]
    for i in range(7, NUM_GEO_ITEMS + 1):
        lat = 37.7835 + i / 500.0 * 3
        lon = -122.41 + i / 500.0 * 4
        geo_items.append((geo_keys[i-1], {"Plat": lat, "Plon": lon}))
    await init_data(js, geo_items)

    # RATE: ctx.put({"code": code, "Indate": in_date, "Outdate": out_date, "RoomType": room_type})
    rate_prefix = "rate"
    rate_items = [
        (rate_prefix + str(1), {"code": "RACK", "Indate": "2015-04-09", "Outdate": "2015-04-10",
             "RoomType": {"BookableRate": 190.0, "Code": "KNG", "RoomDescription": "King sized bed",
                          "TotalRate": 109.0, "TotalRateInclusive": 123.17}}),
        (rate_prefix + str(2), {"code": "RACK", "Indate": "2015-04-09", "Outdate": "2015-04-10",
             "RoomType": {"BookableRate": 139.0, "Code": "QN", "RoomDescription": "Queen sized bed",
                          "TotalRate": 139.0, "TotalRateInclusive": 153.09}}),
        (rate_prefix + str(3), {"code": "RACK", "Indate": "2015-04-09", "Outdate": "2015-04-10",
             "RoomType": {"BookableRate": 109.0, "Code": "KNG", "RoomDescription": "King sized bed",
                          "TotalRate": 109.0, "TotalRateInclusive": 123.17}}),
    ]

    for i in range(4, NUM_RATE_ITEMS + 1):
        if i % 3 == 0:
            hotel_id = i
            end_date = "2015-04-" + ("17" if i % 2 == 0 else "24")
            rate = 109.0
            rate_inc = 123.17
            if i % 5 == 1:
                rate, rate_inc = 120.0, 140.0
            elif i % 5 == 2:
                rate, rate_inc = 124.0, 144.0
            elif i % 5 == 3:
                rate, rate_inc = 132.0, 158.0
            elif i % 5 == 4:
                rate, rate_inc = 232.0, 258.0

            rate_items.append((
                rate_keys[i-1],
                {"code": "RACK", "Indate": "2015-04-09", "Outdate": end_date,
                 "RoomType": {"BookableRate": rate, "Code": "KNG", "RoomDescription": "King sized bed",
                              "TotalRate": rate, "TotalRateInclusive": rate_inc}}
            ))
    await init_data(js, rate_items)

    rec_prefix = "rec"
    rec_items = [
        (rec_prefix + "1", {"HLat": 37.7867, "HLon": -122.4112, "HRate": 109.00, "HPrice": 150.00}),
        (rec_prefix + "2", {"HLat": 37.7854, "HLon": -122.4005, "HRate": 139.00, "HPrice": 120.00}),
        (rec_prefix + "3", {"HLat": 37.7834, "HLon": -122.4071, "HRate": 109.00, "HPrice": 190.00}),
        (rec_prefix + "4", {"HLat": 37.7936, "HLon": -122.3930, "HRate": 129.00, "HPrice": 160.00}),
        (rec_prefix + "5", {"HLat": 37.7831, "HLon": -122.4181, "HRate": 119.00, "HPrice": 140.00}),
        (rec_prefix + "6", {"HLat": 37.7863, "HLon": -122.4015, "HRate": 149.00, "HPrice": 200.00}),
    ]
    for i in range(7, NUM_REC_ITEMS + 1):
        lat = 37.7835 + i / 500.0 * 3
        lon = -122.41 + i / 500.0 * 4
        rate = 135.00
        rate_inc = 179.00
        if i % 3 == 0:
            if i % 5 == 0:
                rate, rate_inc = 109.00, 123.17
            elif i % 5 == 1:
                rate, rate_inc = 120.00, 140.00
            elif i % 5 == 2:
                rate, rate_inc = 124.00, 144.00
            elif i % 5 == 3:
                rate, rate_inc = 132.00, 158.00
            elif i % 5 == 4:
                rate, rate_inc = 232.00, 258.00

        rec_items.append((rec_keys[i-1], {"HLat": lat, "HLon": lon, "HRate": rate, "HPrice": rate_inc}))
    await init_data(js, rec_items)

    # USER: ctx.put(password)
    user_items = []
    for i in range(NUM_USER_ITEMS):
        username = f"Cornell_{i}"
        password = str(i) * 10
        user_items.append((username, password))
    await init_data(js, user_items)

    # HOTEL: ctx.put({"Cap": cap, "Customers": []})
    hotel_items = [(hotel_keys[i], {"Cap": 10, "Customers": []}) for i in range(NUM_HOTEL_ITEMS)]
    await init_data(js, hotel_items)

    # FLIGHT: ctx.put({"Cap": cap, "Customers": []})
    flight_items = [(flight_keys[i], {"Cap": 10, "Customers": []}) for i in range(NUM_FLIGHT_ITEMS)]
    await init_data(js, flight_items)

async def deathstar_init():
    nc = await nats.connect("nats://localhost:4222")
    js = nc.jetstream(publish_async_max_pending=50000)

    await start_metrics_sub(js)

    await register_function(nc, "write-string")
    await register_function(nc, "reserve-flight")
    await register_function(nc, "reserve-hotel")
    await register_function(nc, "nearby")
    await register_function(nc, "get-rates")
    await register_function(nc, "create-order")
    await register_function(nc, "get-recommendations")
    await register_function(nc, "get-user")

    await register_workflow(nc, {
        "id" : "write_string",
        "tasks" : [
            {
                "type" : "Task",
                "func_id" : "write-string",
                "resources" : {
                    "key" : "&key:w",
                    "value" : "$value"
                }
            }
        ]
    })

    await register_workflow(nc, {
        "id" : "search_nearby",
        "tasks" : [
            {
                "type" : "Task",
                "func_id" : "nearby",
                "resources" : {
                    "geo_keys" : "&geo_keys:r",
                    "lat" : "$lat",
                    "lon" : "$lon",
                    "in_date" : "$in_date",
                    "out_date" : "$out_date",
                    "hotel_ids" : "&_hotel_ids:w"
                }
            },
            {
                "type" : "Task",
                "func_id" : "get-rates",
                "resources" : {
                    "rate_keys" : "$rate_keys",
                    "in_date" : "$in_date",
                    "out_date" : "$out_date",
                    "hotel_ids" : "&_hotel_ids:r"
                }
            }

        ]
    })

    await register_workflow(nc, {
        "id" : "recommend",
        "tasks" : [
            {
                "type" : "Task",
                "func_id" : "get-recommendations",
                "resources" : {
                    "requirement" : "$requirement",
                    "lat" : "$lat",
                    "lon" : "$lon",
                    "rec_keys" : "&rec_keys:r"
                }
            }
        ]
    })

    await register_workflow(nc, {
        "id" : "user_login",
        "tasks" : [
            {
                "type" : "Task",
                "func_id" : "get-user",
                "resources" : {
                    "username" : "&username",
                    "password" : "$password"
                }
            }
        ]
    })

    await register_workflow(nc, {
        "id" : "reserve_all",
        "tasks" : [
            {
                "type" : "Task",
                "func_id" : "reserve-hotel",
                "resources" : {
                    "hotel_data" : "&hotel_id:w",
                }
            },
            {
                "type" : "Task",
                "func_id" : "reserve-flight",
                "resources" : {
                    "flight_data" : "&flight_id:w",
                }
            },
            {
                "type" : "Task",
                "func_id" : "create-order",
                "resources" : {
                    "hotel_id" : "$hotel_id",
                    "flight_id" : "$flight_id",
                    "user_id" : "$user_id",
                    "order_key" : "&key"
                }
            }
        ]
    })

    await populate_with_init_data(js)
    print("Data populated with init_data")
    
# -------------------------------------------------------------------------------------
# Workload generation (unchanged)
# -------------------------------------------------------------------------------------

def search_hotel(c):
    in_date = random.randint(9, 23)
    out_date = random.randint(in_date + 1, 24)
    if in_date < 10:
        in_date_str = f"2026-04-0{in_date}"
    else:
        in_date_str = f"2026-04-{in_date}"
    if out_date < 10:
        out_date_str = f"2026-04-0{out_date}"
    else:
        out_date_str = f"2026-04-{out_date}"
    lat = 38.0235 + (random.randint(0, 481) - 240.5) / 1000.0
    lon = -122.095 + (random.randint(0, 325) - 157.0) / 1000.0
    return ("search_nearby", 
            {"lat": lat, "lon": lon, "in_date": in_date_str, "out_date": out_date_str, "_hotel_ids": "hotel_ids", 
            "geo_keys": geo_keys, "rate_keys": rate_keys})

def recommend(c):
    coin = random.random()
    if coin < 0.33:
        req_param = "dis"
    elif coin < 0.66:
        req_param = "rate"
    else:
        req_param = "price"
    lat = 38.0235 + (random.randint(0, 481) - 240.5) / 1000.0
    lon = -122.095 + (random.randint(0, 325) - 157.0) / 1000.0
    return ("recommend", 
            {"requirement": req_param, "lat": lat, "lon": lon, "rec_keys": rec_keys})

def user_login():
    user_id = str(random.randint(0, 500))
    username = f"Cornell_{user_id}"
    password = user_id * 10
    return ("user_login", 
            {"username": username, "password": password})


def reserve_all(c):
    hotel_id = random.randint(0, NUM_HOTEL_ITEMS - 1)
    flight_id = random.randint(0, NUM_FLIGHT_ITEMS - 1)
    user_id = "user1"
    return ("reserve_all", 
            {"hotel_id": "hotel" + str(hotel_id), "flight_id": "flight" + str(flight_id), "user_id": user_id, "key": str(c)})

def deathstar_workload_generator():
    search_ratio = 0.6
    recommend_ratio = 0.39
    user_ratio = 0.005
    reserve_ratio = 0.005
    c = 0
    while True:
        coin = random.random()
        if coin < search_ratio:
            yield search_hotel(c)
        elif coin < search_ratio + recommend_ratio:
            yield recommend(c)
        elif coin < search_ratio + recommend_ratio + user_ratio:
            yield user_login()
        else:
            yield reserve_all(c)
        c += 1


# -------------------------------------------------------------------------------------
# Benchmark runner (still uses send_event for workload traffic; only init changes)
# -------------------------------------------------------------------------------------
async def ack_with_meta(ack_fut, meta):
    ack = await ack_fut
    return ack, meta

async def benchmark_runner(thread_num) -> dict[str, dict]:
    print(f"Benchmark starting from thread {thread_num}")

    nc = await nats.connect("nats://localhost:4222")
    js = nc.jetstream(publish_async_max_pending=50000)

    deathstar_generator = deathstar_workload_generator()
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
            wf_id, wf_input = next(deathstar_generator)
            resp = await js.publish_async("detersl.worker.invoke",
                    json.dumps({
                "workflow_id" : wf_id,
                "input" : wf_input
            }).encode())
            meta = {
                "op": f"{wf_id} : {wf_input}",
                "started_at_ms": epoch_ms(),
            }
            # timestamp_futures[resp] = {"op": f"{wf_id} {key1}->{key2}"}
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

    return req_ts

def run_worker(i):
    return asyncio.run(benchmark_runner(i))

async def run_pool(threads):
    loop = asyncio.get_running_loop()
    with ProcessPoolExecutor(max_workers=threads) as ex:
        tasks = [loop.run_in_executor(ex, run_worker, i) for i in range(threads)]
        return await asyncio.gather(*tasks)


async def main():
    await deathstar_init()

    results = await run_pool(threads)
    # with Pool(threads) as p:
    #     results = p.map(run_worker, range(threads))

    results = {k: v for d in results for k, v in d.items()}

    workflow_metrics = await wait_for_wfs(results.keys())
    for key, resp in tqdm(workflow_metrics.items()):
        completed_at_ms = resp["completed_at"]
        results[key]["completed_at_ms"] = completed_at_ms
        results[key]["latency_ms"] = completed_at_ms - results[key]["started_at_ms"]
    
    pd.DataFrame({"request_id": list(results.keys()),
                "latency_ms": [res["latency_ms"] for res in results.values()],
                "completed_at_ms": [res["completed_at_ms"] for res in results.values()],
                "op": [res["op"] for res in results.values()]
                }).sort_values("completed_at_ms").to_csv(f"{SAVE_DIR}/client_requests.csv", index=False)


if __name__ == "__main__":
    asyncio.run(main())
    
    calculate_metrics.main(
        SAVE_DIR,
        messages_per_second,
        warmup_seconds,
        threads
    )
