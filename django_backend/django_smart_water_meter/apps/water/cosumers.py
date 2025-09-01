import json
from channels.generic.websocket import AsyncWebsocketConsumer
from asgiref.sync import sync_to_async
from django.utils.timezone import now
from .models import WaterLog, Device


class WaterFlowConsumer(AsyncWebsocketConsumer):
    async def connect(self):
        user = self.scope["user"]

        if user.is_authenticated:
            self.user_group = f"user_{user.id}"
            await self.channel_layer.group_add(self.user_group, self.channel_name)
            await self.accept()

            await self.send(text_data=json.dumps({
                "status": "connected",
                "message": f"Welcome {user.username}, listening for your devices."
            }))
        else:
            await self.accept()
            await self.send(text_data=json.dumps({
                "status": "connected",
                "message": "Device or guest connected."
            }))

    async def disconnect(self, close_code):
        if hasattr(self, "user_group"):
            await self.channel_layer.group_discard(self.user_group, self.channel_name)

    async def receive(self, text_data):
        data = json.loads(text_data)
        device_id = data.get("device_id")
        count = data.get("count")

        if device_id and count is not None:
            try:
                # Find the device
                device = await sync_to_async(Device.objects.get)(device_id=device_id)

                # Update last_seen timestamp
                device.last_seen = now()
                await sync_to_async(device.save)()

                # Create the log
                log = await sync_to_async(WaterLog.objects.create)(
                    device=device,
                    count=count
                )

                payload = {
                    "status": "ok",
                    "device_id": device.device_id,
                    "count": count,
                    "liters": log.liters,
                    "timestamp": log.created_at.isoformat(),
                }

                # Reply to ESP32
                await self.send(text_data=json.dumps(payload))

                # Notify all users associated with this device
                users = await sync_to_async(lambda: list(device.users.all()))()
                for user in users:
                    await self.channel_layer.group_send(
                        f"user_{user.id}",
                        {"type": "new_water_log", "data": payload}
                    )

            except Device.DoesNotExist:
                await self.send(text_data=json.dumps({
                    "status": "error",
                    "message": f"Unknown device_id {device_id}"
                }))

    async def new_water_log(self, event):
        await self.send(text_data=json.dumps(event["data"]))
