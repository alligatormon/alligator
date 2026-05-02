cmake_minimum_required(VERSION 3.8)

set(VERSION 1.14.176)
project(alligator VERSION ${VERSION})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FindPkgConfig)
set(CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/generators" ${CMAKE_PREFIX_PATH})

# Provide a default Conan toolchain only if user didn't specify one.
# This must NOT override -DCMAKE_TOOLCHAIN_FILE=... from the cmake command line.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR "${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
	set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/build/build/Debug/generators/conan_toolchain.cmake" CACHE FILEPATH "Conan Toolchain")
endif()

# Conan generators are expected next to the conan_toolchain.cmake.
# Add that directory to CMAKE_PREFIX_PATH so find_package() can locate Config files and
# imported targets (and their include dirs).
if(EXISTS "${CMAKE_TOOLCHAIN_FILE}")
	get_filename_component(CONAN_GENERATORS_DIR "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
	set(CMAKE_PREFIX_PATH "${CONAN_GENERATORS_DIR}" ${CMAKE_PREFIX_PATH})
endif()
find_package(jansson REQUIRED)
find_package(libuv REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(pcre REQUIRED)
find_package(libbsd REQUIRED)
find_package(libuuid REQUIRED)
find_package(xxHash REQUIRED)
find_package(lua REQUIRED)
find_package(duktape REQUIRED)
find_package(oniguruma REQUIRED)

set(LIBUV_INCLUDE_HINTS "")
if(TARGET libuv::uv_a)
	get_target_property(LIBUV_INCLUDE_HINTS libuv::uv_a INTERFACE_INCLUDE_DIRECTORIES)
elseif(TARGET uv_a)
	get_target_property(LIBUV_INCLUDE_HINTS uv_a INTERFACE_INCLUDE_DIRECTORIES)
elseif(TARGET libuv::uv)
	get_target_property(LIBUV_INCLUDE_HINTS libuv::uv INTERFACE_INCLUDE_DIRECTORIES)
elseif(DEFINED libuv_INCLUDE_DIRS)
	set(LIBUV_INCLUDE_HINTS ${libuv_INCLUDE_DIRS})
elseif(DEFINED LIBUV_INCLUDE_DIRS)
	set(LIBUV_INCLUDE_HINTS ${LIBUV_INCLUDE_DIRS})
else()
	pkg_check_modules(LIBUV_PC libuv)
	if(LIBUV_PC_FOUND)
		set(LIBUV_INCLUDE_HINTS ${LIBUV_PC_INCLUDE_DIRS})
	endif()
endif()
if(LIBUV_INCLUDE_HINTS)
	include_directories(${LIBUV_INCLUDE_HINTS})
endif()

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()
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
# Choose an existing libuv target/library name in this toolchain.
set(LIBUV_DRIVER "")
if(TARGET libuv::uv_a)
	set(LIBUV_DRIVER "libuv::uv_a")
elseif(TARGET libuv::uv)
	set(LIBUV_DRIVER "libuv::uv")
elseif(TARGET uv_a)
	set(LIBUV_DRIVER "uv_a")
elseif(TARGET uv)
	set(LIBUV_DRIVER "uv")
elseif(DEFINED libuv_LIBRARIES AND NOT "${libuv_LIBRARIES}" STREQUAL "")
	set(LIBUV_DRIVER "${libuv_LIBRARIES}")
elseif(DEFINED LIBUV_LIBRARIES AND NOT "${LIBUV_LIBRARIES}" STREQUAL "")
	set(LIBUV_DRIVER "${LIBUV_LIBRARIES}")
elseif(LIBUV_PC_FOUND AND LIBUV_PC_LIBRARIES)
	set(LIBUV_DRIVER "${LIBUV_PC_LIBRARIES}")
endif()
if("${LIBUV_DRIVER}" STREQUAL "")
	message(FATAL_ERROR "libuv target/library was not detected. Checked: libuv::uv_a, libuv::uv, uv_a, uv, libuv_LIBRARIES, pkg-config(libuv).")
endif()

IF(${distro}.${disversion} MATCHES "centos.7")
#set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -fstack-protector-all -Wfloat-equal -Wundef -Wshadow -Wpointer-arith -Wcast-align -Wstrict-prototypes -Waggregate-return -Wcast-qual -Wswitch-default -Wswitch-enum -Wconversion -Wunreachable-code -O3 -std=gnu11 -pthread")
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wno-undef -Wunreachable-code -O3 -std=gnu99 -pthread")
# Keep auto-selected libuv target/library; no centos-specific override here.
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
include_directories(/var/lib/smartmontools/drivedb/)
include_directories(/usr/share/smartmontools/)
include_directories(external/mruby/include/)
include_directories(external/libhv/protocol/)

include_directories(/usr/include/zookeeper/)
include_directories(/usr/include/libnl3)
include_directories(/usr/local/include/hv/)
include_directories(external/libhv/include/hv/)
include_directories(/usr/local/include/)
include_directories(external/picohttpparser)
include_directories(external/libfyaml/include/)
include_directories(external/libatasmart/)
include_directories(${PROJECT_SOURCE_DIR})
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
	parsers/dynatrace.c
	parsers/otlp.c
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
	parsers/snmp.c
	parsers/snmp_mib.c
	parsers/mysql.c
	parsers/mysql2.c
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
	parsers/mongodb_wire_client.c
	parsers/mongodb_wire_bson.c
	parsers/bson.c
	parsers/cassandra.c
	parsers/cassandra2.c
	parsers/keepalived.c
	parsers/lang.c
	parsers/nvidia-smi.c
	parsers/auditd.c
	parsers/wazuh.c
	amtail/amtail.c
	amtail/push.c
	amtail/del.c
	external/amtail/amtail_pcre.c
	external/amtail/compile.c
	external/amtail/file.c
	external/amtail/generator.c
	external/amtail/lex.c
	external/amtail/parser.c
	external/amtail/variables.c
	external/amtail/vm.c
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
	common/protobuf_wire.c
	common/url.c
	common/aggregator.c
	common/json_query.c
	common/pcre_parser.c
	common/xml.c
	mapping/mapping.c
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
	grok/grok.c
	grok/push.c
	grok/del.c
	grok/get.c
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
	mapping/type.c
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
	system/linux/ipmi.c
	system/linux/ipset.c
	system/linux/cpu.c
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
	metric/transform.c
	#metric/influxdb.c
	cadvisor/run.c
	cadvisor/ns.c
	cadvisor/metrics.c
	cadvisor/libvirt.c
	dstructures/uv_cache.c
	dstructures/queue.c
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
target_link_libraries(${COMMON} PUBLIC jansson::jansson pcre::pcre PostgreSQL::PostgreSQL libbsd::libbsd libuuid::libuuid xxHash::xxhash lua::lua duktape::duktape ${LIBUV_DRIVER} oniguruma::onig libfyaml.a libmruby.a libmruby_core.a libdns.a libatasmart.a -ludev libpicohttpparser.a)
add_executable(${HEAD} main.c)
configure_file(alligator_template.h ${CMAKE_SOURCE_DIR}/alligator_version.h)

add_library(${COMMON_TESTS} STATIC ${STATIC_FILES} ${SYSTEMD_FILES})
target_link_libraries(${COMMON_TESTS} PUBLIC jansson::jansson pcre::pcre PostgreSQL::PostgreSQL libbsd::libbsd libuuid::libuuid xxHash::xxhash lua::lua duktape::duktape ${LIBUV_DRIVER} oniguruma::onig libfyaml.a libmruby.a libmruby_core.a libdns.a libatasmart.a -ludev libpicohttpparser.a)

set_property(TARGET ${COMMON_TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
add_executable(${TESTS} tests/unit2/tests.c)
set_property(TARGET ${TESTS} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(${TESTS} ${COMMON_TESTS} -Wl,--no-as-needed dl systemd jansson::jansson)
# Parser tests + long main() chains can sit near the default stack limit; JSON L_DEBUG would
# still push va_list frames. Reserve a larger main thread stack for the test binary only.
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	target_link_options(${TESTS} PRIVATE "LINKER:-z,stack-size=0x4000000")
ENDIF()
IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_options(${TESTS} PRIVATE "LINKER:-stack_size,0x4000000")
ENDIF()
add_test(Test1 "build/alligator_tests" ".")
add_custom_command(TARGET ${COMMON} POST_BUILD COMMAND mkdir -p /var/lib/alligator/)
add_custom_command(TARGET ${COMMON} POST_BUILD COMMAND cp ../puppeteer/puppeteer-alligator.js /var/lib/alligator/puppeteer-alligator.js)
add_custom_command(TARGET ${COMMON} POST_BUILD WORKING_DIRECTORY ../lang/parseJks COMMAND go build -o /var/lib/alligator/parseJks.so -buildmode=c-shared cmd/parseJks/main.go)

set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)
# RPM checks reject absolute/build/local RUNPATH values in installed binaries.
# Keep runtime lookup clean for packaged artifacts.
set_target_properties(${HEAD} PROPERTIES
	SKIP_BUILD_RPATH TRUE
	BUILD_WITH_INSTALL_RPATH FALSE
	INSTALL_RPATH ""
	INSTALL_RPATH_USE_LINK_PATH FALSE
)
set_target_properties(${TESTS} PROPERTIES
	SKIP_BUILD_RPATH TRUE
	BUILD_WITH_INSTALL_RPATH FALSE
	INSTALL_RPATH ""
	INSTALL_RPATH_USE_LINK_PATH FALSE
)
IF(NOT ${distro}.${disversion} MATCHES "centos.7")
set_target_properties(${TESTS} PROPERTIES
    COMPILE_FLAGS "-fprofile-instr-generate -fcoverage-mapping"
    LINK_FLAGS "-fprofile-generate"
)
set_target_properties(${COMMON_TESTS} PROPERTIES
    COMPILE_FLAGS "-fprofile-instr-generate -fcoverage-mapping"
    LINK_FLAGS "-fprofile-generate"
)
# to check coverage report use the: ./build/alligator_tests pass; llvm-profdata merge -sparse default.profraw -o default.profdata; llvm-cov report -instr-profile=default.profdata build/alligator_tests
ENDIF(NOT ${distro}.${disversion} MATCHES "centos.7")

IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	target_link_libraries(${HEAD} ${COMMON} uv_a jansson libpq.a libpgcommon.a libpgport.a libxxhash.a libonig.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	target_link_libraries(${HEAD} ${COMMON} uv_a libm.a kvm util libdevstat.a libjansson.a libpcre.a libjail.a liblua.a libduktape.a libmruby.a libmruby_core.a libhv.a picohttpparser.a libdns.a libpq.a libpgcommon.a libpgport.a libxxhash.a libonig.a)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	target_link_libraries(${HEAD} ${COMMON} -Wl,--no-as-needed dl systemd jansson::jansson PostgreSQL::PostgreSQL)
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


include_directories(external/mruby/include/)
include_directories(external/libhv/protocol/)

include_directories(external/libhv/include/hv/)
include_directories(external/picohttpparser)
include_directories(external/libfyaml/include/)
include_directories(external/libatasmart/)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(external/mruby/include)


get_target_property(SRCs ${HEAD} SOURCES)
#add_custom_target(${HEAD}_analyze COMMAND clang ${CMAKE_C_FLAGS} -I. -I/usr/include/x86_64-linux-gnu/ -Iexternal/picohttpparser -Iexternal/libhv/protocol/ -Iexternal/libhv/include/hv -Iexternal/libatasmart/ ${include_flags} --analyze;-Xanalyzer;-analyzer-output=text ${STATIC_FILES})
add_custom_target(${HEAD}_analyze
	COMMAND clang-tidy
		-p=${CMAKE_BINARY_DIR}
		--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy.analyze
		${SRCs} ${STATIC_FILES}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(${HEAD}_lint
	COMMAND clang-tidy
		-p=${CMAKE_BINARY_DIR}
		--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy.lint
		${SRCs} ${STATIC_FILES}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)


set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_FREEBSD_PACKAGE_LICENSE "APACHE20")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "amoshi.mandrakeuser@gmail.com")
set(CPACK_PACKAGE_CONTACT "amoshi.mandrakeuser@gmail.com")

IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	install(FILES grok-patterns/patterns.conf DESTINATION /etc/grok-patterns)
	install(FILES /var/lib/alligator/parseJks.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/puppeteer-alligator.js DESTINATION /var/lib/alligator)
	install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin" COMPONENT applications)
	install(FILES build/alligator
		PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
		DESTINATION /usr/local/bin)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	install(FILES alligator.service DESTINATION /usr/lib/systemd/system)
	install(FILES grok-patterns/patterns.conf DESTINATION /etc/grok-patterns)
	install(FILES /var/lib/alligator/parseJks.so DESTINATION /var/lib/alligator)
	install(FILES /var/lib/alligator/puppeteer-alligator.js DESTINATION /var/lib/alligator)
	install(FILES build/alligator
		PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
		DESTINATION /usr/bin)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

install(DIRECTORY DESTINATION /var/lib/alligator/)

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION	/usr/bin
							/usr/lib/systemd
							/usr/lib/systemd/system)

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "alligator is aggregator for system and software metrics")
set(CPACK_RPM_PACKAGE_DESCRIPTION "alligator is aggregator for system and software metrics")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/../LICENSE")
set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
include(CPack)
