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
        except (json.JSONDecodeError, KeyError, TypeError):
            return Output("invalid event data")

        key = str(resources["key"])
        value = values["value"]
        if isinstance(value, str):
            try:
                json.loads(value)
            except json.JSONDecodeError:
                return Output("invalid json value")
            payload = value
        else:
            payload = json.dumps(value)

        set(key, payload.encode())

        return Output(payload)
