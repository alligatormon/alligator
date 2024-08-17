```
system {
	base; #cpu, memory, load avg, openfiles
	disk; #disk usage and I/O
	network; #network interface start
	process [nginx] [bash] [/[bash]*/]; #scrape VSZ, RSS, CPU, Disk I/O usage from processes
	smart; #smart disk stats
	firewall [ipset=[entries|on]];
	cpuavg period=5; # analog loadavg by cpu usage stat only with period in minutes
	packages [nginx] [alligator]; # scrape packages info with timestamp installed
	cadvisor [docker=http://unix:/var/run/docker.sock:/containers/json] [log_level=info] [add_labels=collector:cadvisor]; # for scrape cadvisor metrics
	services [nginx.service]; # for systemd unit status

	pidfile [tests/mock/linux/svc.pid]; # monitoring process by pidfile
	userprocess [root]; # monitoring process by username
	groupprocess [root]; # monitoring process by groupname
	cgroup [/cpu/]; # monitoring process by cgroup

	sysfs  /path/to/dir; # override path
	procfs /path/to/dir; # override path
	rundir /path/to/dir; # override path
	usrdir /path/to/dir; # override path
	etcdir /path/to/dir; # override path
	log_level off;
}
```
