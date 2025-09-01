from django.db import models
from django.conf import settings
from django.utils.translation import gettext_lazy as _
from base.models import BaseModel


class Device(models.Model):
    users = models.ManyToManyField(
        settings.AUTH_USER_MODEL,
        related_name="devices",
        verbose_name=_("Users")
    )
    device_id = models.CharField(
        max_length=100,
        unique=True,
        verbose_name=_("Device ID")
    )
    name = models.CharField(
        max_length=100,
        blank=True,
        null=True,
        verbose_name=_("Device Name")
    )
    pulse_to_liter = models.FloatField(
        default=650.0,
        verbose_name=_("Pulse to Liter")
    )
    last_seen = models.DateTimeField(
        blank=True, null=True
    )

    class Meta:
        verbose_name = _("Device")
        verbose_name_plural = _("Devices")

    def __str__(self):
        return f"{self.device_id}"


class WaterLog(BaseModel):
    device = models.ForeignKey(
        Device,
        on_delete=models.CASCADE,
        related_name="logs",
        verbose_name=_("Device")
    )
    count = models.PositiveIntegerField(
        verbose_name=_("Pulse Count")
    )
    liters = models.FloatField(
        editable=False,
        verbose_name=_("Liters")
    )

    class Meta:
        verbose_name = _("Water Log")
        verbose_name_plural = _("Water Logs")

    def save(self, *args, **kwargs):
        self.liters = self.count / self.device.pulse_to_liter
        super().save(*args, **kwargs)

    def __str__(self):
        return f"{self.device.device_id} - {self.liters:.2f} L"
