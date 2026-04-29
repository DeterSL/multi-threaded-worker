import json

from wit_world.exports import FuncHandler as BaseFuncHandler
from wit_world.exports.func_handler import Event, Output
from wit_world.imports.kv import *


class FuncHandler(BaseFuncHandler):
    def handle(self, event: Event) -> Output:
        
        data = json.loads(event.data)
        resources = data["resources"]
        values = data["values"]

        order_key = resources["order_key"]
        flight_id = values["flight_id"]
        hotel_id = values["hotel_id"]
        user_id = values["user_id"]

        order_data = {
            "FlightId": flight_id,
            "HotelId": hotel_id,
            "UserId": user_id,
        }
        set(order_key, json.dumps(order_data).encode())
      
        
        return Output("success")
