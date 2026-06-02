#include "main.h"
#include "metric/metric_types.h"
#include "metric/namespace.h"

static void system_register_metric_families(context_arg *carg)
{
	if (!carg)
		return;

	/* Core host/system families. */
	namespace_metric_family_set(NULL, carg, "iface_num", METRIC_TYPE_GAUGE, "Number of network interfaces discovered.");
	namespace_metric_family_set(NULL, carg, "interface_address", METRIC_TYPE_GAUGE, "Interface address presence with interface/address labels.");
	namespace_metric_family_set(NULL, carg, "cpu_model", METRIC_TYPE_GAUGE, "CPU model info as labels.");
	namespace_metric_family_set(NULL, carg, "machine_arch", METRIC_TYPE_GAUGE, "Machine architecture info as labels.");
	namespace_metric_family_set(NULL, carg, "system_uptime_seconds", METRIC_TYPE_GAUGE, "System uptime in seconds.");
	namespace_metric_family_set(NULL, carg, "mounts_num", METRIC_TYPE_GAUGE, "Number of mounted filesystems.");

	/* Disk/filesystem families. */
	namespace_metric_family_set(NULL, carg, "disk_usage", METRIC_TYPE_GAUGE, "Filesystem space usage in bytes by mountpoint and type.");
	namespace_metric_family_set(NULL, carg, "disk_usage_percent", METRIC_TYPE_GAUGE, "Filesystem space usage percentage by mountpoint and type.");
	namespace_metric_family_set(NULL, carg, "disk_inodes", METRIC_TYPE_GAUGE, "Filesystem inode counts by mountpoint and type.");
	namespace_metric_family_set(NULL, carg, "disk_inodes_percent", METRIC_TYPE_GAUGE, "Filesystem inode usage percentage by mountpoint and type.");
	namespace_metric_family_set(NULL, carg, "disk_filesystem", METRIC_TYPE_GAUGE, "Filesystem type marker by mountpoint and fs labels.");
	namespace_metric_family_set(NULL, carg, "disk_io", METRIC_TYPE_GAUGE, "Disk I/O values by device and metric subtype.");
	namespace_metric_family_set(NULL, carg, "disk_io_await_seconds_total", METRIC_TYPE_COUNTER, "Cumulative disk I/O wait time in seconds by device and operation type.");
	namespace_metric_family_set(NULL, carg, "disk_busy", METRIC_TYPE_GAUGE, "Disk busy time ratio by device.");
	namespace_metric_family_set(NULL, carg, "disk_model", METRIC_TYPE_GAUGE, "Disk model information as labels.");
	namespace_metric_family_set(NULL, carg, "disk_num", METRIC_TYPE_GAUGE, "Number of block devices discovered.");

	/* Memory/CPU/process families. */
	namespace_metric_family_set(NULL, carg, "memory_usage", METRIC_TYPE_GAUGE, "Memory usage values by memory component type.");
	namespace_metric_family_set(NULL, carg, "memory_usage_hw", METRIC_TYPE_GAUGE, "Raw memory counters from host memory info by type.");
	namespace_metric_family_set(NULL, carg, "memory_usage_cgroup", METRIC_TYPE_GAUGE, "Raw memory counters from cgroup memory info by type.");
	namespace_metric_family_set(NULL, carg, "memory_usage_percent", METRIC_TYPE_GAUGE, "Memory usage percentage by type.");
	namespace_metric_family_set(NULL, carg, "memory_dimm_size_bytes", METRIC_TYPE_GAUGE, "Per-DIMM memory size in bytes by EDAC controller/channel/slot labels.");
	namespace_metric_family_set(NULL, carg, "memory_errors", METRIC_TYPE_GAUGE, "Per-DIMM memory error counters by EDAC labels and error type.");
	namespace_metric_family_set(NULL, carg, "memory_stat", METRIC_TYPE_GAUGE, "Memory paging and swap statistics by type.");
	namespace_metric_family_set(NULL, carg, "load_average", METRIC_TYPE_GAUGE, "System load averages by period label.");
	namespace_metric_family_set(NULL, carg, "cpu_usage_average_percent", METRIC_TYPE_GAUGE, "Rolling average CPU usage percentage.");
	namespace_metric_family_set(NULL, carg, "cpu_usage_core_time", METRIC_TYPE_COUNTER, "Cumulative CPU time in seconds by core and mode.");
	namespace_metric_family_set(NULL, carg, "cpu_usage_time", METRIC_TYPE_COUNTER, "Cumulative CPU time in seconds by mode (from /proc/stat).");
	namespace_metric_family_set(NULL, carg, "cpu_usage_hw_time", METRIC_TYPE_COUNTER, "Cumulative host CPU time in seconds by mode when scraped from a cgroup.");
	namespace_metric_family_set(NULL, carg, "cpu_usage_core", METRIC_TYPE_GAUGE, "Per-core CPU usage percentage by mode.");
	namespace_metric_family_set(NULL, carg, "cpu_current_frequency_hertz", METRIC_TYPE_GAUGE, "Current CPU frequency in hertz by core.");
	namespace_metric_family_set(NULL, carg, "cpu_usage_calc_delta_seconds", METRIC_TYPE_GAUGE, "Elapsed wall-clock time between CPU usage calculations in seconds.");
	namespace_metric_family_set(NULL, carg, "cores_num", METRIC_TYPE_GAUGE, "Effective number of CPU cores available to alligator.");
	namespace_metric_family_set(NULL, carg, "cores_num_cgroup", METRIC_TYPE_GAUGE, "Number of CPU cores available by cgroup quota.");
	namespace_metric_family_set(NULL, carg, "cores_num_hw", METRIC_TYPE_GAUGE, "Number of online hardware CPU cores.");
	namespace_metric_family_set(NULL, carg, "forks_total", METRIC_TYPE_COUNTER, "Total number of forks since boot.");
	namespace_metric_family_set(NULL, carg, "context_switches_total", METRIC_TYPE_COUNTER, "Total number of context switches since boot.");
	namespace_metric_family_set(NULL, carg, "interrupts_total", METRIC_TYPE_COUNTER, "Total number of interrupts since boot.");
	namespace_metric_family_set(NULL, carg, "interrupts_by_irq_total", METRIC_TYPE_COUNTER, "Total interrupts by interrupt code and description.");
	namespace_metric_family_set(NULL, carg, "cpu_cgroup_periods_total", METRIC_TYPE_COUNTER, "Total CPU cgroup scheduling periods.");
	namespace_metric_family_set(NULL, carg, "cpu_cgroup_throttled_periods_total", METRIC_TYPE_COUNTER, "Total throttled CPU cgroup periods.");
	namespace_metric_family_set(NULL, carg, "cpu_cgroup_throttled_seconds_total", METRIC_TYPE_COUNTER, "Total throttled CPU time in seconds.");

	namespace_metric_family_set(NULL, carg, "process_stats", METRIC_TYPE_GAUGE, "Process statistics by process labels and stat type.");
	namespace_metric_family_set(NULL, carg, "process_memory", METRIC_TYPE_GAUGE, "Process memory values by process and memory type.");
	namespace_metric_family_set(NULL, carg, "process_io_bytes_total", METRIC_TYPE_COUNTER, "Per-process I/O byte counters by process and operation labels.");
	namespace_metric_family_set(NULL, carg, "process_io_chars_total", METRIC_TYPE_COUNTER, "Per-process I/O character counters by process and operation labels.");
	namespace_metric_family_set(NULL, carg, "process_io_syscalls_total", METRIC_TYPE_COUNTER, "Per-process I/O syscall counters by process and operation labels.");
	namespace_metric_family_set(NULL, carg, "process_page_faults_total", METRIC_TYPE_COUNTER, "Per-process page fault counters by process and fault type.");
	namespace_metric_family_set(NULL, carg, "process_schedstat_run_periods_total", METRIC_TYPE_COUNTER, "Per-process scheduler run period counters.");
	namespace_metric_family_set(NULL, carg, "process_schedstat_run_time_nanoseconds_total", METRIC_TYPE_COUNTER, "Per-process scheduler run time in nanoseconds.");
	namespace_metric_family_set(NULL, carg, "process_schedstat_runqueue_time_nanoseconds_total", METRIC_TYPE_COUNTER, "Per-process scheduler runqueue time in nanoseconds.");
	namespace_metric_family_set(NULL, carg, "process_vmap_count", METRIC_TYPE_GAUGE, "Per-process virtual memory map count.");
	namespace_metric_family_set(NULL, carg, "process_match", METRIC_TYPE_GAUGE, "Process match flag by process selector.");
	namespace_metric_family_set(NULL, carg, "process_uptime", METRIC_TYPE_GAUGE, "Per-process uptime in seconds.");
	namespace_metric_family_set(NULL, carg, "process_rlimit", METRIC_TYPE_GAUGE, "Process rlimit values by process and resource type.");
	namespace_metric_family_set(NULL, carg, "process_rlimit_usage", METRIC_TYPE_GAUGE, "Process rlimit usage ratio by process and resource type.");
	namespace_metric_family_set(NULL, carg, "process_vmap_usage", METRIC_TYPE_GAUGE, "Process virtual memory map usage ratio.");
	namespace_metric_family_set(NULL, carg, "process_cpu_seconds_total", METRIC_TYPE_COUNTER, "Per-process CPU time in seconds by mode.");
	namespace_metric_family_set(NULL, carg, "process_states", METRIC_TYPE_GAUGE, "Number of processes by scheduler state.");
	namespace_metric_family_set(NULL, carg, "tasks_total", METRIC_TYPE_GAUGE, "Total number of tasks.");
	namespace_metric_family_set(NULL, carg, "tasks_usage", METRIC_TYPE_GAUGE, "Task limit usage percentage.");
	namespace_metric_family_set(NULL, carg, "tasks_max", METRIC_TYPE_GAUGE, "Maximum number of tasks allowed by the kernel.");
	namespace_metric_family_set(NULL, carg, "processes_total", METRIC_TYPE_GAUGE, "Total number of processes.");
	namespace_metric_family_set(NULL, carg, "processes", METRIC_TYPE_GAUGE, "Total number of processes.");
	namespace_metric_family_set(NULL, carg, "processes_max", METRIC_TYPE_GAUGE, "Maximum number of processes allowed by the kernel.");
	namespace_metric_family_set(NULL, carg, "processes_usage", METRIC_TYPE_GAUGE, "PID limit usage percentage.");
	namespace_metric_family_set(NULL, carg, "open_files", METRIC_TYPE_GAUGE, "Total number of open file descriptors on the host.");
	namespace_metric_family_set(NULL, carg, "open_files_process", METRIC_TYPE_GAUGE, "Total number of open file descriptors used by processes.");
	namespace_metric_family_set(NULL, carg, "open_files_system", METRIC_TYPE_GAUGE, "Total number of open file descriptors used by the system.");
	namespace_metric_family_set(NULL, carg, "open_pipes", METRIC_TYPE_GAUGE, "Total number of open pipes on the host.");
	namespace_metric_family_set(NULL, carg, "open_sockets", METRIC_TYPE_GAUGE, "Total number of open sockets on the host.");
	namespace_metric_family_set(NULL, carg, "max_files", METRIC_TYPE_GAUGE, "Maximum number of file descriptors allowed by the kernel.");
	namespace_metric_family_set(NULL, carg, "max_files_per_proc", METRIC_TYPE_GAUGE, "Maximum open files per process.");
	namespace_metric_family_set(NULL, carg, "max_sockets", METRIC_TYPE_GAUGE, "Maximum number of sockets allowed by the kernel.");
	namespace_metric_family_set(NULL, carg, "swap_usage", METRIC_TYPE_GAUGE, "Swap space in kilobytes by usage type.");
	namespace_metric_family_set(NULL, carg, "vm_faults", METRIC_TYPE_COUNTER, "Total VM page faults since boot.");
	namespace_metric_family_set(NULL, carg, "io_faults", METRIC_TYPE_COUNTER, "Total VM I/O faults since boot.");
	namespace_metric_family_set(NULL, carg, "vforks", METRIC_TYPE_COUNTER, "Total vfork operations since boot.");
	namespace_metric_family_set(NULL, carg, "rforks", METRIC_TYPE_COUNTER, "Total rfork operations since boot.");
	namespace_metric_family_set(NULL, carg, "system_calls", METRIC_TYPE_COUNTER, "Total system calls since boot.");
	namespace_metric_family_set(NULL, carg, "kernel_version", METRIC_TYPE_GAUGE, "Kernel version marker with version and platform labels.");
	namespace_metric_family_set(NULL, carg, "btmp_file_size", METRIC_TYPE_GAUGE, "Current btmp file size in bytes.");
	namespace_metric_family_set(NULL, carg, "buddyinfo_count", METRIC_TYPE_GAUGE, "Free memory blocks by NUMA node, zone, and order.");
	namespace_metric_family_set(NULL, carg, "numa_stat", METRIC_TYPE_GAUGE, "NUMA statistics by node and metric type.");
	namespace_metric_family_set(NULL, carg, "os_distribution", METRIC_TYPE_GAUGE, "Operating system distribution information as labels.");
	namespace_metric_family_set(NULL, carg, "pgscan_total", METRIC_TYPE_COUNTER, "Total page scan events from vmstat by scan type.");
	namespace_metric_family_set(NULL, carg, "pgsteal_total", METRIC_TYPE_COUNTER, "Total page steal events from vmstat by steal type.");

	/* Network families. */
	namespace_metric_family_set(NULL, carg, "if_stat", METRIC_TYPE_GAUGE, "Network interface statistics by interface and stat type.");
	namespace_metric_family_set(NULL, carg, "interface_stats", METRIC_TYPE_GAUGE, "Network interface statistics by interface and stat type.");
	namespace_metric_family_set(NULL, carg, "network_stat_total", METRIC_TYPE_COUNTER, "Total network stack statistics from /proc/net/netstat by protocol and stat labels.");
	namespace_metric_family_set(NULL, carg, "if_speed", METRIC_TYPE_GAUGE, "Network interface speed.");
	namespace_metric_family_set(NULL, carg, "if_up", METRIC_TYPE_GAUGE, "Network interface administrative state.");
	namespace_metric_family_set(NULL, carg, "if_duplex", METRIC_TYPE_GAUGE, "Network interface duplex mode marker.");
	namespace_metric_family_set(NULL, carg, "link_status", METRIC_TYPE_GAUGE, "Network interface link state.");
	namespace_metric_family_set(NULL, carg, "port_usage_count", METRIC_TYPE_GAUGE, "Port usage count by protocol and port labels.");
	namespace_metric_family_set(NULL, carg, "udp_entrypoint_read_total", METRIC_TYPE_COUNTER, "Total UDP datagrams read by entrypoint.");

	/* Conntrack/firewall families. */
	namespace_metric_family_set(NULL, carg, "conntrack_max", METRIC_TYPE_GAUGE, "Maximum conntrack entries.");
	namespace_metric_family_set(NULL, carg, "conntrack_count", METRIC_TYPE_GAUGE, "Current conntrack entries.");
	namespace_metric_family_set(NULL, carg, "conntrack_usage", METRIC_TYPE_GAUGE, "Conntrack usage percentage.");
	namespace_metric_family_set(NULL, carg, "softirq_total", METRIC_TYPE_COUNTER, "Total softirq events.");
	namespace_metric_family_set(NULL, carg, "time_now", METRIC_TYPE_GAUGE, "Current Unix timestamp.");
	namespace_metric_family_set(NULL, carg, "core_temperature_celsius", METRIC_TYPE_GAUGE, "Core/component temperature in degrees Celsius by sensor labels.");
	namespace_metric_family_set(NULL, carg, "firewall_packets_total", METRIC_TYPE_COUNTER, "Total firewall packets by chain/rule labels.");
	namespace_metric_family_set(NULL, carg, "firewall_bytes_total", METRIC_TYPE_COUNTER, "Total firewall bytes by chain/rule labels.");
	namespace_metric_family_set(NULL, carg, "firewall_works", METRIC_TYPE_GAUGE, "Firewall operational status marker on host.");
	namespace_metric_family_set(NULL, carg, "firewall_ipset_update_error", METRIC_TYPE_COUNTER, "Total ipset update errors.");
	namespace_metric_family_set(NULL, carg, "firewall_iptables_update_error", METRIC_TYPE_COUNTER, "Total iptables update errors.");
	namespace_metric_family_set(NULL, carg, "firewall_iptables_update_rules", METRIC_TYPE_COUNTER, "Total iptables rules update operations.");
	namespace_metric_family_set(NULL, carg, "nfset_address_total", METRIC_TYPE_GAUGE, "Total addresses in an nftables set.");
	namespace_metric_family_set(NULL, carg, "nfset_ports_total", METRIC_TYPE_GAUGE, "Total ports in an nftables set.");
	namespace_metric_family_set(NULL, carg, "systemd_scopes_count", METRIC_TYPE_GAUGE, "Count of discovered systemd scopes by type.");
	namespace_metric_family_set(NULL, carg, "utmp_logged_in_timestamp_seconds", METRIC_TYPE_GAUGE, "Login timestamp from utmp records in Unix seconds.");
	namespace_metric_family_set(NULL, carg, "system_manufacturer", METRIC_TYPE_GAUGE, "System manufacturer information as labels.");
	namespace_metric_family_set(NULL, carg, "system_product_name", METRIC_TYPE_GAUGE, "System product name information as labels.");
	namespace_metric_family_set(NULL, carg, "system_serial", METRIC_TYPE_GAUGE, "System serial information as labels.");
	namespace_metric_family_set(NULL, carg, "system_version", METRIC_TYPE_GAUGE, "System version information as labels.");
	namespace_metric_family_set(NULL, carg, "baseboard_manufacturer", METRIC_TYPE_GAUGE, "Baseboard manufacturer information as labels.");
	namespace_metric_family_set(NULL, carg, "baseboard_product_name", METRIC_TYPE_GAUGE, "Baseboard product name information as labels.");
	namespace_metric_family_set(NULL, carg, "baseboard_version", METRIC_TYPE_GAUGE, "Baseboard version information as labels.");
	namespace_metric_family_set(NULL, carg, "baseboard_serial", METRIC_TYPE_GAUGE, "Baseboard serial number information as labels.");
	namespace_metric_family_set(NULL, carg, "baseboard_asset_tag", METRIC_TYPE_GAUGE, "Baseboard asset tag information as labels.");
	namespace_metric_family_set(NULL, carg, "baseboard_location", METRIC_TYPE_GAUGE, "Baseboard location information as labels.");
	namespace_metric_family_set(NULL, carg, "bios_vendor", METRIC_TYPE_GAUGE, "BIOS vendor information as labels.");
	namespace_metric_family_set(NULL, carg, "bios_version", METRIC_TYPE_GAUGE, "BIOS version information as labels.");
	namespace_metric_family_set(NULL, carg, "chassis_manufacturer", METRIC_TYPE_GAUGE, "Chassis manufacturer information as labels.");
	namespace_metric_family_set(NULL, carg, "chassis_type", METRIC_TYPE_GAUGE, "Chassis type marker as labels.");
	namespace_metric_family_set(NULL, carg, "chassis_version", METRIC_TYPE_GAUGE, "Chassis version information as labels.");
	namespace_metric_family_set(NULL, carg, "chassis_serial", METRIC_TYPE_GAUGE, "Chassis serial number information as labels.");
	namespace_metric_family_set(NULL, carg, "chassis_asset_tag", METRIC_TYPE_GAUGE, "Chassis asset tag information as labels.");
	namespace_metric_family_set(NULL, carg, "memory_module_size_kb", METRIC_TYPE_GAUGE, "Memory module size in kibibytes by module/device labels.");
	namespace_metric_family_set(NULL, carg, "memory_module_speed", METRIC_TYPE_GAUGE, "Memory module speed by module/device labels.");
	namespace_metric_family_set(NULL, carg, "processor_core_count", METRIC_TYPE_GAUGE, "Processor core count by processor labels.");
	namespace_metric_family_set(NULL, carg, "processor_core_enabled", METRIC_TYPE_GAUGE, "Processor enabled core count by processor labels.");
	namespace_metric_family_set(NULL, carg, "processor_current_speed", METRIC_TYPE_GAUGE, "Processor current speed by processor labels.");
	namespace_metric_family_set(NULL, carg, "processor_external_clock", METRIC_TYPE_GAUGE, "Processor external clock by processor labels.");
	namespace_metric_family_set(NULL, carg, "processor_manufacturer", METRIC_TYPE_GAUGE, "Processor manufacturer information as labels.");
	namespace_metric_family_set(NULL, carg, "processor_max_speed", METRIC_TYPE_GAUGE, "Processor maximum speed by processor labels.");
	namespace_metric_family_set(NULL, carg, "processor_status", METRIC_TYPE_GAUGE, "Processor status marker by processor labels.");
	namespace_metric_family_set(NULL, carg, "processor_thread_count", METRIC_TYPE_GAUGE, "Processor thread count by processor labels.");
	namespace_metric_family_set(NULL, carg, "processor_version", METRIC_TYPE_GAUGE, "Processor version information as labels.");
	namespace_metric_family_set(NULL, carg, "server_platform", METRIC_TYPE_GAUGE, "Detected server platform marker by platform labels.");
	namespace_metric_family_set(NULL, carg, "service_enabled", METRIC_TYPE_GAUGE, "Systemd service enabled state by service labels.");
	namespace_metric_family_set(NULL, carg, "service_running", METRIC_TYPE_GAUGE, "Systemd service running state by service labels.");
	namespace_metric_family_set(NULL, carg, "service_match", METRIC_TYPE_GAUGE, "Systemd service match flag by service labels.");
	namespace_metric_family_set(NULL, carg, "service_tasks_count", METRIC_TYPE_GAUGE, "Systemd service task counters by service and type labels.");
	namespace_metric_family_set(NULL, carg, "socket_counters", METRIC_TYPE_GAUGE, "Socket counters by protocol/state labels.");
	namespace_metric_family_set(NULL, carg, "socket_match", METRIC_TYPE_GAUGE, "Socket match flag by selector labels.");
	namespace_metric_family_set(NULL, carg, "socket_stat", METRIC_TYPE_GAUGE, "Socket statistics by protocol/state labels.");
	namespace_metric_family_set(NULL, carg, "socket_stat_receive_queue_length", METRIC_TYPE_GAUGE, "Socket receive queue current length by socket labels.");
	namespace_metric_family_set(NULL, carg, "socket_stat_receive_queue_limit", METRIC_TYPE_GAUGE, "Socket receive queue limit by socket labels.");

	/* IPMI families. */
	namespace_metric_family_set(NULL, carg, "ipmi_last_power_event", METRIC_TYPE_GAUGE, "IPMI chassis last power event state marker.");
	namespace_metric_family_set(NULL, carg, "ipmi_power_restore_policy", METRIC_TYPE_GAUGE, "IPMI chassis power restore policy marker.");
	namespace_metric_family_set(NULL, carg, "ipmi_dcmi_power_reading_instantaneous", METRIC_TYPE_GAUGE, "IPMI DCMI instantaneous power reading.");
	namespace_metric_family_set(NULL, carg, "ipmi_dcmi_power_reading_minimum", METRIC_TYPE_GAUGE, "IPMI DCMI minimum power reading.");
	namespace_metric_family_set(NULL, carg, "ipmi_dcmi_power_reading_maximum", METRIC_TYPE_GAUGE, "IPMI DCMI maximum power reading.");
	namespace_metric_family_set(NULL, carg, "ipmi_dcmi_power_reading_average_over_sample_period", METRIC_TYPE_GAUGE, "IPMI DCMI average power over sample period.");
	namespace_metric_family_set(NULL, carg, "ipmi_dcmi_power_reading_state", METRIC_TYPE_GAUGE, "IPMI DCMI reading state.");
	namespace_metric_family_set(NULL, carg, "ipmi_lan", METRIC_TYPE_GAUGE, "IPMI LAN status marker.");
	namespace_metric_family_set(NULL, carg, "ipmi_send_command_duration_nanoseconds", METRIC_TYPE_GAUGE, "IPMI command send ioctl duration in nanoseconds.");
	namespace_metric_family_set(NULL, carg, "ipmi_receive_message_duration_nanoseconds", METRIC_TYPE_GAUGE, "IPMI receive message ioctl duration in nanoseconds.");
	namespace_metric_family_set(NULL, carg, "ipmi_system_power", METRIC_TYPE_GAUGE, "IPMI chassis system power state bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_power_overload", METRIC_TYPE_GAUGE, "IPMI chassis power overload status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_power_interlock", METRIC_TYPE_GAUGE, "IPMI chassis power interlock status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_main_power_fault", METRIC_TYPE_GAUGE, "IPMI chassis main power fault status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_power_control_fault", METRIC_TYPE_GAUGE, "IPMI chassis power control fault status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_front_panel_lockout", METRIC_TYPE_GAUGE, "IPMI front panel lockout status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_drive_fault", METRIC_TYPE_GAUGE, "IPMI drive fault status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_chassis_event_ac_failed", METRIC_TYPE_GAUGE, "IPMI chassis event bit: AC failed.");
	namespace_metric_family_set(NULL, carg, "ipmi_chassis_event_fault_main", METRIC_TYPE_GAUGE, "IPMI chassis event bit: main power fault.");
	namespace_metric_family_set(NULL, carg, "ipmi_chassis_event_interlock", METRIC_TYPE_GAUGE, "IPMI chassis event bit: interlock.");
	namespace_metric_family_set(NULL, carg, "ipmi_chassis_event_overload", METRIC_TYPE_GAUGE, "IPMI chassis event bit: overload.");
	namespace_metric_family_set(NULL, carg, "ipmi_chassis_event_power_on_via_ipmi", METRIC_TYPE_GAUGE, "IPMI chassis event bit: power on via IPMI command.");
	namespace_metric_family_set(NULL, carg, "ipmi_chassis_intrusion", METRIC_TYPE_GAUGE, "IPMI chassis intrusion status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_cooling_fan_fault", METRIC_TYPE_GAUGE, "IPMI cooling fan fault status bit.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_status", METRIC_TYPE_GAUGE, "IPMI sensor status by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_stat", METRIC_TYPE_GAUGE, "IPMI sensor numeric value by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_lower_critical", METRIC_TYPE_GAUGE, "IPMI sensor lower critical threshold by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_lower_non_critical", METRIC_TYPE_GAUGE, "IPMI sensor lower non-critical threshold by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_lower_non_recoverable", METRIC_TYPE_GAUGE, "IPMI sensor lower non-recoverable threshold by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_upper_critical", METRIC_TYPE_GAUGE, "IPMI sensor upper critical threshold by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_upper_non_critical", METRIC_TYPE_GAUGE, "IPMI sensor upper non-critical threshold by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_sensor_upper_non_recoverable", METRIC_TYPE_GAUGE, "IPMI sensor upper non-recoverable threshold by sensor labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_eventlog_key", METRIC_TYPE_GAUGE, "IPMI eventlog entry marker by event labels.");
	namespace_metric_family_set(NULL, carg, "ipmi_eventlog_size", METRIC_TYPE_GAUGE, "Number of IPMI eventlog entries.");
}
void ipaddr_info() {
	char addr[512];
	char mask[512];
	char phys[512];
	uv_interface_address_t *info;
	int count, i;
	uint64_t val = 1;

	uv_interface_addresses(&info, &count);
	i = count;

	int64_t count64 = count;
	metric_add_auto("iface_num", &count64, DATATYPE_INT, ac->system_carg);
	while (i--) {
		uv_interface_address_t interface = info[i];

		unsigned char phys_addr[6];
		phys_addr[0] = interface.phys_addr[0];
		phys_addr[1] = interface.phys_addr[1];
		phys_addr[2] = interface.phys_addr[2];
		phys_addr[3] = interface.phys_addr[3];
		phys_addr[4] = interface.phys_addr[4];
		phys_addr[5] = interface.phys_addr[5];

		snprintf(phys, sizeof(phys), "%02X:%02X:%02X:%02X:%02X:%02X", phys_addr[0], phys_addr[1], phys_addr[2], phys_addr[3], phys_addr[4], phys_addr[5]);
		//printf("1 %02X:%02X:%02X:%02X:%02X:%02X\n", phys_addr[0], phys_addr[1], phys_addr[2], phys_addr[3], phys_addr[4], phys_addr[5]);
		//printf("2 %u:%u:%u:%u:%u:%u\n", phys_addr[0], phys_addr[1], phys_addr[2], phys_addr[3], phys_addr[4], phys_addr[5]);

		if (interface.address.address4.sin_family == AF_INET) {
			uv_ip4_name(&interface.address.address4, addr, sizeof(addr));
			uv_ip4_name(&interface.netmask.netmask4, mask, sizeof(mask));
			metric_add_labels5("interface_address", &val, DATATYPE_UINT, ac->system_carg, "version", "IPv4", "address", addr, "phys_addr", phys, "iface", interface.name, "mask", mask);
		}
		else if (interface.address.address4.sin_family == AF_INET6) {
			uv_ip6_name(&interface.address.address6, addr, sizeof(addr));
			uv_ip6_name(&interface.netmask.netmask6, mask, sizeof(mask));
			metric_add_labels5("interface_address", &val, DATATYPE_UINT, ac->system_carg, "version", "IPv6", "address", addr, "phys_addr", phys, "iface", interface.name, "mask", mask);
		}
	}

	uv_free_interface_addresses(info, count);

}

