#pragma once
#define PLATFORM_BAREMETAL 0
#define PLATFORM_LXC 1
#define PLATFORM_OPENVZ 2
#define PLATFORM_NSPAWN 3
#define PLATFORM_DOCKER 4
#define PLATFORM_KVM 5
#define PLATFORM_OPENSTACK 6
void pidfile_push(char *file, int type);
void pidfile_del(char *file, int type);
void userprocess_push(alligator_ht *userprocess, char *user);
void userprocess_del(alligator_ht* userprocess, char *user);
void sysctl_del(alligator_ht* sysctl, char *sysctl_name);
void sysctl_push(alligator_ht *sysctl, char *sysctl_name);
void system_free();
void ipaddr_info();
void hw_cpu_info();
void get_utsname();
void get_smbios();
