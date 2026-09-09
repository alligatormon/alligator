**Language / Язык:** [English](openstack.md) | [Русский](../ru/parsers/openstack.md)

## OpenStack

Scrapes OpenStack APIs the same way as [prometheus/openstack-exporter](https://github.com/openstack-exporter/openstack-exporter): Keystone v3 token, then GETs from the service catalog for identity, Nova, Neutron, Cinder, and Glance.

Handler name is **`openstack`**.

Credentials in the URL (`user:password`) go into the Keystone JSON body. HTTP Basic is **not** sent. Default user/project domain is `Default`, project defaults to the username, interface is `public`.

### Connection URL

```
http://admin:secret@keystone:5000
https://admin:secret@keystone:5000/v3
```

Default path is `/v3/auth/tokens`. If the URL already has a path, Alligator appends `/auth/tokens`.

### Example

```
aggregate {
    openstack http://admin:secret@keystone:5000
        project=admin user_domain=Default project_domain=Default
        region=RegionOne endpoint_type=public;
}
```

Equivalent OpenStack env vars on the same stanza also work: `OS_PROJECT_NAME`, `OS_USER_DOMAIN_NAME`, `OS_PROJECT_DOMAIN_NAME`, `OS_REGION_NAME`, `OS_INTERFACE` / `OS_ENDPOINT_TYPE`.

### Metrics

Names match `openstack-exporter` (`openstack_<service>_<metric>`). Core (non-slow) coverage:

Identity:

- `openstack_identity_up`
- `openstack_identity_domains` / `openstack_identity_domain_info{id,name,enabled,description}`
- `openstack_identity_users` / `openstack_identity_groups` / `openstack_identity_projects` / `openstack_identity_project_info{...}`
- `openstack_identity_regions`
- `openstack_identity_catalog_services`

Nova:

- `openstack_nova_up`
- `openstack_nova_flavors` / `openstack_nova_flavor{id,name,vcpus,ram,disk,is_public}`
- `openstack_nova_availability_zones` / `openstack_nova_security_groups`
- `openstack_nova_agent_state{id,hostname,service,adminState,zone}`
- `openstack_nova_vcpus_available` / `vcpus_used` / `memory_*_bytes` / `local_storage_*_bytes` / `free_disk_bytes` / `current_workload` (`hostname`)
- `openstack_nova_total_vms` / `openstack_nova_running_vms` / `openstack_nova_server_status{...}`

These are **API inventory** from Nova (status, flavor, hypervisor gauges), not live guest usage. For CPU, memory, disk, and NIC stats of running KVM/QEMU (and LXC) guests, run Alligator [cAdvisor](../system.md#cadvisor) on each compute hypervisor.

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
    openstack http://admin:secret@keystone:5000
        project=admin user_domain=Default project_domain=Default
        region=RegionOne endpoint_type=public;
}
```

cAdvisor then exports `container_*` series (CPU, RSS, filesystem, network) with a `libvirt_id` label. OpenStack KVM domains are usually named `instance-<id>` (`OS-EXT-SRV-ATTR:instance_name`); join that to `openstack_nova_server_status` label `instance_libvirt`. The parser’s `hypervisor_hostname` label is the compute host. LXC guests on the same host are scraped without libvirt.

See [cAdvisor](../system.md#cadvisor) for options (`docker`, `add_label`, `log_level`) and the Grafana dashboard.

Neutron:

- `openstack_neutron_up`
- `openstack_neutron_networks` / `openstack_neutron_network{...}`
- `openstack_neutron_subnets` / `openstack_neutron_subnet{...}`
- `openstack_neutron_ports` / `openstack_neutron_port{...}` / `ports_no_ips` / `ports_lb_not_active`
- `openstack_neutron_floating_ips` / `openstack_neutron_floating_ip{...}`
- `openstack_neutron_routers` / `openstack_neutron_router{...}` / `routers_not_active`
- `openstack_neutron_security_groups` / `openstack_neutron_agent_state{...}`

Cinder:

- `openstack_cinder_up`
- `openstack_cinder_volumes` / `volume_gb` / `volume_status` / `volume_status_counter{status}`
- `openstack_cinder_snapshots` / `snapshot_gb`
- `openstack_cinder_backups` / `backup_gb`
- `openstack_cinder_agent_state{uuid,hostname,service,adminState,zone}`
- `openstack_cinder_pool_capacity_free_gb` / `pool_capacity_total_gb`

Glance:

- `openstack_glance_up`
- `openstack_glance_images` / `openstack_glance_image_bytes` / `openstack_glance_image_created_at`

Quota/limit/VPN/IP-availability extra calls from the exporter are not scraped.

Unit tests: [`src/tests/unit2/parsers.h`](../../src/tests/unit2/parsers.h) (`api_test_parser_openstack`).
