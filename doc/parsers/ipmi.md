## IPMI

To enable the collection of statistics from ipmi, use the following option:
```
aggregate {
    ipmi exec:///usr/bin/ipmitool;
}
```
ipmitool should be installed on the server
