cmake_minimum_required(VERSION 3.8)

#set(VERSION 0.14.61)
set(VERSION 1.14.80)
project(alligator VERSION ${VERSION})

#set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
#set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_BUILD_TYPE Debug)
set(COMMON_TESTS lib${PROJECT_NAME}_tests)
set(COMMON lib${PROJECT_NAME})
set(TESTS ${PROJECT_NAME}_tests)
set(HEAD ${PROJECT_NAME})
set(CLIENT ${PROJECT_NAME}_client)
#set(JP ${PROJECT_NAME}_jp)
file(STRINGS /etc/os-release distro REGEX "^ID=")
string(REGEX REPLACE "ID=\"(.*)\"" "\\1" distro "${distro}")
file(STRINGS /etc/os-release disversion REGEX "^VERSION_ID=")
string(REGEX REPLACE "VERSION_ID=\"(.*)\"" "\\1" disversion "${disversion}")
message("found ${distro}.${disversion}")

# for speed check add -H -ftime-report
IF(${distro}.${disversion} MATCHES "centos.7")
#set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -fstack-protector-all -Wfloat-equal -Wundef -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes -Waggregate-return -Wcast-qual -Wswitch-default -Wswitch-enum -Wconversion -Wunreachable-code -O3 -std=gnu11 -pthread")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wno-undef -Wunreachable-code -O3 -std=gnu11 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "ubuntu.16.04")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wundef -Wunreachable-code -O3 -std=gnu11 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "ubuntu.14.04")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wundef -Wunreachable-code -O3 -std=gnu11 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "ubuntu.12.04")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wundef -Wunreachable-code -O3 -std=gnu11 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "debian.9")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wundef -Wunreachable-code -O3 -std=gnu11 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "freebsd.11")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -O3 -std=gnu11 -pthread -Wno-unused-result -Wno-strlcpy-strlcat-size")
ELSE(${distro}.${disversion} MATCHES "centos.7")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -O3 -std=gnu11 -pthread -Wno-format-truncation -Wno-unused-result -Wno-error=format-overflow")
ENDIF(${distro}.${disversion} MATCHES "centos.7")
include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/include/cutter)
include_directories(/usr/local/include/cutter)
include_directories(/usr/include/postgresql)
include_directories(/usr/include/mysql)
include_directories(external/mruby/include/)
include_directories(external/libhv/protocol/)
include_directories(external/zookeeper/zookeeper-client/zookeeper-client-c/include/)
include_directories(external/zookeeper/zookeeper-client/zookeeper-client-c/generated/)
#IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
include_directories(external/mbedtls/include/)
link_directories(external/mbedtls/library/)
#ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")

include_directories(external/iptables/include/)
include_directories(/usr/include/zookeeper/)
include_directories(/usr/lib/jvm/java-15-openjdk-15.0.1.9-2.rolling.el8.x86_64/include/)
include_directories(/usr/lib/jvm/java-15-openjdk-15.0.1.9-2.rolling.el8.x86_64/include/linux)
include_directories(/usr/lib/jvm/java-14-openjdk-14.0.2.12-1.rolling.el7.x86_64/include/)
include_directories(/usr/lib/jvm/java-14-openjdk-14.0.2.12-1.rolling.el7.x86_64/include/linux)
include_directories(/usr/lib/jvm/java-11-openjdk-11.0.10.0.9-0.el7_9.x86_64/include/)
include_directories(/usr/lib/jvm/java-11-openjdk-11.0.10.0.9-0.el7_9.x86_64/include/linux)
include_directories(/usr/lib/jvm/java-14-openjdk-amd64/include/)
include_directories(/usr/lib/jvm/java-14-openjdk-amd64/include/linux)
include_directories(/usr/lib/jvm/java-11-openjdk-amd64/include/)
include_directories(/usr/lib/jvm/java-11-openjdk-amd64/include/linux)
include_directories(/usr/local/include/mysql)
include_directories(/usr/local/include/hv/)
include_directories(external/libhv/include/hv/)
include_directories(/usr/local/include/)
include_directories(external/picohttpparser)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

link_directories(/usr/local/lib64/)
link_directories(/usr/local/lib/)
link_directories(external/mbedtls/library/)
link_directories(external/mbedtls/crypto/library/)
link_directories(external/mruby/build/host/lib/)
link_directories(/usr/lib/x86_64-linux-gnu/)
link_directories(/usr/local/lib64/)

