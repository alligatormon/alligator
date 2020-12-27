TEST=false
[ ! -z "$1" ] && TEST=$1
mkdir external

yum -y install rpm-devel pcre-static libuv-static systemd-devel nc mariadb-server mariadb-devel postgresql-server postgresql-pgpool-II postgresql-devel postgresql-static pgbouncer mysql-proxy-devel mysql-proxy https://repo.ius.io/ius-release-el7.rpm sudo java-latest-openjdk-devel jq nsd nmap-ncat unbound python3-pip gcc glibc-static wget cmake3 rpmdevtools redhat-rpm-config epel-rpm-macros createrepo libpqxx-devel gcc-c++ make

unbound-control-setup

cd external
git clone git://git.netfilter.org/iptables
cd iptables
git checkout v1.8.6
make -j install
./autogen.sh
./configure --disable-nftables --enable-static
make -j
make -j install
cd ../../

cd external
git clone https://github.com/ARMmbed/mbedtls.git
cd mbedtls
git checkout mbedtls-2.15.1
make -j install
cd ../../

cd external
git clone https://github.com/pantoniou/libfyaml.git
cd libfyaml
./bootstrap.sh
./configure
make
make -j install
cd ../../

cd external
ls jansson-2.12.tar.gz || wget http://www.digip.org/jansson/releases/jansson-2.12.tar.gz
tar xfvz jansson-2.12.tar.gz
cd jansson-2.12
./configure
make -j
make -j install
cd ../../

make -j install
cd external
git clone https://github.com/Rupan/libatasmart.git
cd libatasmart/
./autogen.sh
./configure --enable-static
make -j
make -j install
cd ../../

#rpm -i external/zookeeper-native-3.4.5+cdh5.14.2+142-1.cdh5.14.2.p0.11.1.osg34.el7.x86_64.rpm
yum -y install https://t2.unl.edu/osg/3.4/el7/rolling/x86_64/zookeeper-native-3.4.5+cdh5.14.2+142-1.cdh5.14.2.p0.11.1.osg34.el7.x86_64.rpm
cp ../misc/bintray-apache-couchdb-rpm.repo /etc/yum.repos.d/
cp ../misc/datastax.repo /etc/yum.repos.d/
cp ../misc/mongodb.repo /etc/yum.repos.d/
cp ../misc/rabbitmq38.repo /etc/yum.repos.d/
cp ../misc/elasticsearch.repo /etc/yum.repos.d/
rpm --import https://repo.clickhouse.tech/CLICKHOUSE-KEY.GPG
yum-config-manager --add-repo https://repo.clickhouse.tech/rpm/stable/x86_64

#yum -y install couchdb
#yum -y install dsc20
#yum -y install mongodb-org-server mongodb-org mongodb-org-shell
#yum -y install rabbitmq-server

cd external
git clone https://github.com/mongodb/mongo-c-driver.git
cd mongo-c-driver/
git checkout 1.16.2
make -j install
if [ $? -ne 0 ]
then
	python build/calc_release_version.py > VERSION_CURRENT
	mkdir cmake-build
	cd cmake-build
	cmake3 -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF ..
	make -j install
	cd ../
fi
cd ../../

cd external
git clone https://github.com/jemalloc/jemalloc.git
cd jemalloc
./autogen.sh
make -j install
cd ../../

pip3 install statsd
if [ $TEST == "true" ]
then
	yum -y install elasticsearch monit syslog-ng rsyslog unbound varnish nginx rabbitmq-server haproxy gearmand uwsgi redis beanstalkd openssl11-libs openssl11-devel openssl11-static openssl11 rabbitmq-server clickhouse-server zookeeper gearmand python3 python3-pip ragel-devel userspace-rcu-devel libsodium-devel nginx varnish uwsgi-plugin-python36 squid lighttpd httpd24u syslog-ng
	rpm -i external/couchbase-server-community-6.5.1-centos7.x86_64.rpm
	pip3 install https://github.com/mher/flower/zipball/master

	cd external
	git clone https://github.com/gdnsd/gdnsd.git
	cd gdnsd/
	ls
	autoreconf -vif
	./configure
	make -j install
	cd ../../

	cd external
	wget https://releases.hashicorp.com/consul/1.8.4/consul_1.8.4_linux_amd64.zip
	unzip consul_1.8.4_linux_amd64.zip
	cp -a consul /usr/bin
	cd ..

	cd external/memcached-1.5.16/
	make install
	cd ..

	yum install https://github.com/nats-io/nats-server/releases/download/v2.1.9/nats-server-v2.1.9-amd64.rpm
fi
