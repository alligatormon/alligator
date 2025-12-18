# Wazuh

Wazuh can be monitored using statistics state-files.

In Alligator, the stats are collected using the following configuration:
```
aggregate {
    wazuh file:///var/ossec/var/run/wazuh-agentd.state;
}
```