set(STATIC_FILES
	events/uv_alloc.c
	events/a_signal.c
	events/debug.c
	events/server.c
	events/client.c
	events/process.c
	events/unix_server.c
	events/unixgram.c
	events/udp.c
	events/icmp.c
	events/general.c
	events/filetailer.c
	events/fs_write.c
	events/fs_read.c
	events/system_scrape.c
	events/fragmentation.c
	events/access.c
	events/metrics.c
	events/context_arg.c
	resolver/resolver.c
	resolver/udp.c
	resolver/tcp.c
	resolver/internal.c
	resolver/get.c
	resolver/parser.c
	resolver/getaddrinfo.c
	parsers/dummy.c
	parsers/pushgateway.c
	parsers/multiparser.c
	parsers/multicollector.c
	parsers/clickhouse.c
	parsers/aerospike.c
	parsers/redis.c
	parsers/sentinel.c
	parsers/memcached.c
	parsers/beanstalkd.c
	parsers/zookeeper.c
	parsers/gearmand.c
	parsers/haproxy.c
	parsers/http_proto.c
	parsers/uwsgi.c
	parsers/php-fpm.c
	parsers/eventstore.c
	parsers/flower.c
	parsers/nats.c
	parsers/riak.c
	parsers/rabbitmq.c
	parsers/opentsdb.c
	parsers/powerdns.c
	parsers/log.c
	parsers/elasticsearch.c
	parsers/nginx_upstream_check.c
	parsers/monit.c
	parsers/rsyslog_impstats.c
	parsers/gdnsd.c
	parsers/syslog-ng.c
	parsers/consul.c
	parsers/nifi.c
	parsers/varnish.c
	parsers/json.c
	parsers/postgresql.c
	parsers/hadoop.c
	parsers/unbound.c
	parsers/solr.c
	parsers/ntpd.c
	parsers/ipmi.c
	parsers/mysql.c
	parsers/tftp.c
	parsers/squid.c
	parsers/patroni.c
	parsers/lighttpd.c
	parsers/httpd.c
	parsers/nsd.c
	parsers/named.c
	parsers/jks.c
	parsers/kubernetes.c
	parsers/kubeconfig.c
	parsers/oracle.c
	parsers/couchdb.c
	parsers/couchbase.c
	parsers/druid.c
	parsers/mogilefs.c
	parsers/moosefs.c
	parsers/rpm.c
	parsers/firewall.c
	parsers/mongodb.c
	parsers/keepalived.c
	parsers/lang.c
	scheduler/influxdb.c
	scheduler/handler.c
	scheduler/push.c
	scheduler/get.c
	scheduler/del.c
	common/selector.c
	common/pem_check.c
	common/rtime.c
	common/validator.c
	common/base64.c
	common/http.c
	common/fastcgi.c
	common/url.c
	common/aggregator.c
	common/json_parser.c
	common/pcre_parser.c
	common/xml.c
	common/mapping.c
	common/smart.c
	common/netlib.c
	common/deb.c
	common/mkdirp.c
	common/yaml.c
	common/file_stat.c
	common/crc32.c
	common/murmurhash.c
	common/lcrypto.c
	puppeteer/puppeteer.c
	query/query.c
	query/promql.c
	action/action.c
	probe/probe.c
	cluster/type.c
	cluster/del.c
	cluster/get.c
	cluster/push.c
	cluster/pass.c
	cluster/api.c
	cluster/sync.c
	cluster/handler.c
	cluster/later.c
	router/stat.c
	router/probe.c
	router/json.c
	router/dsv.c
	router/conf.c
	router/oplog.c
	router/sharedlock.c
	router/labels_cache.c
	router/resolver.c
	api/api_router.c
	api/api_v1.c
	common/reject.c
	common/entrypoint.c
	common/pw.c
	common/stop.c
	config/env.c
	config/parser.c
	config/plain.c
	config/mapping.c
	config/get.c
	config/intstrvalue.c
	dynconf/sd.c
	lang/lang.c
	lang/lua.c
	lang/mruby.c
	lang/so.c
	lang/duktape.c
	lang/push.c
	system/common.c
	system/freebsd.c
	system/macosx.c
	system/linux.c
	system/linux/ipset.c
	system/linux/sysctl.c
	system/linux/conntrack.c
	system/linux/sockets.c
	system/fdescriptors.c
	modules/modules.c
	metric/expiretree.c
	metric/labels.c
	metric/metrictree.c
	metric/percentile_heap.c
	metric/metric_dump.c
	metric/query.c
	metric/namespace.c
	metric/serializer.c
	#metric/influxdb.c
	cadvisor/run.c
	cadvisor/ns.c
	cadvisor/metrics.c
	dstructures/uv_cache.c
	dstructures/ht.c
	dstructures/maglev.c
	dstructures/tommy.c
	dstructures/tommyds/tommyds/tommyarray.c
	dstructures/tommyds/tommyds/tommyhashdyn.c
	dstructures/tommyds/tommyds/tommyhashlin.c
	dstructures/tommyds/tommyds/tommylist.c
	dstructures/tommyds/tommyds/tommyhash.c
	dstructures/tommyds/tommyds/tommyarray.c
	dstructures/tommyds/tommyds/tommytree.c
	dstructures/tommyds/tommyds/tommytrie.c
	dstructures/tommyds/tommyds/tommyarrayblk.c
	external/picohttpparser/picohttpparser.c
	/app/src/external/libhv/protocol/dns.c
)
#IF(${distro} MATCHES "centos")
#set(RPM_FILES parser/rpm.c)
#ELSE(${distro} MATCHES "centos")
#set(RPM_FILES parser/rpm-common.c)
#ENDIF(${distro} MATCHES "centos")
add_library(${COMMON} STATIC ${STATIC_FILES} ${RPM_FILES})
add_executable(${HEAD} main.c)
configure_file(alligator_template.h ${CMAKE_SOURCE_DIR}/alligator_version.h)

