from django import forms
from django.utils.translation import gettext_lazy as _
from .models import Device

class ClaimDeviceForm(forms.Form):
    device_id = forms.CharField(
        max_length=100,
        label=_("Device ID"),
        widget=forms.TextInput(attrs={
            "class": "border rounded p-2 w-full",
            "placeholder": _("Enter your device ID")
        })
    )

    def clean_device_id(self):
        device_id = self.cleaned_data['device_id']
        if not Device.objects.filter(device_id=device_id).exists():
            raise forms.ValidationError(_("Device ID does not exist."))
        return device_id


class CreateDeviceForm(forms.ModelForm):
    class Meta:
        model = Device
        fields = ["device_id", "name", "pulse_to_liter"]
        labels = {
            "device_id": _("Device ID"),
            "name": _("Device Name"),
            "pulse_to_liter": _("Pulse to Liter"),
        }
        widgets = {
            "device_id": forms.TextInput(attrs={"class": "border rounded p-2 w-full", "placeholder": _("Unique device ID")}),
            "name": forms.TextInput(attrs={"class": "border rounded p-2 w-full", "placeholder": _("Optional device name")}),
            "pulse_to_liter": forms.NumberInput(attrs={"class": "border rounded p-2 w-full", "step": 0.01}),
        }

    def save(self, user=None, commit=True):
        """Automatically add the logged-in user as owner"""
        device = super().save(commit=False)
        if commit:
            device.save()
            if user:
                device.users.add(user)
        return device
