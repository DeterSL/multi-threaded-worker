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
        key = str(resources["hotel_data"])
        
        hotel_data = json.loads(get(key).decode())

        hotel_data["Cap"] -= 1
        if hotel_data["Cap"] < 0:
            raise NotEnoughSpace(f"Not enough space: for hotel: {key}")
        
        set(key, json.dumps(hotel_data).encode())
        return Output("success")
