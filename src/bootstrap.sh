export DEBIAN_FRONTEND=noninteractive
apt update
apt -y install software-properties-common
apt -y install gpg
apt -y install lsb-release
yum -y update ca-certificates
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Debian && echo 'deb http://ppa.launchpad.net/ansible/ansible/ubuntu trusty main' > /etc/apt/sources.list.d/ansible.list
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Debian && apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 93C4A3FD7BB9C367
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Debian && echo 'deb http://ftp.debian.org/debian stretch-backports main' > /etc/apt/sources.list.d/java.list
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Ubuntu && add-apt-repository -y ppa:openjdk-r/ppa
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Ubuntu && add-apt-repository --yes --update ppa:ansible/ansible
apt update
stat /usr/bin/yum || alias yum=apt
yum -y install epel-release
yum -y install ansible
pkg install -y py38-ansible
export PIP_BREAK_SYSTEM_PACKAGES=1
ansible-playbook --connection=local --inventory 127.0.0.1, playbook.yml
cmake -version

#conan install . --build=missing -s build_type=Debug
conan install . --build=missing
#CONAN_DISABLE_CHECK_COMPILER=True conan install . --build=missing -s compiler.version=5
lsb_release -c | awk -F: '{print $2}' | awk '{print $1}' | grep stretch && conan install ../misc/debian9/ --build=missing && apt -y install libpcre3-dev
uname -s | grep FreeBSD && conan install ../misc/freebsd/ --build=missing
conan build external/
