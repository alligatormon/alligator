cmake_minimum_required(VERSION 3.6)

set(VERSION 1.10.124)
project(alligator VERSION ${VERSION})

set(CMAKE_BUILD_TYPE Debug)
set(COMMON_TESTS lib${PROJECT_NAME}_tests)
set(COMMON lib${PROJECT_NAME})
set(TESTS ${PROJECT_NAME}_tests)
set(HEAD ${PROJECT_NAME})
set(CLIENT ${PROJECT_NAME}_client)
#set(JP ${PROJECT_NAME}_jp)


set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -O3 -std=gnu11 -pthread")
include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/include/cutter)
include_directories(/usr/local/include/cutter)
include_directories(/usr/local/include/libbson-1.0)
include_directories(/usr/local/include/libmongoc-1.0)
include_directories(/usr/include/mysql)
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
include_directories(external/mbedtls/include/)
link_directories(external/mbedtls/library/)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")

include_directories(external/iptables/include/)
include_directories(/usr/include/zookeeper/)
include_directories(/usr/lib/jvm/java-14-openjdk-14.0.2.12-1.rolling.el7.x86_64/include/)
include_directories(/usr/lib/jvm/java-14-openjdk-14.0.2.12-1.rolling.el7.x86_64/include/linux)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

link_directories(/usr/local/lib64/)
link_directories(/usr/local/lib/)
link_directories(external/mbedtls/library/)
link_directories(external/mbedtls/crypto/library/)
link_directories(external/iptables/libiptc/.libs/)

set(STATIC_FILES
	events/uv_alloc.c
	events/a_signal.c
	events/debug.c
	events/server2.c
	events/client4.c
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
	platform/libbsd.c
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
	parsers/mongodb.c
	#parsers/mysql.c
	common/selector.c
	#common/https_tls_check.c
	common/pem_check2.c
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
	common/rpm.c
	common/deb.c
	common/mkdirp.c
	common/yaml.c
	query/query.c
	common/stat.c
	common/reject.c
	api/api_router.c
	api/api_v1.c
	common/iptc.c
	common/entrypoint.c
	#config/aggregate.c
	config/file.c
	config/context.c
	config/mapping.c
	config/parser.c
	config/plain.c
	dynconf/sd.c
	lang/lang.c
	lang/java.c
	system/linux.c
	system/freebsd.c
	system/macosx.c
	modules/modules.c
	metric/expiretree.c
	metric/labels.c
	metric/metrictree.c
	metric/percentile_heap.c
	metric/metric_dump.c
	cadvisor/run.c
	cadvisor/ns.c
	cadvisor/metrics.c
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
)
add_library(${COMMON} STATIC ${STATIC_FILES})
add_executable(${HEAD} main.c)

add_library(${COMMON_TESTS} STATIC
		common/selector.c
		parsers/http_proto.c
		system/linux.c
		common/http.c
		common/base64.c
		platform/libbsd.c
		common/selector.c
		common/validator.c
		common/url.c
		common/netlib.c
)

add_library(${TESTS} SHARED tests/unit/tests.c)
set_property(TARGET ${COMMON_TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET ${TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(${TESTS} ${COMMON_TESTS} uv cutter)
#add_custom_command(TARGET ${TESTS} POST_BUILD COMMAND cutter .)
add_custom_command(TARGET ${COMMON} PRE_BUILD COMMAND sed -e 's/TEMPLATE_VERSION/${VERSION}/' alligator_template.h > alligator_version.h)
add_custom_command(TARGET ${COMMON} POST_BUILD COMMAND javac -d /var/lib/alligator lang/alligatorJmx.java)
add_test(Test1 "cutter" ".")

set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(${HEAD} ${COMMON} uv jansson)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	target_link_libraries(${HEAD} ${COMMON} libuv.a libm.a kvm util libdevstat.a libjansson.a libpcre.a libmbedtls.a libmbedx509.a libmbedcrypto.a libuv.a libjail.a libjemalloc.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	target_link_libraries(${HEAD} ${COMMON} dl z libuv.a libpcre.a pthread libjansson.a m libatasmart.a libmbedtls.a libmbedx509.a libmbedcrypto.a -ludev /usr/lib64/libzookeeper_mt.a libip4tc.a libip6tc.a libbson-static-1.0.a libjemalloc.a libfyaml-0.5.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

#add_executable(${JP} json_parser.c)
#target_link_libraries(${JP} ${COMMON} jansson)

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "amoshi.mandrakeuser@gmail.com")
set(CPACK_PACKAGE_CONTACT "amoshi.mandrakeuser@gmail.com")

IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	install(FILES alligator
		PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
		DESTINATION /usr/local/bin)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	install(FILES alligator.service DESTINATION /usr/lib/systemd/system)
	install(FILES alligator
		PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
		DESTINATION /usr/bin)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

install(DIRECTORY DESTINATION /var/lib/alligator/)

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION	/usr/bin
							/usr/lib/systemd
							/usr/lib/systemd/system)

include(CPack)
