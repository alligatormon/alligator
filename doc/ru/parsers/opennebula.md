**Language / Язык:** [English](../../parsers/opennebula.md) | [Русский](opennebula.md)

## OpenNebula

Собирает метрики OpenNebula через XML-RPC (`/RPC2`) так же, как [nebula_exporter](https://github.com/Haameed/nebula_exporter): хосты, VM, виртуальные сети и datastores.

Имя handler'а — **`opennebula`**. На каждый scrape Alligator делает четыре POST:

| Handler key | XML-RPC метод | Пул |
|-------------|----------------|------|
| `opennebula_hosts` | `one.hostpool.info` | `HOST_POOL` |
| `opennebula_vms` | `one.vmpool.info` | `VM_POOL` |
| `opennebula_vnets` | `one.vnpool.info` | `VNET_POOL` |
| `opennebula_datastores` | `one.datastorepool.info` | `DATASTORE_POOL` |

Сессия XML-RPC — `user:password` из URL (первый параметр). HTTP Basic тоже отправляется, если в URL есть credentials.

Путь по умолчанию — `/RPC2`, если в URL нет path. Порт XML-RPC по умолчанию — **2633**.

`file://` с дампом XML пула (или полного `methodResponse`) разбирается без HTTP-запроса.

### Пример

```
aggregate {
    opennebula https://oneadmin:secret@opennebula:2633;
}
```

### Metrics

Хост (`hostname`, `cluster`):

- `opennebula_monitoring_host_status` — 0=INIT, 1=MONITORING_MONITORED, 2=MONITORED, 3=ERROR, 4=DISABLED
- `opennebula_monitoring_cpu_total` / `cpu_max` / `cpu_used` / `host_free_cpu` — единицы CPU (100 на ядро)
- `opennebula_monitoring_memory_total` / `memory_allocated` / `memory_max`

VM (лейбл `hostname` — имя VM, как в nebula_exporter):

- `opennebula_monitoring_vm_status`
- `opennebula_monitoring_vm_cpu`
- `opennebula_monitoring_vm_memory`

Это **выделенные** значения из шаблона VM, а не живое потребление гостя. CPU, память, диск и сеть работающих KVM/QEMU (и LXC) гостей собирает [cAdvisor](../system.md#cadvisor) Alligator на каждом гипервизоре OpenNebula.

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
    opennebula https://oneadmin:secret@opennebula:2633;
}
```

cAdvisor отдаёт серии `container_*` (CPU, RSS, filesystem, сеть) с лейблом `libvirt_id`. KVM-домены OpenNebula обычно называются `one-<id>`; стыкуйте это с `ID` VM из XML-RPC (лейбл `hostname` парсера — это **NAME** OpenNebula). LXC-гости на том же хосте собираются без libvirt.

Опции (`docker`, `add_label`, `log_level`) и Grafana dashboard — в [cAdvisor](../system.md#cadvisor).

Виртуальная сеть (`vnet`):

- `opennebula_monitoring_vnet_total` — сумма `SIZE` address range
- `opennebula_monitoring_vnet_leases` — `USED_LEASES`

Datastore (лейбл `hostname` — имя datastore):

- `opennebula_monitoring_datastore_status` — 0=ON, 1=OFF
- `opennebula_monitoring_datastore_total` / `datastore_free` / `datastore_used` — МБ

Свободный CPU берётся из `HOST_SHARE/FREE_CPU` в `one.hostpool.info` (exporter дополнительно вызывает `one.hostpool.monitoring` и per-host `info`).

Юнит-тесты: [`src/tests/unit2/parsers.h`](../../../src/tests/unit2/parsers.h) (`api_test_parser_opennebula`).
