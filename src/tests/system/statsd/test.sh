[ -z $1 ] && exit 1
[ -z $2 ] && exit 1
[ -z $4 ] && SCHEME="http" || SCHEME="$4"

HOST="$1"
PORT="$2"
TCPPORT="$3"
UDPPORT="$5"
CURL_CMD="curl -sk $SCHEME://$HOST:$PORT"

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

curl_test()
{
	text=`$CURL_CMD`
	echo $text | grep "$1" && success $1 || error "$text" "$1"
}

curl_no_test()
{
	text=`$CURL_CMD`
	echo $text | grep "$1" && error_no $1 || success "no metric $1"
}

statsd_send()
{
	printf "$1" | nc $HOST $TCPPORT
}

statsd_udp_send()
{
	printf "$1" | nc -u $HOST $UDPPORT
}

pushgateway_send()
{
	curl -sk -d "$1" $SCHEME://$HOST:$PORT/metrics$2
}

# Test posting statsd metric via |g
statsd_send 'test_metric.engine_array_list_object:9|g|#id:34'
curl_test 'test_metric_engine_array_list_object {id="34"} 9.000000'

# Test counting statsd metric via |c
statsd_send 'test_metric.engine_array_list_object:11|c|#id:34'
curl_test 'test_metric_engine_array_list_object {id="34"} 20.000000'

# Metric with incomplete content should not be published
pushgateway_send 'ndpoint="/api/efcwefcerwv/rvegr/vvrt/vtrvle", method="POST"} 116.000000' ''
curl_no_test 'ndpoint'

# Test statsd dogstatsd labels format
statsd_send 'project.api.response_code:1|c|#status_code:200,endpoint:/api/healthcheck,method:GET'
curl_test 'project_api_response_code {status_code="200", endpoint="/api/healthcheck", method="GET"} 1.000000'

# Label should be starts with ASCII letters or _ or :, not numeric https://prometheus.io/docs/concepts/data_model/
statsd_send 'select100.api.response_code:1|c|#status_code:200,endpoint:/api/report/v1.0/api_geo?geo_ids=1,method:GET,2=3'
curl_no_test 'select100_api_response_code {2="3", status_code="200", endpoint="/api/report/v1.0/api_geo?geo_ids=1", method="GET"} 1.000000'

statsd_send 'project.api.response_time:655.000000|ms|#endpoint:/api/sql?response_format=JSON,method:POST'
curl_test 'project_api_response_time {endpoint="/api/sql?response_format=JSON", method="POST"} 655.000000'

pushgateway_send 'test_metric_pushgateway{le="900"} 23472378' '/job/kernel/kernel/version/'
curl_test 'test_metric_pushgateway {job="kernel", kernel="version", le="900"} 23472378.000000'

pushgateway_send 'test_metric_pushgateway 1132' '/job/kernel/kernel/version/'
curl_test 'test_metric_pushgateway {job="kernel", kernel="version"} 1132.000000'

pushgateway_send 'test_metric_pushgateway{bucket="500", service="app", env="prod"} 231' ''
curl_test 'test_metric_pushgateway {bucket="500", env="prod", service="app"} 231.000000'

pushgateway_send 'admin_templates.templates_list", method="GET"} 0.006529' ''
curl_no_test 'admin_templates'

pushgateway_send 'project_hapi_response_time{code="200", 2="3"} 3' ''
curl_no_test 'project_hapi_response_time {code="200"} 3.000000'

pushgateway_send 'fvorvno_efvinrf_efivni_199 {theme_id="1028", name="Elektrotehnicheskaja", strojmaterialov="eskaja", promyshlennost'\''="Proizvodstvo"} 101.000000' ''
curl_no_test 'fvorvno_efvinrf_efivni_199'

# Testing UDP statsd transport with mapping labels and metric name.
statsd_udp_send "test3.tata.test4.papa:$RANDOM|ms"
curl_test 'test3_test4 {label_name_test3="test4_key"}'

# Send multiple queries for calculating quantiles. Calculation has complex solutions, because of this tests only prefix of answer numeric on unstable quantiles (0.9, 0.99).
DATAS="13414 11737 9794 2743 10785 8717 9035 21296 17588 15529 7138 14999 25624 15637 25784 17479 12492 24182 32526 32531 5400 2690 9071 8916 21445 12141"
for DATA in $DATAS
do
	statsd_udp_send "test3.tata.test4.papa:$DATA|ms"
done
curl_test 'test3_test4_quantile {label_name_test3="test4_key", quantile="0.75"} 2743'
curl_test 'test3_test4_quantile {label_name_test3="test4_key", quantile="0.9"} 15'
curl_test 'test3_test4_quantile {label_name_test3="test4_key", quantile="0.99"} 32'

#statsd_udp_send "requests:1|c|#servername:help.example.ru,zone:help,code:200"
#curl_test 'requests"

