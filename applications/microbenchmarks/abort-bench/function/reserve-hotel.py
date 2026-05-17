import json

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *


class FuncHandler(BaseFuncHandler):
    def handle(self, event: Event) -> Output:
        
        data = json.loads(event.data)
        resources = data["resources"]
        values = data["values"]
        key = str(resources["hotel_data"])

        hotel_data = json.loads(get(key).decode())
        if values.get("should_fail", False):
            raise RuntimeError(f"Injected failure for hotel {key}")
        hotel_data["Cap"] -= 1
        if hotel_data["Cap"] < 0:
            return Output(json.dumps({"success": False}))
        
        set(key, json.dumps(hotel_data).encode())
     
        return Output(json.dumps({"success": True}))