add_library(${COMMON_TESTS} STATIC
	common/selector.c
	parsers/http_proto.c
	system/linux.c
	common/http.c
	common/base64.c
	common/selector.c
	events/context_arg.c
	common/validator.c
	common/url.c
	common/netlib.c
	common/pw.c
	query/promql.c
	dstructures/ht.c
	dstructures/tommy.c
	dstructures/tommyds/tommyds/tommyarray.c
	dstructures/tommyds/tommyds/tommyhashdyn.c
	dstructures/tommyds/tommyds/tommyhashlin.c
	dstructures/tommyds/tommyds/tommylist.c
	dstructures/tommyds/tommyds/tommyhash.c
	dstructures/tommyds/tommyds/tommyarray.c
	dstructures/tommyds/tommyds/tommytree.c
	dstructures/tommyds/tommyds/tommytrie.c
	dstructures/tommyds/tommyds/tommyarrayblk.c
	metric/expiretree.c
	metric/labels.c
	metric/metrictree.c
	metric/percentile_heap.c
	metric/metric_dump.c
)

set_property(TARGET ${COMMON_TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
IF(${distro}.${disversion} MATCHES "centos.7")
add_library(${TESTS} SHARED tests/unit/tests.c)
set_property(TARGET ${TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(${TESTS} ${COMMON_TESTS} libuv_a.a cutter)
add_test(Test1 "cutter" ".")
ENDIF(${distro}.${disversion} MATCHES "centos.7")
#add_custom_command(TARGET ${TESTS} POST_BUILD COMMAND cutter .)
add_custom_command(TARGET ${COMMON} POST_BUILD COMMAND mkdir -p /var/lib/alligator/)
add_custom_command(TARGET ${COMMON} POST_BUILD COMMAND cp puppeteer/puppeteer-alligator.js /var/lib/alligator/puppeteer-alligator.js)
add_custom_command(TARGET ${COMMON} POST_BUILD WORKING_DIRECTORY lang/parseJks COMMAND go build -o /var/lib/alligator/parseJks.so -buildmode=c-shared cmd/parseJks/main.go)
add_custom_command(TARGET ${COMMON} POST_BUILD WORKING_DIRECTORY lang/mongo COMMAND go build -o /var/lib/alligator/mongo.so -buildmode=c-shared cmd/mongo/main.go)

set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(${HEAD} ${COMMON} uv jansson)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	target_link_libraries(${HEAD} ${COMMON} libuv.a libm.a kvm util libdevstat.a libjansson.a libpcre.a libmbedtls.a libmbedx509.a libmbedcrypto.a libuv.a libjail.a liblua.a libduktape.a libmruby.a libmruby_core.a libhv_static.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	target_link_libraries(${HEAD} ${COMMON} -Wl,--no-as-needed dl libz.a libuv_a.a libpcre.a pthread libjansson.a m libatasmart.a libmbedtls.a libmbedx509.a libmbedcrypto.a -ludev libzookeeper_mt.a libjemalloc.a libfyaml-0.7.a libpcre.a liblua.a libmruby.a libmruby_core.a libduktape.a libuuid.a libcrypto.a libz.a libbsd.a libhv_static.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

#add_executable(${JP} json_parser.c)
#target_link_libraries(${JP} ${COMMON} jansson)

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_FREEBSD_PACKAGE_LICENSE "APACHE20")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "amoshi.mandrakeuser@gmail.com")
set(CPACK_PACKAGE_CONTACT "amoshi.mandrakeuser@gmail.com")

IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	install(FILES /var/lib/alligator/parseJks.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/mongo.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/puppeteer-alligator.js DESTINATION /var/lib/alligator)
	install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin" COMPONENT applications)
	install(FILES bin/alligator
		PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
		DESTINATION /usr/local/bin)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	install(FILES alligator.service DESTINATION /usr/lib/systemd/system)
	install(FILES /var/lib/alligator/parseJks.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/mongo.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/puppeteer-alligator.js DESTINATION /var/lib/alligator)
	install(FILES bin/alligator
		PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
		DESTINATION /usr/bin)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

install(DIRECTORY DESTINATION /var/lib/alligator/)

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION	/usr/bin
							/usr/lib/systemd
							/usr/lib/systemd/system)

include(CPack)