void hw_cpu_info()
{
	int count;
	uv_cpu_info_t *cinfo;
	uv_cpu_info(&cinfo, &count);
	uint64_t val = 1;
	int i = count;
	while (i--)
	{
		char socket_num[3];
		snprintf(socket_num, sizeof(socket_num), "%d", i);
		metric_add_labels2("cpu_model", &val, DATATYPE_UINT, ac->system_carg, "model", cinfo[i].model, "socket", socket_num);
	}
	uv_free_cpu_info(cinfo, count);
}

void get_utsname()
{
	uv_utsname_t buf;
	uv_os_uname(&buf);
	uint64_t val = 1;
	//printf("sysname: %s, relese: %s, version: %s, machine: %s\n", buf.sysname, buf.release, buf.version, buf.machine);
	metric_add_labels("machine_arch", &val, DATATYPE_UINT, ac->system_carg, "arch", buf.machine);
}

void system_initialize()
{
	extern aconf *ac;
	ac->system_base = 0;
	ac->system_interrupts = 0;
	ac->system_network = 0;
	ac->system_disk = 0;
	ac->system_process = 0;
	ac->system_cadvisor = 0;
	ac->system_services_process = 0;
	ac->system_smart = 0;
	ac->system_carg = calloc(1, sizeof(*ac->system_carg));
	ac->system_carg->ttl = 300;
	ac->system_carg->curr_ttl = 0;
	ac->scs = calloc(1, sizeof(system_cpu_stats));
	ac->system_sysfs = strdup("/sys/");
	ac->system_procfs = strdup("/proc/");
	ac->system_rundir = strdup("/run/");
	ac->system_usrdir = strdup("/usr/");
	ac->system_etcdir = strdup("/etc/");
	ac->system_pidfile = calloc(1, sizeof(*ac->system_pidfile));

	ac->process_match = calloc(1, sizeof(match_rules));
	ac->process_match->hash = alligator_ht_init(NULL);

	ac->packages_match = calloc(1, sizeof(match_rules));
	ac->packages_match->hash = alligator_ht_init(NULL);

	ac->services_match = calloc(1, sizeof(match_rules));
	ac->services_match->hash = alligator_ht_init(NULL);

	ac->services_process_match = calloc(1, sizeof(match_rules));
	ac->services_process_match->hash = alligator_ht_init(NULL);

	ac->system_userprocess = alligator_ht_init(NULL);
	ac->system_groupprocess = alligator_ht_init(NULL);
	ac->system_services_checking_users = alligator_ht_init(NULL);

	ac->system_sysctl = alligator_ht_init(NULL);
	system_register_metric_families(ac->system_carg);
}


int is_container(int8_t platform) {
	return (!platform) || (platform > 4) ? 0 : 1;
}