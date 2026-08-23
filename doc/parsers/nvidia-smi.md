**Language / Язык:** [English](nvidia-smi.md) | [Русский](../ru/parsers/nvidia-smi.md)

## NVIDIA GPU metrics

Collects GPU statistics by running `nvidia-smi` with a fixed CSV query and parsing the result.

### Connection URL

```
exec://nvidia-smi
exec:///full/path/to/nvidia-smi
```

Handler name is **`nvidia_smi`**. Alligator appends a long `--query-gpu=… --format=csv` argument list automatically; you only need the binary path.

### Example

```
aggregate {
    nvidia_smi exec://nvidia-smi;
}
```

If the binary is not on `$PATH`:

```
aggregate {
    nvidia_smi exec:///usr/bin/nvidia-smi;
}
```

### Requirements

- NVIDIA driver and `nvidia-smi` installed
- Permission to query GPUs (usually any user can read local SMI)

### Metrics

CSV column names become `nvidia_smi_<column>` (Prometheus-normalized). Memory values in KiB/MiB/GiB are converted to bytes (`*_bytes`); percents, watts, and MHz get `_percent` / `_watt` / `_mhz` suffixes.

Typical metrics:

- `nvidia_smi_count` — GPU count
- `nvidia_smi_memory_{total,used,free}_bytes`
- `nvidia_smi_utilization_{gpu,memory}_percent`
- `nvidia_smi_temperature_{gpu,memory}`
- `nvidia_smi_power_draw_watt`, `nvidia_smi_fan_speed_percent`
- `nvidia_smi_clocks_current_*_mhz`
- `nvidia_smi_driver_version{version=…}`, `nvidia_smi_pstate{pstate=…}`

Labels on per-GPU gauges: `name`, `uuid`, `serial`, `index`.
