import json
from time import sleep

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *

class NotEnoughSpace(Exception):
    pass

class FuncHandler(BaseFuncHandler):
    def handle(self, event: Event) -> Output:
        try:
            data = json.loads(event.data)
            resources = data["resources"]
        except json.JSONDecodeError:
            data = {}
        key = str(resources["flight_data"])
        
        flight_data = json.loads(get(key).decode())

        flight_data["Cap"] -= 1
        if flight_data["Cap"] < 0:
            raise NotEnoughSpace(f"Not enough space: for flight: {key}")
        
        set(key, json.dumps(flight_data).encode())
        return Output("success")
     