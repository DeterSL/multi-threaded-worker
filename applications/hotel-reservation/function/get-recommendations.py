import json
from time import sleep
import numpy as np

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *

class WrongRecommendationOption(Exception):
    pass

def haversine_km(lat1, lon1, lat2, lon2):
    # Spherical Earth approximation for distance in km.
    r = 6371.0088
    lat1 = np.asarray(lat1, dtype=float)
    lon1 = np.asarray(lon1, dtype=float)
    lat2 = np.asarray(lat2, dtype=float)
    lon2 = np.asarray(lon2, dtype=float)

    phi1 = np.radians(lat1)
    phi2 = np.radians(lat2)
    dphi = np.radians(lat2 - lat1)
    dlambda = np.radians(lon2 - lon1)
    a = np.sin(dphi / 2) ** 2 + np.cos(phi1) * np.cos(phi2) * np.sin(dlambda / 2) ** 2
    distances = 2 * r * np.arcsin(np.sqrt(np.clip(a, 0.0, 1.0)))
    return float(distances) if np.ndim(distances) == 0 else distances

class FuncHandler(BaseFuncHandler):
    def handle(self, event: Event) -> Output:
        try:
            data = json.loads(event.data)
            resources = data["resources"]
            values = data["values"]
        except json.JSONDecodeError:
            data = {}
        
        requirement = values["requirement"]
        lat = values["lat"]
        lon = values["lon"]

        rec_keys = resources["rec_keys"]
        rec_d = {key: json.loads(get(key).decode()) for key in rec_keys}

        if requirement == "dis":
            hotel_keys = list(rec_d)
            hotel_lats = np.array([rec_d[key]["HLat"] for key in hotel_keys], dtype=float)
            hotel_lons = np.array([rec_d[key]["HLon"] for key in hotel_keys], dtype=float)
            distances = haversine_km(lat, lon, hotel_lats, hotel_lons)
            min_dist = distances.min()
            res = [hotel_keys[idx] for idx in np.flatnonzero(distances == min_dist)]
        elif requirement == "rate":
            max_rate = max(rec_d, key=lambda x: rec_d[x]["HRate"])
            res = [k for k, v in rec_d.items() if v["HRate"] == max_rate]
        elif requirement == "price":
            min_price = min(rec_d, key=lambda x: rec_d[x]["HPrice"])
            res = [k for k, v in rec_d.items() if v["HPrice"] == min_price]
        else:
            raise WrongRecommendationOption(f"No such requirement: {requirement}")
        
        return Output(json.dumps(res))
