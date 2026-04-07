import json
from time import sleep
import numpy as np

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *

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
        lat = values["lat"]
        lon = values["lon"]
        geo_keys = resources["geo_keys"]
        geo_dict = {key: json.loads(get(key).decode()) for key in geo_keys}

        point_keys = list(geo_dict)
        point_lats = np.array([geo_dict[key]["Plat"] for key in point_keys], dtype=float)
        point_lons = np.array([geo_dict[key]["Plon"] for key in point_keys], dtype=float)
        distances = haversine_km(lat, lon, point_lats, point_lons)
        nearby_indices = np.flatnonzero(distances < 10)
        sorted_indices = nearby_indices[np.argsort(distances[nearby_indices])]
        res = {"HotelIds": [point_keys[idx] for idx in sorted_indices[:5]]}
        set(resources["hotel_ids"], json.dumps(res).encode())
        
        return Output("success")
