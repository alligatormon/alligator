[ -z $1 ] && exit 1
[ -z $2 ] && exit 1
[ -z $3 ] && exit 1

HOST="$1"
PORT="$2"
PORT10000="$3"
IFS=""

error()
{
	echo -e "\033[31mNo $2 in: \e[0m"
	echo "$1"
}

error_no()
{
	echo -e "\033[31mExists metric $1 \e[0m"
}

success()
{
	echo -e "\e[32mSuccess: $1 \e[0m"
}

check_expire_time()
{
	echo "test_metric_$1 1" | curl -H "X-Expire-Time: $1" --data-binary @- $HOST:$PORT
	EXPIRE_TIME=$((`date +"%s"` + $1))
	text=`curl -s $HOST:$PORT/stats`
	echo $text | grep "test_metric_$1" | grep $EXPIRE_TIME > /dev/null && success $1 || error $text "not found metric with name: test_metric_$1 and expire: $EXPIRE_TIME:"
}

check_expire_sleep()
{
	echo "test_metric_sleep_$1 1" | curl -H "X-Expire-Time: $1" --data-binary @- $HOST:$PORT
	text=`curl -s $HOST:$PORT/metrics`
	echo $text | grep "test_metric_sleep_$1" > /dev/null && success $1 || error $text "not found metric with name: test_metric_sleep_$1"
	sleep $(($1+11))
	text=`curl -s $HOST:$PORT/metrics`
	echo $text | grep "test_metric_sleep_$1" > /dev/null && error $text "found metric after sleep with name test_metric_sleep_$1" || success $1
}

check_expire_time10000()
{
	echo "test_metric_10k 1" | curl --data-binary @- $HOST:$PORT10000
	EXPIRE_TIME=$((`date +"%s"` + 10000))
	text=`curl -s $HOST:$PORT10000/stats`
	echo $text | grep "test_metric_10k" | grep $EXPIRE_TIME >/dev/null && success 10k || error $text "not found metric with name: test_metric_10k and expire:$EXPIRE_TIME:"
}

check_expire_time10000
check_expire_sleep 10

check_expire_time 212
check_expire_time 2120
check_expire_time 1000
