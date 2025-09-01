from django.contrib import admin
from .models import User


admin.site.register(User)
class UserAdmin(admin.ModelAdmin):
    list_display = ('username', 'email', 'first_name', 'last_name', 'is_admin', 'is_active', 'is_teacher', 'is_employee', 'is_student', 'created_at', 'updated_at')
    list_filter = ('is_admin', 'is_active', 'is_teacher', 'is_employee', 'is_student', 'created_at', 'updated_at')
    search_fields = ('username', 'first_name', 'last_name', 'email', 'phone_number', 'created_at', 'updated_at')
    ordering = ('username', 'created_at', 'updated_at')