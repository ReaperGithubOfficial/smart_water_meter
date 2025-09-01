from django.urls import path
from . import views

urlpatterns = [
    path("create/", views.create_user, name="create_user"),
    path("profile/", views.update_profile, name="update_profile"),
]
