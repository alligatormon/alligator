cmake_minimum_required(VERSION 3.8)

set(VERSION 1.14.146)
project(alligator VERSION ${VERSION})

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()
include(FindPkgConfig)
find_package(PostgreSQL REQUIRED)

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
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wno-undef -Wunreachable-code -O3 -std=gnu99 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "centos.9")
set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-error=unused-result -Wno-error=undef -Wno-undef -Wunreachable-code -Wno-visibility -std=gnu11 -pthread -O3")
ELSEIF(${distro}.${disversion} MATCHES "ubuntu.16.04")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wundef -Wunreachable-code -O3 -std=gnu11 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "ubuntu.14.04")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wundef -Wunreachable-code -O3 -std=gnu11 -pthread")
ELSEIF(${distro}.${disversion} MATCHES "freebsd.11")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -O3 -std=gnu11 -pthread -Wno-unused-result -Wno-strlcpy-strlcat-size")
ELSE(${distro}.${disversion} MATCHES "centos.7")
set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-error=unused-result -Wno-error=undef -Wno-undef -Wunreachable-code -Wno-visibility -O3 -std=gnu11 -pthread")
ENDIF(${distro}.${disversion} MATCHES "centos.7")
include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/include/postgresql)
include_directories(/usr/include/mysql)
include_directories(/var/lib/smartmontools/drivedb/)
include_directories(/usr/share/smartmontools/)
include_directories(external/mruby/include/)
include_directories(external/libhv/protocol/)

include_directories(/usr/include/zookeeper/)
include_directories(/usr/include/libnl3)
include_directories(/usr/local/include/mysql)
include_directories(/usr/local/include/hv/)
include_directories(external/libhv/include/hv/)
include_directories(/usr/local/include/)
include_directories(external/picohttpparser)
include_directories(external/libfyaml/include/)
include_directories(external/libatasmart/)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(external/mruby/include)

link_directories(/usr/local/lib64/)
link_directories(/usr/local/lib/)
link_directories(external/mruby/build/host/lib/)
link_directories(external/libhv/lib/)
link_directories(/usr/lib/x86_64-linux-gnu/)
link_directories(/usr/local/lib64/)
link_directories(external/mruby/build/host/lib/)
link_directories(external/libatasmart/.libs/)
link_directories(external/libfyaml/src/.libs/)
link_directories(external/picohttpparser/)

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
	events/threads.c
	events/tls.c
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
	parsers/cassandra.c
	parsers/keepalived.c
	parsers/lang.c
	parsers/nvidia-smi.c
	parsers/auditd.c
	scheduler/influxdb.c
	scheduler/handler.c
	scheduler/push.c
	scheduler/get.c
	scheduler/del.c
	common/selector.c
	common/rtime.c
	common/validator.c
	common/base64.c
	common/http.c
	common/url.c
	common/aggregator.c
	common/json_query.c
	common/pcre_parser.c
	common/xml.c
	common/mapping.c
	common/netlib.c
	common/deb.c
	common/mkdirp.c
	common/yaml.c
	common/file_stat.c
	common/crc32.c
	common/murmurhash.c
	common/xxh.c
	common/lcrypto.c
	common/units.c
	common/logs.c
	common/patricia.c
	common/auth.c
	smart/smart.c
	smart/drivedb.c
	x509/pem_check.c
	x509/push.c
	x509/del.c
	puppeteer/puppeteer.c
	query/query.c
	query/push.c
	query/del.c
	query/get.c
	query/promql.c
	action/action.c
	action/push.c
	action/del.c
	action/get.c
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
	dynconf/sd.c
	lang/lang.c
	lang/lua.c
	lang/mruby.c
	lang/so.c
	lang/duktape.c
	lang/push.c
	lang/del.c
	system/common.c
	system/freebsd.c
	system/macosx.c
	system/smbios.c
	system/linux.c
	system/linux/ipset.c
	system/linux/sysctl.c
	system/linux/conntrack.c
	system/linux/sockets.c
	system/linux/memory.c
	system/linux/nftables.c
	system/linux/distribution_name.c
	system/linux/interrupts.c
	system/linux/power_supply.c
	system/linux/disk.c
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
	cadvisor/libvirt.c
	dstructures/uv_cache.c
	dstructures/ht.c
	dstructures/maglev.c
	dstructures/tommy.c
	dstructures/tommyds/tommyds/tommyhashdyn.c
	dstructures/tommyds/tommyds/tommylist.c
	dstructures/tommyds/tommyds/tommyhash.c
	dstructures/ngram/ngram.c
)

