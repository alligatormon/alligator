# NTP

Alligator includes an implementation of the NTP protocol, providing the ability to detect time discrepancies between the current node and a specified NTP server.

Here is an example of configuration:
```
aggregate {
    ntp udp://time.google.com:123;
}
```
