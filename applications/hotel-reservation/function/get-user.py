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
            stored_password = get(str(resources["username"])).decode()
            tentative = values["password"]

            if stored_password == tentative:
                data = {'success': True}
            else:
                data = {'success': False}
        except Exception as e:
            data = {'success': False, 'error': str(e)}
            
        output = Output(json.dumps(data))
        return output