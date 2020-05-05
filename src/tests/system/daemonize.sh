action=$1
shift
name=$1
shift
if [ "$action" == "start" ]
then
	nohup $* &
	echo $! > /var/run/$name
elif [ "$action" == "stop" ]
then
	kill $(cat /var/run/$name)
	rm -f /var/run/$name
elif [ "$action" == "status" ]
then
	ls /var/run/$name >/dev/null 2>&1 && printf "Process $name running\n" || printf "Process $name not running\n"
else
	printf "Try ./daemonize.sh <start|stop|status> <servicename> <CMD>\n"
fi
