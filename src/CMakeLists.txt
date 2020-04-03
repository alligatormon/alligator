cmake_minimum_required(VERSION 3.6)
##add_subdirectory(external/mbedtls)

project(alligator VERSION 1.10.60)

set(CMAKE_BUILD_TYPE Debug)
set(COMMON_TESTS lib${PROJECT_NAME}_tests)
set(COMMON lib${PROJECT_NAME})
set(TESTS ${PROJECT_NAME}_tests)
set(HEAD ${PROJECT_NAME})
set(CLIENT ${PROJECT_NAME}_client)
#set(JP ${PROJECT_NAME}_jp)


#set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-format-truncation -Wno-error=unused-result -O3 -std=gnu11 -pthread")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -O3 -std=gnu11 -pthread")
include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/include/cutter)
include_directories(/usr/local/include/cutter)
#include_directories(/usr/local/opt/openssl/include/)
#include_directories(/usr/local/ssl/include/)
#include_directories(evt-tls/api)
#include_directories(evt-tls)
include_directories(external/libpquv/src/include/)
include_directories(external/libuv-mbedtls/include/)
include_directories(external/iptables/include/)
include_directories(/usr/include/zookeeper/)
##include_directories(external/mbedtls/include/)
##include_directories(external/mbedtls/crypto/include/)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

#link_directories(/usr/local/ssl/lib/)
link_directories(/usr/local/lib64/)
link_directories(/usr/local/lib/)
link_directories(external/mbedtls/library/)
link_directories(external/mbedtls/crypto/library/)
link_directories(external/iptables/libiptc/.libs/)

set(STATIC_FILES
	#events/uv_tls.c
	events/uv_alloc.c
	events/a_signal.c
	events/httpserver.c
	#events/client.c
	#events/sclient.c
	events/sclient3.c
	events/client3.c
	events/process.c
	events/unix_client.c
	events/unix_server.c
	events/unixgram.c
	events/udp.c
	events/icmp.c
	events/general.c
	events/filetailer.c
	events/fs_write.c
	events/fs_read.c
	events/system_scrape.c
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
	common/mkdirp.c
	api/api_router.c
	api/api_v1.c
	common/iptc.c
	#config/aggregate.c
	config/file.c
	config/context.c
	config/mapping.c
	dynconf/sd.c
	system/linux.c
	system/freebsd.c
	system/macosx.c
	modules/modules.c
	modules/postgresql.c
	metric/expiretree.c
	metric/labels.c
	metric/metrictree.c
	metric/percentile_heap.c
	metric/metric_dump.c
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
	#evt-tls/src/evt_tls.c
	external/libpquv/src/pquv.c
	external/libpquv/src/util.c
	
)
add_library(${COMMON} STATIC ${STATIC_FILES})
add_executable(${HEAD} main.c)

#add_subdirectory(modules/libpquv)
#add_library(postgresql_client SHARED modules/postgresql.c)
#target_link_libraries(postgresql_client c pquv)
#include_directories(./modules/libpquv/src/include/)
#link_directories(./modules/libpquv/src/)


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
add_custom_command(TARGET ${TESTS} POST_BUILD COMMAND cutter .)
add_test(Test1 "cutter" ".")

set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(${HEAD} ${COMMON} ssl crypto uv jansson)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	target_link_libraries(${HEAD} ${COMMON} ssl crypto uv m kvm util devstat jansson pcre)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	#target_link_libraries(${HEAD} ${COMMON} libssl.a libcrypto.a dl z libuv.a libpcre.a pthread libjansson.a m libatasmart.a libmbedtls.a libmbedcrypto.a libmbedx509.a -ludev)
	target_link_libraries(${HEAD} ${COMMON} dl z libuv.a libpcre.a pthread libjansson.a m libatasmart.a libmbedtls.a libmbedx509.a libmbedcrypto.a -ludev /usr/lib64/libzookeeper_mt.a libip4tc.a libip6tc.a)
	#target_link_libraries(${HEAD} ${COMMON} dl z libuv.a libpcre.a pthread libjansson.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

#add_executable(${JP} json_parser.c)
#target_link_libraries(${JP} ${COMMON} jansson)

#add_library(aggregator3_lib thpool/thpool.c)
#target_link_libraries(aggregator3_lib pthread)
#add_executable(${AGGREGATOR3} aggregator3.c)
#target_link_libraries(${AGGREGATOR3} uv aggregator3_lib)

#set_target_properties(${COMMON} ${PLUGIN} PROPERTIES
#  PREFIX ""
#  CXX_STANDARD 14
#  CXX_STANDARD_REQUIRED ON
#  COMPILE_OPTIONS -fPIC -Wall -Wextra
#  )

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "amoshi.mandrakeuser@gmail.com")
set(CPACK_PACKAGE_CONTACT "amoshi.mandrakeuser@gmail.com")

#install(TARGETS ${PLUGIN} DESTINATION /usr/lib64/uwsgi)

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
