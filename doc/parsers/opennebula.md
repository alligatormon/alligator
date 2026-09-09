**Language / Язык:** [English](opennebula.md) | [Русский](../ru/parsers/opennebula.md)

## OpenNebula

Scrapes the OpenNebula XML-RPC API (`/RPC2`) the same way as [nebula_exporter](https://github.com/Haameed/nebula_exporter): hosts, VMs, virtual networks, and datastores.

Handler name is **`opennebula`**. Alligator issues four POSTs per scrape:

| Handler key | XML-RPC method | Pool |
|-------------|----------------|------|
| `opennebula_hosts` | `one.hostpool.info` | `HOST_POOL` |
| `opennebula_vms` | `one.vmpool.info` | `VM_POOL` |
| `opennebula_vnets` | `one.vnpool.info` | `VNET_POOL` |
| `opennebula_datastores` | `one.datastorepool.info` | `DATASTORE_POOL` |

Session string is `user:password` from the URL (first XML-RPC parameter). HTTP Basic is also sent when the URL has credentials.

Default path is `/RPC2` when the URL has no path. Default XML-RPC port is **2633**.

`file://` dumps of a pool XML (or a full `methodResponse`) can be parsed without generating an HTTP request.

### Example

```
aggregate {
    opennebula https://oneadmin:secret@opennebula:2633;
}
```

### Metrics

Host (`hostname`, `cluster`):

- `opennebula_monitoring_host_status` — 0=INIT, 1=MONITORING_MONITORED, 2=MONITORED, 3=ERROR, 4=DISABLED
- `opennebula_monitoring_cpu_total` / `cpu_max` / `cpu_used` / `host_free_cpu` — CPU units (100 per core)
- `opennebula_monitoring_memory_total` / `memory_allocated` / `memory_max`

VM (label `hostname` is the VM name, matching nebula_exporter):

- `opennebula_monitoring_vm_status`
- `opennebula_monitoring_vm_cpu`
- `opennebula_monitoring_vm_memory`

These are **allocated** values from the VM template, not live guest usage. For CPU, memory, disk, and NIC stats of running KVM/QEMU (and LXC) guests, run Alligator [cAdvisor](../system.md#cadvisor) on each OpenNebula hypervisor.

### VM resources (cAdvisor)

On the host that runs the VMs, enable cAdvisor and load libvirt (same `modules` pattern as `rpmlib` / `nvml`):

```
modules {
    libvirt /usr/lib64/libvirt.so.0;
}

system {
    cadvisor;
}

aggregate {
    opennebula https://oneadmin:secret@opennebula:2633;
}
```

cAdvisor then exports `container_*` series (CPU, RSS, filesystem, network) with a `libvirt_id` label. OpenNebula KVM domains are usually named `one-<id>`; join that to the VM `ID` from XML-RPC (the parser’s `hostname` label is the OpenNebula **NAME**). LXC guests on the same host are scraped without libvirt.

See [cAdvisor](../system.md#cadvisor) for options (`docker`, `add_label`, `log_level`) and the Grafana dashboard.

Virtual network (`vnet`):

- `opennebula_monitoring_vnet_total` — sum of address-range `SIZE`
- `opennebula_monitoring_vnet_leases` — `USED_LEASES`

Datastore (label `hostname` is the datastore name):

- `opennebula_monitoring_datastore_status` — 0=ON, 1=OFF
- `opennebula_monitoring_datastore_total` / `datastore_free` / `datastore_used` — MB

Free CPU is taken from `HOST_SHARE/FREE_CPU` in `one.hostpool.info` (the exporter additionally calls `one.hostpool.monitoring` and per-host `info`).

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_opennebula`).
