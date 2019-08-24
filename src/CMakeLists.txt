cmake_minimum_required(VERSION 3.6)

project(alligator VERSION 1.9.114)

set(CMAKE_BUILD_TYPE Debug)
set(COMMON_TESTS lib${PROJECT_NAME}_tests)
set(COMMON lib${PROJECT_NAME})
#set(TESTS ${PROJECT_NAME}_tests)
set(HEAD ${PROJECT_NAME})
set(CLIENT ${PROJECT_NAME}_client)
#set(JP ${PROJECT_NAME}_jp)


IF(WIN32)
	set(CMAKE_CXX_COMPILER C:\\Program Files\\LLVM\\bin\\clang-cl.exe)
	set(CMAKE_C_COMPILER C:\\Program Files\\LLVM\\bin\\clang-cl.exe)
	set(EXECUTABLE_OUTPUT_PATH bin)
	include_directories(C:\\assets\\libuv64\\include)
	include_directories(C:\\assets\\)
	include_directories(${CMAKE_CURRENT_BINARY_DIR})
	link_directories(C:\\assets\\libuv64\\lib)
	link_directories(${CMAKE_CURRENT_BINARY_DIR})
	add_library(${COMMON} STATIC
		dstructures/tommyds/tommyds/tommyarray.c
		dstructures/tommyds/tommyds/tommyhashdyn.c
		dstructures/tommyds/tommyds/tommyhashlin.c
		dstructures/tommyds/tommyds/tommylist.c
		dstructures/tommyds/tommyds/tommyhash.c
		dstructures/tommyds/tommyds/tommyarray.c
		dstructures/tommyds/tommyds/tommytree.c
		dstructures/tommyds/tommyds/tommytrie.c
		dstructures/tommyds/tommyds/tommyarrayblk.c
		evt-tls/src/evt_tls.c
		system/windows.c
		common/selector.c
		events/uv_alloc.c
		events/httpserver.c
		events/client.c
		events/system_scrape.c
		common/base64.c
		common/http.c
		dstructures/metric.c
		dstructures/tommy.c
		dstructures/rbtree.c
		config/aggregate.c
		config/file.c
		system/linux.c
		config/context.c
		config/mapping.c
		parsers/winparser.c
		common/rtime.c
		common/mapping.c
		platform/windows.c
		#parsers/prom_format.c
		parsers/sqlserver.c
		#events/process.c
		#events/fs_write.c
	)
	add_executable(${HEAD} win32.c)
	set_target_properties(${HEAD} PROPERTIES OUTPUT_NAME "alligator")
	target_link_libraries(${HEAD} ${COMMON} uv.lib odbc32.lib)
ELSE(WIN32)
	set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -O3 -std=gnu11 -pthread")
	include_directories(/usr/include)
	include_directories(/usr/local/include)
	include_directories(/usr/include/cutter)
	include_directories(/usr/local/include/cutter)
	include_directories(/usr/local/opt/openssl/include/)
	include_directories(evt-tls/api)
	include_directories(evt-tls)
	include_directories(${CMAKE_CURRENT_BINARY_DIR})
	include_directories(./libatasmart)

	link_directories(./openssl-1.0.2s)
	link_directories(./libatasmart/.libs/)
	link_directories(/usr/local/lib64/)
	link_directories(/usr/local/lib/)
	#link_directories(/usr/local/opt/openssl/lib/)

	set(STATIC_FILES
		events/uv_tls.c
		events/uv_alloc.c
		events/a_signal.c
		events/httpserver.c
		events/client.c
		events/sclient.c
		events/process.c
		events/unix_client.c
		events/unix_server.c
		events/unixgram.c
		events/udp.c
		events/icmp.c
		events/general.c
		events/filetailer.c
		events/fs_write.c
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
		parsers/mssql.c
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
		common/selector.c
		common/https_tls_check.c
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
		config/aggregate.c
		config/file.c
		config/context.c
		config/mapping.c
		system/linux.c
		system/freebsd.c
		system/macosx.c
		modules/modules.c
		metric/expiretree.c
		metric/labels.c
		metric/metrictree.c
		metric/percentile_heap.c
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
		evt-tls/src/evt_tls.c
		
	)
	add_library(${COMMON} STATIC ${STATIC_FILES})
	add_executable(${HEAD} main.c)

	#add_subdirectory(modules/libpquv)
	#add_library(postgresql_client SHARED modules/postgresql.c)
	#target_link_libraries(postgresql_client c pquv)
	#include_directories(./modules/libpquv/src/include/)
	#link_directories(./modules/libpquv/src/)
ENDIF(WIN32)


add_library(${COMMON_TESTS} STATIC
		common/selector.c
		parsers/http_proto.c
		#parsers/prom_format.c
		system/linux.c
		common/http.c
		common/base64.c
		platform/libbsd.c
		common/selector.c
		common/validator.c
		common/url.c
)

###add_library(${TESTS} SHARED tests/unit/tests.c)
###set_property(TARGET ${COMMON_TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
###set_property(TARGET ${TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
###target_link_libraries(${TESTS} ${COMMON_TESTS} uv cutter)
#add_custom_command(TARGET ${TESTS} POST_BUILD COMMAND cutter .)
#add_test(Test1 "cutter" ".")
#add_executable(${CLIENT} events/server_handler.c)
#target_link_libraries(${CLIENT} ${COMMON} uv)

set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(${HEAD} ${COMMON} ssl crypto uv jansson)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	target_link_libraries(${HEAD} ${COMMON} ssl crypto uv m kvm util devstat jansson)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	#target_link_libraries(${HEAD} ${COMMON} ssl crypto dl z pthread libuv.a)
	target_link_libraries(${HEAD} ${COMMON} libssl.a libcrypto.a dl z libuv.a libpcre.a pthread libjansson.a m libatasmart.a -ludev)
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
