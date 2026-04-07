import json
from time import sleep

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *

class FuncHandler(BaseFuncHandler):
    def handle(self, event: Event) -> Output:
        try:
            data = json.loads(event.data)
            resources = data["resources"]
            values = data["values"]
        except json.JSONDecodeError:
            data = {}
        
        try:
            hotel_ids_data = get(resources["hotel_ids"])
            hotel_ids = json.loads(hotel_ids_data.decode())["HotelIds"]
            rate_keys = values["rate_keys"]

            def geo_to_rate(k: str) -> str:
                return "rate" + k[3:] if k.startswith("geo") else k
            
            res_plans = [geo_to_rate(h) for h in hotel_ids if geo_to_rate(h) in rate_keys]

            return Output(json.dumps(res_plans))
        except Exception as e:
            return Output(f"error: {str(e)}")   