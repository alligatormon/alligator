## NVIDIA GPU metrics

Alligator allows the collection of metrics from the `nvidia-smi` tool. To enable this, use the following option:
```
aggregate {
    nvidia_smi exec://nvidia-smi;
}
```

If the `nvidia-smi` binary is not located in the standard $PATH directory, use the full path. For example:
```
aggregate {
    nvidia_smi exec:///opt/nvidia/bin/nvidia-smi;
}
```
