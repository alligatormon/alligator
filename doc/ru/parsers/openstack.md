**Language / Язык:** [English](../../parsers/openstack.md) | [Русский](openstack.md)

## OpenStack

Собирает метрики OpenStack так же, как [prometheus/openstack-exporter](https://github.com/openstack-exporter/openstack-exporter): токен Keystone v3, затем GET по каталогу сервисов (identity, Nova, Neutron, Cinder, Glance).

Имя handler'а — **`openstack`**.

Логин/пароль из URL (`user:password`) идут в JSON-тело Keystone. HTTP Basic **не** отправляется. Домены user/project по умолчанию — `Default`, project — имя пользователя, interface — `public`.

### Connection URL

```
http://admin:secret@keystone:5000
https://admin:secret@keystone:5000/v3
```

Путь по умолчанию — `/v3/auth/tokens`. Если в URL уже есть path, Alligator дописывает `/auth/tokens`.

### Пример

```
aggregate {
    openstack http://admin:secret@keystone:5000
        project=admin user_domain=Default project_domain=Default
        region=RegionOne endpoint_type=public;
}
```

На той же строке работают и переменные OpenStack: `OS_PROJECT_NAME`, `OS_USER_DOMAIN_NAME`, `OS_PROJECT_DOMAIN_NAME`, `OS_REGION_NAME`, `OS_INTERFACE` / `OS_ENDPOINT_TYPE`.

### Metrics

Имена как в `openstack-exporter` (`openstack_<service>_<metric>`). Базовый набор (без slow-метрик):

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

Это **инвентарь API** Nova (статус, flavor, gauge гипервизора), а не живое потребление гостя. CPU, память, диск и сеть работающих KVM/QEMU (и LXC) гостей собирает [cAdvisor](../system.md#cadvisor) Alligator на каждом compute-гипервизоре.

### Ресурсы VM (cAdvisor)

На хосте, где крутятся VM, включите cAdvisor и загрузите libvirt (тот же `modules`, что у `rpmlib` / `nvml`):

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

cAdvisor отдаёт серии `container_*` (CPU, RSS, filesystem, сеть) с лейблом `libvirt_id`. KVM-домены OpenStack обычно называются `instance-<id>` (`OS-EXT-SRV-ATTR:instance_name`); стыкуйте это с лейблом `instance_libvirt` у `openstack_nova_server_status`. Лейбл `hypervisor_hostname` парсера — compute-хост. LXC-гости на том же хосте собираются без libvirt.

Опции (`docker`, `add_label`, `log_level`) и Grafana dashboard — в [cAdvisor](../system.md#cadvisor).

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

Дополнительные вызовы exporter'а (квоты, limits, VPN, IP availability) не собираются.

Unit-тесты: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_openstack`).