message("found ${SYSTEMD_FOUND}.${SYSTEMD_VERSION}")
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
if(${distro} MATCHES "centos")
pkg_check_modules(SYSTEMD "systemd" REQUIRED)
	if(SYSTEMD_FOUND)
		if(${SYSTEMD_VERSION} VERSION_LESS 221)
			set(SYSTEMD_FILES
				system/linux/systemd.c
			)
		else()
			set(SYSTEMD_FILES
				system/linux/systemd-sdbus.c
			)
		endif()
	endif()
else()
pkg_check_modules(SYSTEMD "libsystemd" REQUIRED)
	if(SYSTEMD_FOUND)
		if(${SYSTEMD_VERSION} VERSION_LESS 221)
			set(SYSTEMD_FILES
				system/linux/systemd.c
			)
		else()
			set(SYSTEMD_FILES
				system/linux/systemd-sdbus.c
			)
		endif()
	endif()
endif()
endif()
add_library(${COMMON} STATIC ${STATIC_FILES} ${SYSTEMD_FILES})
add_executable(${HEAD} main.c)
configure_file(alligator_template.h ${CMAKE_SOURCE_DIR}/alligator_version.h)

add_library(${COMMON_TESTS} STATIC ${STATIC_FILES} ${SYSTEMD_FILES})

