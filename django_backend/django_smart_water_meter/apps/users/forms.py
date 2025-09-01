from django import forms
from django.utils.translation import gettext_lazy as _
from .models import User


class UserCreateForm(forms.ModelForm):
    password1 = forms.CharField(
        label=_("Password"),
        widget=forms.PasswordInput(attrs={"class": "border rounded p-2 w-full"}),
    )
    password2 = forms.CharField(
        label=_("Confirm Password"),
        widget=forms.PasswordInput(attrs={"class": "border rounded p-2 w-full"}),
    )

    class Meta:
        model = User
        fields = ["first_name", "last_name", "username", "phone_number", "email", "birth_date"]
        widgets = {
            "first_name": forms.TextInput(attrs={"class": "border rounded p-2 w-full"}),
            "last_name": forms.TextInput(attrs={"class": "border rounded p-2 w-full"}),
            "username": forms.TextInput(attrs={"class": "border rounded p-2 w-full"}),
            "phone_number": forms.TextInput(attrs={"class": "border rounded p-2 w-full"}),
            "email": forms.EmailInput(attrs={"class": "border rounded p-2 w-full"}),
            "birth_date": forms.DateTimeInput(attrs={"class": "border rounded p-2 w-full", "type": "date"}),
        }

    def clean_password2(self):
        password1 = self.cleaned_data.get("password1")
        password2 = self.cleaned_data.get("password2")
        if password1 and password2 and password1 != password2:
            raise forms.ValidationError(_("Passwords don't match"))
        return password2

    def save(self, commit=True):
        user = super().save(commit=False)
        user.set_password(self.cleaned_data["password1"])
        if commit:
            user.save()
        return user


class UserUpdateForm(forms.ModelForm):
    class Meta:
        model = User
        fields = ["first_name", "last_name", "phone_number", "email", "birth_date"]
        widgets = {
            "first_name": forms.TextInput(attrs={"class": "border rounded p-2 w-full"}),
            "last_name": forms.TextInput(attrs={"class": "border rounded p-2 w-full"}),
            "phone_number": forms.TextInput(attrs={"class": "border rounded p-2 w-full"}),
            "email": forms.EmailInput(attrs={"class": "border rounded p-2 w-full"}),
            "birth_date": forms.DateInput(attrs={"class": "border rounded p-2 w-full", "type": "date"}),
        }
