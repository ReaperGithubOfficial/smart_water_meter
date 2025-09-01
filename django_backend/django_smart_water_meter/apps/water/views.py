from django.shortcuts import render, redirect
from django.utils.timezone import now
from datetime import timedelta
from django.contrib import messages
from .models import Device, WaterLog
from .forms import ClaimDeviceForm, CreateDeviceForm

ONLINE_THRESHOLD = timedelta(seconds=30)

# Dashboard view
def dashboard(request):
    user = request.user
    if not user.is_authenticated:
        devices = []
    else:
        devices = list(user.devices.all())

    for device in devices:
        device.is_online = device.last_seen and (now() - device.last_seen < ONLINE_THRESHOLD)

    # Get latest 20 logs for the user's devices
    logs = WaterLog.objects.filter(device__in=devices).order_by("-created_at")[:20]

    return render(request, "water/dashboard.html", {
        "devices": devices,
        "logs": logs
    })


# Claim an existing device by device_id
def claim_device(request):
    if not request.user.is_authenticated:
        return redirect('login')

    if request.method == "POST":
        form = ClaimDeviceForm(request.POST)
        if form.is_valid():
            device_id = form.cleaned_data['device_id']
            device = Device.objects.get(device_id=device_id)

            if request.user in device.users.all():
                messages.info(request, "You already claimed this device.")
            else:
                device.users.add(request.user)
                messages.success(request, f"Device {device_id} added successfully.")

            return redirect('dashboard')
    else:
        form = ClaimDeviceForm()

    return render(request, "water/claim_device.html", {"form": form})


# Create a new device and automatically assign it to the logged-in user
def create_device(request):
    if not request.user.is_authenticated:
        return redirect('login')

    if request.method == "POST":
        form = CreateDeviceForm(request.POST)
        if form.is_valid():
            form.save(user=request.user)
            messages.success(request, "Device created and assigned successfully!")
            return redirect('dashboard')
    else:
        form = CreateDeviceForm()

    return render(request, "water/create_device.html", {"form": form})