set_property(TARGET ${COMMON_TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
add_executable(${TESTS} tests/unit2/tests.c)
set_property(TARGET ${TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(${TESTS} ${COMMON_TESTS} systemd dl libz.a libuv_a.a libpcre.a pthread libjansson.a m libatasmart.a -ludev libjemalloc.a libfyaml.a libpcre.a liblua.a libmruby.a libmruby_core.a libduktape.a libuuid.a libcrypto.a libssl.a libz.a libbsd.a libhv.a libpicohttpparser.a libdns.a libpq.a libpgcommon.a libpgport.a libxxhash.a)
add_test(Test1 "bin/alligator_tests" ".")
add_custom_command(TARGET ${COMMON} POST_BUILD COMMAND mkdir -p /var/lib/alligator/)
add_custom_command(TARGET ${COMMON} POST_BUILD COMMAND cp puppeteer/puppeteer-alligator.js /var/lib/alligator/puppeteer-alligator.js)
add_custom_command(TARGET ${COMMON} POST_BUILD WORKING_DIRECTORY lang/parseJks COMMAND go build -o /var/lib/alligator/parseJks.so -buildmode=c-shared cmd/parseJks/main.go)
add_custom_command(TARGET ${COMMON} POST_BUILD WORKING_DIRECTORY lang/mongo COMMAND go build -o /var/lib/alligator/mongo.so -buildmode=c-shared cmd/mongo/main.go)
add_custom_command(TARGET ${COMMON} POST_BUILD WORKING_DIRECTORY lang/cassandra COMMAND go build -o /var/lib/alligator/cassandra.so -buildmode=c-shared cmd/cassandra/main.go)

set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)
IF(NOT ${distro}.${disversion} MATCHES "centos.7")
set_target_properties(${TESTS} PROPERTIES
    COMPILE_FLAGS "-fprofile-instr-generate -fcoverage-mapping"
    LINK_FLAGS "-fprofile-generate"
)
set_target_properties(${COMMON_TESTS} PROPERTIES
    COMPILE_FLAGS "-fprofile-instr-generate -fcoverage-mapping"
    LINK_FLAGS "-fprofile-generate"
)
# to check coverage report use the: ./bin/alligator_tests pass; llvm-profdata merge -sparse default.profraw -o default.profdata; llvm-cov report -instr-profile=default.profdata bin/alligator_tests
ENDIF(NOT ${distro}.${disversion} MATCHES "centos.7")

IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(${HEAD} ${COMMON} libuv_a.a jansson libpq.a libpgcommon.a libpgport.a libxxhash.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	target_link_libraries(${HEAD} ${COMMON} libuv_a.a libm.a kvm util libdevstat.a libjansson.a libpcre.a libjail.a liblua.a libduktape.a libmruby.a libmruby_core.a libhv.a picohttpparser.a libdns.a libpq.a libpgcommon.a libpgport.a libxxhash.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	target_link_libraries(${HEAD} ${COMMON} -Wl,--no-as-needed dl systemd libz.a libuv_a.a libpcre.a pthread libjansson.a m libatasmart.a -ludev libjemalloc.a libfyaml.a libpcre.a liblua.a libmruby.a libmruby_core.a libduktape.a libuuid.a libcrypto.a libssl.a libz.a libbsd.a libhv.a libpicohttpparser.a libdns.a libpq.a libpgcommon.a libpgport.a libxxhash.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

#add_executable(${JP} json_parser.c)
#target_link_libraries(${JP} ${COMMON} jansson)

function(add_include_flags include_flags_var)
    set(result "")
    foreach(dir ${ARGN})
        if (NOT ${dir} MATCHES "libbsd")
            list(APPEND result "-I${dir}")
        endif()
    endforeach()
    set(${include_flags_var} "${result}" PARENT_SCOPE)
endfunction()
add_include_flags(include_flags ${CMAKE_INCLUDE_PATH})


include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/include/postgresql)
include_directories(/usr/include/mysql)
include_directories(external/mruby/include/)
include_directories(external/libhv/protocol/)

include_directories(/usr/include/zookeeper/)
include_directories(/usr/include/libnl3)
include_directories(/usr/local/include/mysql)
include_directories(/usr/local/include/hv/)
include_directories(external/libhv/include/hv/)
include_directories(/usr/local/include/)
include_directories(external/picohttpparser)
include_directories(external/libfyaml/include/)
include_directories(external/libatasmart/)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(external/mruby/include)


get_target_property(SRCs ${HEAD} SOURCES)
#add_custom_target(${HEAD}_analyze COMMAND clang ${CMAKE_C_FLAGS} -I. -I/usr/include/x86_64-linux-gnu/ -Iexternal/picohttpparser -Iexternal/libhv/protocol/ -Iexternal/libhv/include/hv -Iexternal/libatasmart/ ${include_flags} --analyze;-Xanalyzer;-analyzer-output=text ${STATIC_FILES})
add_custom_target(${HEAD}_analyze COMMAND clang-tidy --config-file=.clang-tidy.analyze ${SRCs} ${STATIC_FILES} -- ${CMAKE_C_FLAGS} -Wno-implicit-function-declaration -I. -I/usr/include/ -Iexternal/picohttpparser -Iexternal/libhv/protocol/ -Iexternal/libhv/include/hv -Iexternal/libatasmart/ -Iexternal/libfyaml/include/ -Iexternal/mruby/include/ ${include_flags})

add_custom_target(${HEAD}_lint COMMAND clang-tidy --config-file=.clang-tidy.lint ${SRCs} ${STATIC_FILES} -- ${CMAKE_C_FLAGS} -Wno-implicit-function-declaration -I. -I/usr/include/ -Iexternal/picohttpparser -Iexternal/libhv/protocol/ -Iexternal/libhv/include/hv -Iexternal/libatasmart/ -Iexternal/libfyaml/include/ -Iexternal/mruby/include/ ${include_flags})


set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_FREEBSD_PACKAGE_LICENSE "APACHE20")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "amoshi.mandrakeuser@gmail.com")
set(CPACK_PACKAGE_CONTACT "amoshi.mandrakeuser@gmail.com")

IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	install(FILES /var/lib/alligator/parseJks.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/mongo.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/cassandra.so DESTINATION /var/lib/alligator)
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
	install(FILES /var/lib/alligator/cassandra.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/puppeteer-alligator.js DESTINATION /var/lib/alligator)
	install(FILES bin/alligator
		PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
		DESTINATION /usr/bin)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

install(DIRECTORY DESTINATION /var/lib/alligator/)

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION	/usr/bin
							/usr/lib/systemd
							/usr/lib/systemd/system)

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "alligator is aggregator for system and software metrics")
set(CPACK_RPM_PACKAGE_DESCRIPTION "alligator is aggregator for system and software metrics")
set(CPACK_RESOURCE_FILE_LICENSE ../LICENSE)
set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
include(CPack)
