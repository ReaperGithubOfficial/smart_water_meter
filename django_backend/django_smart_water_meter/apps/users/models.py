from django.db import models
from django.contrib.auth.models import AbstractBaseUser
from django.utils.translation import gettext_lazy as _
from .managers import UserManager
from base.models import BaseModel
from uuid import uuid4


class User(BaseModel, AbstractBaseUser):
    id = models.UUIDField(primary_key=True, default=uuid4, editable=False, verbose_name=_("ID"))
    first_name = models.CharField(max_length=100, verbose_name=_("First Name"))
    last_name = models.CharField(max_length=100, verbose_name=_("Last Name"))
    username = models.CharField(max_length=100, unique=True, verbose_name=_("Username"), db_index=True)
    phone_number = models.CharField(max_length=14, unique=True, null=True, blank=True, verbose_name=_("Phone Number"))
    email = models.EmailField(max_length=200, unique=True, verbose_name=_("Email"), db_index=True)
    birth_date = models.DateTimeField(null=True, blank=True, verbose_name=_("Birth Date"))

    # Permission fields
    is_admin = models.BooleanField(default=False, verbose_name=_("Is Admin"))
    is_active = models.BooleanField(default=True, verbose_name=_("Is Active"))
    is_staff = models.BooleanField(default=False)
    is_superuser = models.BooleanField(default=False)

    # Custom role fields
    is_teacher = models.BooleanField(default=False, verbose_name=_("Is Teacher"))
    is_employee = models.BooleanField(default=False, verbose_name=_("Is Employee"))
    is_student = models.BooleanField(default=False, verbose_name=_("Is Student"))

    objects = UserManager()

    USERNAME_FIELD = 'username'
    REQUIRED_FIELDS = ["phone_number", "email", "first_name", "last_name"]

    def __str__(self):
        return self.username

    def has_perm(self, perm, obj=None):
        return self.is_admin or self.is_superuser

    def has_module_perms(self, app_label):
        return True