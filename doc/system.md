# System block

## Overview
Alligator supports system-wide metrics regarding OS, hardware and virtual machines.

There is quick overview of all options in this context:
```
system {
    base;
    disk;
    network;
    process [nginx] [bash] [/[bash]*/];
    services [nginx.service];
    smart;
    firewall [ipset=[entries|on]];
    cpuavg period=5;
    packages [nginx] [alligator];
    cadvisor [option1] [option2] .. [optionN];

    pidfile /path/to/pidfile1 [/path/to/pidfile2] ... [/path/to/pidfileN];
    userprocess user1 [user2] ... [userN];
    groupprocess user1 [group2] ... [groupN];
    cgroup /cgroup1/ /[cgroup2]/ ... /[cgroupN]/;

    sysfs  /path/to/dir;
    procfs /path/to/dir;
    rundir /path/to/dir;
    usrdir /path/to/dir;
    etcdir /path/to/dir;
    log_level off;
}
```

## base
Enables monitoring of the base OS and hardware resources, including CPU, memory, and temperatures of the baseboard and components. OS resources such as loadavg, openfiles, interrupts, and context switches are also included.


## disk
Enables monitoring disk metrics, including the filesystem stats and I/O block devices stats.


## network
Enables the monitoring of the network interfaces and sockets statistics.


## process
Specifies a list of processes to check for running and to collect resources usage data in ulimits terms.\
If processes aren't specified, Alligator will collect ulimits usage for all processes.\
This includes metrics like memory usage, CPU usage, disk I/O, open files, threads, and vmaps.


## services
Similar to process, but for services (on Linux systems systemd services).\
Checks whether the service is running and enabled, and obtains the process statistics associated with this service.


## smart
Enables the collection of S.M.A.R.T. metrics.


## firewall
Enables the retrieval of counters from the firewall.
The extra parameter 'ipset' allows scraping the list of sets. If the 'entries' option is specified, Alligator will scrape the entries within the ipset.


## cpuavg
Creates a loadavg analog in Linux based only on CPU usage.\
It has one option 'period' which specifies the averaging period in minutes.\
For example, the collected metric looks like this:
```
cpu_avg 4.326667
```


## packages
Collects information about packages installed in the OS by the system installer and their installation date.\
It can specify a list of packages. Otherwise, it collect information from all packages in the system.
Here's an example of a collected metric:
```
package_installed {version="6.40", name="nmap-ncat", release="7.el7"} 1527797852
```


## cadvisor
Implements metrics from the well-known exporter called CAdvisor.\

Example of use in the configuration file:
```
system {
    cadvisor [docker=http://unix:/var/run/docker.sock:/containers/json] [log_level=info] [add_labels=collector:cadvisor];
}
```

### log\_level
Specify of the level of logging for this context. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

### add\_label
This option provides opportunity to manipulate extra labels.

For example, this configuration adds two extra labels for each metric:
```
system {
    cadvisor add_label=label1:value1 add_label=label2:value2;
}
```

### docker
Specifies the socket of the docker daemon. The default is `http://unix:/var/run/docker.sock:/containers/json`.


## pidfile, userprocess, groupprocess, cgroup
Specifies the checking of processes by pidfile, user, group, or cgroup.

Example of use in the configuration file:
```
system {
    pidfile /var/run/nginx.pid;
    userprocess nginx;
    groupprocess nobody;
    cgroup /cpu/;
}
```

## sysfs, procfs, rundir, usrdir, etcdir
Allows the redefinition of the default control directories of the OS, useful for testing and custom-built systems.


## log_level
Default: off\
Plural: no\
Specify of the level of logging for this context. Units for this option are explained in this [document](https://github.com/alligatormon/alligator/blob/master/doc/configuration.md#available-log-levels)

```
system {
    base;
    log_level debug;
}
```


# Dashboard
The system dashboard for Grafana + Prometheus is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-system.json)
<h1 align="center" style="border-bottom: none">
    <img width="100" height="100" alt="Alligator system dashboard" src="/doc/images/dashboard-system.jpg"></a><br>Alligator
</h1>

Additionally, the firewall dashboard is available at the following [link](https://github.com/alligatormon/alligator/tree/master/dashboards/alligator-firewall.json)
