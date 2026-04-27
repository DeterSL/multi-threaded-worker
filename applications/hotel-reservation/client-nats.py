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
import asyncio
from multiprocessing import Pool
from concurrent.futures import ProcessPoolExecutor
import pandas as pd
import calculate_metrics
from tqdm import tqdm

APP_DIR = Path(__file__).resolve().parent
FUNCTION_DIR = APP_DIR / "function"
REPO_ROOT = APP_DIR.parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from detersl_client import AsyncDeterSLClient

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
NUM_GEO_ITEMS = 80
NUM_RATE_ITEMS = 80
NUM_REC_ITEMS = 80
NUM_USER_ITEMS = 501
NUM_HOTEL_ITEMS = 100
NUM_FLIGHT_ITEMS = 100
metrics_pull_batch = 1000
metrics_pull_timeout_s = 0.1
metrics_collect_timeout_s = 120

geo_keys = ["geo"+str(i) for i in range(1, NUM_GEO_ITEMS + 1)]
rate_keys = ["rate"+str(i) for i in range(1, NUM_RATE_ITEMS + 1)]
rec_keys = ["rec"+str(i) for i in range(1, NUM_REC_ITEMS + 1)]
user_keys = [f"Cornell_{i}" for i in range(NUM_USER_ITEMS)]
hotel_keys = ["hotel"+str(i) for i in range(NUM_HOTEL_ITEMS)]
flight_keys = ["flight"+str(i) for i in range(NUM_FLIGHT_ITEMS)]

def epoch_ms() -> int:
    return int(time.time() * 1000)

####################################################################################################################
def make_detersl_client() -> AsyncDeterSLClient:
    return AsyncDeterSLClient(
        publish_async_max_pending=50000,
        metrics_pull_batch=metrics_pull_batch,
        metrics_pull_timeout_s=metrics_pull_timeout_s,
        metrics_wait_timeout_s=metrics_collect_timeout_s,
    )


async def init_data(client: AsyncDeterSLClient, data):
    req_ids = await client.publish_invocations(
        {
            "workflow_id" : "write_string",
            "input" : {
                "key" : str(key),
                "value" : value
            }
        }
        for key, value in tqdm(data)
    )

    await client.wait_for_requests({request_id: index for index, request_id in enumerate(req_ids)})


async def populate_with_init_data(client: AsyncDeterSLClient):
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
    await init_data(client, geo_items)

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
    await init_data(client, rate_items)

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
    await init_data(client, rec_items)

    # USER: ctx.put(password)
    user_items = []
    for i in range(NUM_USER_ITEMS):
        username = f"Cornell_{i}"
        password = str(i) * 10
        user_items.append((username, password))
    await init_data(client, user_items)

    # HOTEL: ctx.put({"Cap": cap, "Customers": []})
    hotel_items = [(hotel_keys[i], {"Cap": 10, "Customers": []}) for i in range(NUM_HOTEL_ITEMS)]
    await init_data(client, hotel_items)

    # FLIGHT: ctx.put({"Cap": cap, "Customers": []})
    flight_items = [(flight_keys[i], {"Cap": 10, "Customers": []}) for i in range(NUM_FLIGHT_ITEMS)]
    await init_data(client, flight_items)

async def deathstar_init(client: AsyncDeterSLClient):
    await client.register_functions(
        [
            "write-string",
            "reserve-flight",
            "reserve-hotel",
            "nearby",
            "get-rates",
            "create-order",
            "get-recommendations",
            "get-user",
        ],
        function_dir=FUNCTION_DIR,
    )

    await client.register_workflow({
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

    await client.register_workflow({
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
                    "hotel_ids" : "&_hotel_ids:w"
                }
            }

        ]
    })

    await client.register_workflow({
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

    await client.register_workflow({
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

    await client.register_workflow({
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

    await populate_with_init_data(client)
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

    client = make_detersl_client()
    await client.connect()
    try:
        deathstar_generator = deathstar_workload_generator()
        req_ts: dict[str, dict] = {}
        tasks = []

        for second in range(seconds):
            sec_start = timer()
            step = max(1, messages_per_second // sleeps_per_second)
            for i in tqdm(range(messages_per_second)):
                if i % step == 0:
                    await asyncio.sleep(sleep_time)
                wf_id, wf_input = next(deathstar_generator)
                resp = await client.invoke_workflow(wf_id, wf_input)
                meta = {
                    "op": f"{wf_id} : {wf_input}",
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

def run_worker(i):
    return asyncio.run(benchmark_runner(i))

async def run_pool(threads):
    loop = asyncio.get_running_loop()
    with ProcessPoolExecutor(max_workers=threads) as ex:
        tasks = [loop.run_in_executor(ex, run_worker, i) for i in range(threads)]
        return await asyncio.gather(*tasks)


async def main():
    client = make_detersl_client()
    await client.connect(start_metrics=True)
    try:
        await deathstar_init(client)

        results = await run_pool(threads)
        results = {k: v for d in results for k, v in d.items()}

        workflow_metrics = await client.wait_for_requests(results)
        for key, resp in tqdm(workflow_metrics.items()):
            completed_at_ms = resp["completed_at"]
            results[key]["completed_at_ms"] = completed_at_ms
            results[key]["latency_ms"] = completed_at_ms - results[key]["started_at_ms"]

        pd.DataFrame({"request_id": list(results.keys()),
                    "latency_ms": [res["latency_ms"] for res in results.values()],
                    "completed_at_ms": [res["completed_at_ms"] for res in results.values()],
                    "op": [res["op"] for res in results.values()]
                    }).sort_values("completed_at_ms").to_csv(f"{SAVE_DIR}/client_requests.csv", index=False)
    finally:
        await client.close()


if __name__ == "__main__":
    asyncio.run(main())
    
    calculate_metrics.main(
        SAVE_DIR,
        messages_per_second,
        warmup_seconds,
        threads
    )
