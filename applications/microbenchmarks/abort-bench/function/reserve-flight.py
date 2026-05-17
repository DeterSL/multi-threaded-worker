import json

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *


class FuncHandler(BaseFuncHandler):
    def handle(self, event: Event) -> Output:
        
        data = json.loads(event.data)
        resources = data["resources"]
        key = str(resources["flight_data"])

        flight_data = json.loads(get(key).decode())
        flight_data["Cap"] -= 1
        if flight_data["Cap"] < 0:
            return Output(json.dumps({"success": False}))

        set(key, json.dumps(flight_data).encode())
    
        return Output(json.dumps({"success": True}))
