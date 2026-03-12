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

        key = str(resources["key"])
        value : int = int(values["value"])

        set(key, value.to_bytes(4))

        data = {'success': True}
        output = Output(json.dumps(data))
        return output