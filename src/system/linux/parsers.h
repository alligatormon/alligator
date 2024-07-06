#pragma once
void get_conntrack_info();
void memory_errors_by_controller();
void get_distribution_name();
void ipset();
void sysctl_run(alligator_ht* sysctl);
void sysctl_free(alligator_ht* sysctl);
void get_proc_interrupts(int extended_mode);
