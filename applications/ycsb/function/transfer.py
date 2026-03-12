import json
from time import sleep

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *

class NotEnoughCredit(Exception):
    pass

def add_1_to(key):
    to_value = int.from_bytes(get(key))
    to_value += 1
    set(key, to_value.to_bytes(4))

def remove_1_from(key):
    from_val = int.from_bytes(get(key))
    from_val -= 1

    if from_val < 0:
        raise NotEnoughCredit(f"Not enough credit for"
                              f" user: {key}")
    
    set(key, from_val.to_bytes(4))

class FuncHandler(BaseFuncHandler):
    def handle(self, event: Event) -> Output:
        try:
            data = json.loads(event.data)
            resources = data["resources"]
        except json.JSONDecodeError:
            data = {}
        
        transfer_from = str(resources["from"])
        transfer_to = str(resources["to"])

        add_1_to(transfer_to)

        # Will throw exception and abort both transactions if not enough credit
        remove_1_from(transfer_from)
        
        data = {'success': True}
        output = Output(json.dumps(data))
        return output