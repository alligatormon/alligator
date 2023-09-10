#!/bin/sh
printf "#!/bin/sh\ncat tests/mock/linux/iptables\n" > /usr/sbin/iptables
chmod +x /usr/sbin/iptables
