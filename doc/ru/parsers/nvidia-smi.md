**Language / Язык:** [English](../../parsers/nvidia-smi.md) | [Русский](nvidia-smi.md)

## NVIDIA GPU metrics

Собирает статистику GPU запуском `nvidia-smi` с фиксированным CSV query и разбором результата.

### Connection URL

```
exec://nvidia-smi
exec:///full/path/to/nvidia-smi
```

Имя handler — **`nvidia_smi`**. Alligator сам добавляет длинный список `--query-gpu=… --format=csv`; нужен только путь к бинарнику.

### Пример

```
aggregate {
    nvidia_smi exec://nvidia-smi;
}
```

Если бинарник не в `$PATH`:

```
aggregate {
    nvidia_smi exec:///usr/bin/nvidia-smi;
}
```

### Requirements

- Установленный NVIDIA driver и `nvidia-smi`
- Права на query GPU (обычно достаточно локального пользователя)

### Metrics

Имена колонок CSV становятся `nvidia_smi_<column>` (нормализация Prometheus). Память в KiB/MiB/GiB переводится в bytes (`*_bytes`); проценты, ватты и MHz получают суффиксы `_percent` / `_watt` / `_mhz`.

Типичные метрики:

- `nvidia_smi_count` — число GPU
- `nvidia_smi_memory_{total,used,free}_bytes`
- `nvidia_smi_utilization_{gpu,memory}_percent`
- `nvidia_smi_temperature_{gpu,memory}`
- `nvidia_smi_power_draw_watt`, `nvidia_smi_fan_speed_percent`
- `nvidia_smi_clocks_current_*_mhz`
- `nvidia_smi_driver_version{version=…}`, `nvidia_smi_pstate{pstate=…}`

Метки на per-GPU gauges: `name`, `uuid`, `serial`, `index`.
