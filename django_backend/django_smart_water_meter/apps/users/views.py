from django.shortcuts import render, redirect
from django.contrib import messages
from .forms import UserCreateForm, UserUpdateForm

# Create a new user (admin or signup)
def create_user(request):
    if request.method == "POST":
        form = UserCreateForm(request.POST)
        if form.is_valid():
            form.save()
            messages.success(request, "User created successfully!")
            return redirect("dashboard")
    else:
        form = UserCreateForm()
    return render(request, "users/create_user.html", {"form": form})

# Update logged-in user's profile
def update_profile(request):
    if not request.user.is_authenticated:
        return redirect("login")

    if request.method == "POST":
        form = UserUpdateForm(request.POST, instance=request.user)
        if form.is_valid():
            form.save()
            messages.success(request, "Profile updated successfully!")
            return redirect("dashboard")
    else:
        form = UserUpdateForm(instance=request.user)
    return render(request, "users/update_profile.html", {"form": form})
