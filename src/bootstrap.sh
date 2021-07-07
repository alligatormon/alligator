#!/bin/sh

TEST=false
[ ! -z "$1" ] && TEST=$1
mkdir external

apt || ln -s /usr/bin/apt-get  /usr/bin/apt
apt update
export DEBIAN_FRONTEND=noninteractive
apt install -y g++ gcc  libudev-dev
apt install -y cmake
apt install -y libuv1-dev
apt install -y automake
apt install -y autoconf
apt install -y wget
apt install -y libtool
apt install -y libnftnl-dev
apt install -y libmongoc-dev
apt install -y libbson-dev
ln -s /usr/bin/make /usr/bin/gmake
apt install -y libpcre3-dev python3 libpq-dev m4 curl libzookeeper-mt-dev libatasmart4 libjemalloc-dev uuid-dev libghc-regex-pcre-dev vim python3-pip libtool-bin valgrind netcat
apt install -y libmysqlclient-dev
apt -y install libiptc-dev
apt -y install git
apt -y install libpq-dev
apt -y install python3-pip
apt -y install libzookeeper-st-dev
apt -y install libzookeeper-mt-dev
apt -y install vim
apt -y install default-libmysqlclient-dev
apt -y install libsnappy-dev
apt install -y openjdk-14-source || apt install -y openjdk-11-source
yum -y install epel-release https://osdn.net/projects/cutter/storage/centos/cutter-release-1.3.0-1.noarch.rpm

echo 'skip_if_unavailable=true' >> /etc/yum.repos.d/cutter.repo

yum -y install https://repo.ius.io/ius-release-el7.rpm
yum -y install vim
yum -y install snappy-devel
ln -sf /usr/bin/vim /usr/bin/vi
yum -y install rpm-devel systemd-devel nc mariadb-server mariadb-devel postgresql-server postgresql-devel postgresql-static pgbouncer sudo java-latest-openjdk-devel jq nsd nmap-ncat unbound python3-pip gcc wget cmake3 rpmdevtools redhat-rpm-config epel-rpm-macros createrepo gcc-c++ make git libtool libuuid-devel valgrind pcre-devel libbson-devel cyrus-sasl-devel libicu-devel libzstd-devel libev-devel libevent-devel libxslt
yum -y install cutter pcre-static libuv-static postgresql-pgpool-II mysql-proxy-devel mysql-proxy glibc-static libpqxx-devel netcat

ln -s /usr/bin/python{3,}
ln -s /usr/bin/cmake{3,}

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

		cp ../misc/libpcre.a /usr/lib
		cp ../misc/libbson-static-1.0.a /usr/lib
	fi
        if [ `echo $VERSION_ID | awk -F\. '{print $1}'` -le 16 ]; then
		apt -y install software-properties-common
		add-apt-repository -y ppa:openjdk-r/ppa
		apt-get update
		apt install -y openjdk-11-jdk

        apt install -y software-properties-common
        add-apt-repository ppa:deadsnakes/ppa
        apt update
        apt -y install python3.8*
        ln -sf /usr/bin/pip3 /usr/bin/vi
        ln -sf /usr/bin/python3.8 /usr/bin/python3

        curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py
        python3 get-pip.py
	fi
        if [ `echo $VERSION_ID | awk -F\. '{print $1}'` -le 14 ]; then
		apt -y install software-properties-common
		add-apt-repository -y ppa:george-edison55/cmake-3.x
		apt-get update
		apt install -y cmake
		apt install -y make
		#curl -sSL https://cmake.org/files/v3.5/cmake-3.5.2-Linux-x86_64.tar.gz | sudo tar -xzC /opt
	fi
fi

echo $NAME | grep Debian
if [ $? -eq 0 ]; then
        if [ `echo $VERSION_ID | awk -F\. '{print $1}'` -le 9 ]; then
		echo 'deb http://ftp.debian.org/debian stretch-backports main' | tee /etc/apt/sources.list.d/stretch-backports.list
		apt update
		apt install -y openjdk-11-jdk
		ln -s /usr/lib/x86_64-linux-gnu /usr/lib64
		apt install -y libicu-dev libzstd-dev libssl-dev libsasl2-dev

		#cd external
		#git clone https://github.com/mongodb/mongo-c-driver.git
		#cd mongo-c-driver
		#git checkout 1.17.5
		#mkdir cmake-build
		#python3 build/calc_release_version.py > VERSION_CURRENT
		#cd cmake-build
		#cmake -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF ..
		#make -j install
		#make -j install
		#cd -
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

ln -s /usr/bin/python3 /usr/bin/python

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

#cd external
#git clone https://github.com/ARMmbed/mbedtls.git
#cd mbedtls
#git checkout mbedtls-2.15.1
#make clean
#make -j install
#cd ../../

cd external
git clone https://github.com/pantoniou/libfyaml.git
cd libfyaml
git pull
./bootstrap.sh
./configure
make
make -j install
cd ../../

#cd external
#ls jansson-2.12.tar.gz || wget http://www.digip.org/jansson/releases/jansson-2.12.tar.gz
#tar xfvz jansson-2.12.tar.gz
#cd jansson-2.12
#./configure
#make -j
#make -j install
#cd ../../

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
cp ../misc/datastax.repo /etc/yum.repos.d/
cp ../misc/mongodb.repo /etc/yum.repos.d/
cp ../misc/elasticsearch.repo /etc/yum.repos.d/
rpm --import https://repo.clickhouse.tech/CLICKHOUSE-KEY.GPG
yum-config-manager --add-repo https://repo.clickhouse.tech/rpm/stable/x86_64

#yum -y install dsc20
#yum -y install mongodb-org-server mongodb-org mongodb-org-shell

ln -s /usr/bin/python{3,}
cd external
git clone https://github.com/mongodb/mongo-c-driver.git
cd mongo-c-driver/
git checkout 1.17.5
make clean
python build/calc_release_version.py > VERSION_CURRENT
mkdir cmake-build
cd cmake-build
cmake -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF ..
make install
cd ../
cd ../../

cd external
git clone https://github.com/jemalloc/jemalloc.git
cd jemalloc
git pull
./autogen.sh
make -j install
cd ../../

pip3 install statsd
pip3 install conan

conan install . --build=missing
yum -y update libarchive

echo "TEST is $TEST"
if [ ${TEST} == "true" ]
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

	yum -y install https://github.com/nats-io/nats-server/releases/download/v2.1.9/nats-server-v2.1.9-amd64.rpm

	cd external
		wget -O aerospike.tgz 'https://www.aerospike.com/download/server/latest/artifact/el7'
		tar -xvf aerospike.tgz
		rpm -i aerospike-server-community-*-el7/aerospike-server-community-*.el7.x86_64.rpm
	cd ..
fi
