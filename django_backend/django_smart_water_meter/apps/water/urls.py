from django.urls import path
from . import views

urlpatterns = [
    path("claim-device/", views.claim_device, name="claim_device"),
    path("", views.dashboard, name="dashboard"),
    path("create-device/", views.create_device, name="create_device")
]
