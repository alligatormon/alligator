## NVIDIA GPU metrics

Alligator allows to collect metrics from `nvidia-smi` tool. To enable this you can use such option:
```
aggregator {
    nvidia_smi exec://nvidia-smi;
}
```

If the path of `nvidia-smi` binary not in standart PATH directory to run, use the fullpath. For example:
```
aggregator {
    nvidia_smi exec:///opt/nvidia/bin/nvidia-smi;
}
```
