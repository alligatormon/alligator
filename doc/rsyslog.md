
## Monitoring rsyslog impstats:
```
entrypoint {
	handler rsyslog-impstats;
	udp 127.0.0.1:1111;
}
```

rsyslog.conf:
```
module(
	load="impstats"
	interval="10"
	resetCounters="off"
	log.file="off
	log.syslog="on"
	ruleset="rs_impstats"
)

template(name="impformat" type="list") {
        property(outname="message" name="msg")
}

ruleset(name="rs_impstats" queue.type="LinkedList" queue.filename="qimp" queue.size="5000" queue.saveonshutdown="off") {
        *.* action (
                type="omfwd"
                target="127.0.0.1"
                port="1111"
                protocol="udp"
                Template="impformat"
        )
}
```
