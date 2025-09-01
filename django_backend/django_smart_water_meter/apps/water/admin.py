from django.contrib import admin
from django.utils.html import format_html
from django.utils.timezone import now
from datetime import timedelta
from .models import Device, WaterLog

ONLINE_THRESHOLD = timedelta(seconds=30)

class WaterLogInline(admin.TabularInline):
    model = WaterLog
    extra = 0
    readonly_fields = ('count', 'liters', 'created_at')
    can_delete = False
    ordering = ('-created_at',)

@admin.register(Device)
class DeviceAdmin(admin.ModelAdmin):
    list_display = ('device_id', 'name', 'users_list', 'last_seen', 'online_status')
    search_fields = ('device_id', 'name', 'users__username')
    list_filter = ('users',)
    inlines = [WaterLogInline]
    filter_horizontal = ('users',)

    def users_list(self, obj):
        return ", ".join([u.username for u in obj.users.all()])
    users_list.short_description = "Users"

    def online_status(self, obj):
        if obj.last_seen and (now() - obj.last_seen < ONLINE_THRESHOLD):
            color = 'green'
            status = 'Online'
        else:
            color = 'red'
            status = 'Offline'
        return format_html(
            '<span style="color:{}; font-weight:bold;">{}</span>',
            color,
            status
        )
    online_status.short_description = "Status"

@admin.register(WaterLog)
class WaterLogAdmin(admin.ModelAdmin):
    list_display = ('device', 'count', 'liters', 'created_at')
    list_filter = ('device',)
    search_fields = ('device__device_id',)
    readonly_fields = ('liters',)
    ordering = ('-created_at',)
