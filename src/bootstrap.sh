ln -s . /app
apt update
apt -y install software-properties-common gpg
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Debian && echo 'deb http://ppa.launchpad.net/ansible/ansible/ubuntu trusty main' > /etc/apt/sources.list.d/ansible.list
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Debian && apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 93C4A3FD7BB9C367
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Debian && echo 'deb http://ftp.debian.org/debian stretch-backports main' > /etc/apt/sources.list.d/java.list
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Ubuntu && add-apt-repository -y ppa:openjdk-r/ppa
lsb_release --id | awk -F: '{print $2}' | awk '{print $1}' | grep Ubuntu && add-apt-repository --yes --update ppa:ansible/ansible
apt update
stat /usr/bin/yum || alias yum=apt
yum -y install epel-release
yum -y install ansible
ansible-playbook --connection=local --inventory 127.0.0.1, playbook.yml

conan install . --build=missing
lsb_release -c | awk -F: '{print $2}' | awk '{print $1}' | grep stretch && conan install ../misc/debian9/ --build=missing && apt -y install libpcre3-dev
conan install
conan build external/
