#!/bin/sh

TEST=false
[ ! -z "$1" ] && TEST=$1
mkdir external

apt update
export DEBIAN_FRONTEND=noninteractive
apt install -y cmake g++ gcc libuv1-dev libudev-dev
ln -s /usr/bin/make /usr/bin/gmake
apt install -y libpcre3-dev python3 libbson-dev libmongoc-dev libpq-dev m4 wget curl libzookeeper-mt-dev autoconf libatasmart4 libjemalloc-dev libiptc-dev libtool libnftnl-dev uuid-dev libghc-regex-pcre-dev libmysqlclient-dev vim python3-pip libtool-bin libtool git valgrind netcat
apt install -y openjdk-14-source || apt install -y openjdk-11-source
yum -y install epel-release https://osdn.net/projects/cutter/storage/centos/cutter-release-1.3.0-1.noarch.rpm

echo 'skip_if_unavailable=true' >> /etc/yum.repos.d/cutter.repo

yum -y install https://repo.ius.io/ius-release-el7.rpm
yum -y install rpm-devel systemd-devel nc mariadb-server mariadb-devel postgresql-server postgresql-devel postgresql-static pgbouncer sudo java-11-openjdk-devel jq nsd nmap-ncat unbound python3-pip gcc wget cmake3 rpmdevtools redhat-rpm-config epel-rpm-macros createrepo gcc-c++ make git libtool libuuid-devel valgrind pcre-devel libbson-devel cyrus-sasl-devel libicu-devel libzstd-devel libev-devel libevent-devel
yum -y install cutter pcre-static libuv-static postgresql-pgpool-II mysql-proxy-devel mysql-proxy glibc-static libpqxx-devel netcat

ln -s /usr/bin/python{3,}

unbound-control-setup

. /etc/os-release

echo $NAME | grep Ubuntu
if [ $? -eq 0 ]; then
        if [ `echo $VERSION_ID | awk -F\. '{print $1}'` -le 19 ]; then
		cd external
		git clone https://github.com/libuv/libuv.git
		cd libuv
		./autogen.sh
		./configure --enable-static
		make -j install
		cd ../../

		cp ../misc/libbson-static-1.0.a /usr/lib
	fi
fi

echo $NAME | grep "CentOS Linux"
if [ $? -eq 0 ]; then
        if [ `echo $VERSION_ID | awk -F\. '{print $1}'` -ge 8 ]; then
		cd external
		git clone https://github.com/libuv/libuv.git
		cd libuv
		./autogen.sh
		./configure --enable-static
		make -j install
		cd ../../

		cp ../misc/libpcre.a /usr/lib
		cp ../misc/libbson-static-1.0.a /usr/lib
	fi
fi

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
make clean
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
cp ../misc/cloudera-cdh5.repo /etc/yum.repos.d/
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
make clean
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
make clean
./autogen.sh
make -j install
cd ../../

pip3 install statsd
if [ $TEST == "true" ]
then
	yum -y install elasticsearch monit syslog-ng rsyslog unbound varnish nginx rabbitmq-server haproxy gearmand uwsgi redis beanstalkd openssl11-libs openssl11-devel openssl11-static openssl11 rabbitmq-server clickhouse-server zookeeper gearmand python3 python3-pip ragel-devel userspace-rcu-devel libsodium-devel nginx varnish uwsgi-plugin-python36 squid lighttpd httpd24u syslog-ng hadoop-hdfs xinetd tftp-server manticore
	rpm -i external/couchbase-server-community-6.5.1-centos7.x86_64.rpm
	pip3 install https://github.com/mher/flower/zipball/master
	pip3 install Celery==4.4.0

	yum -y install https://github.com/sysown/proxysql/releases/download/v2.0.16/proxysql-2.0.16-1-centos7.x86_64.rpm

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
	cd ../..

	yum install https://github.com/nats-io/nats-server/releases/download/v2.1.9/nats-server-v2.1.9-amd64.rpm
fi
