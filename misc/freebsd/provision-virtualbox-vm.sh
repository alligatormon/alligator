#!/bin/sh
# One-time setup inside the base FreeBSD VM before taking a VirtualBox snapshot.
# Run as root (or via sudo). See doc/ci-freebsd-virtualbox.md
set -eu

export ASSUME_ALWAYS_YES=YES

pkg update
pkg install -y \
	git \
	cmake \
	gmake \
	python3 \
	py311-pip \
	autoconf \
	automake \
	libtool \
	pkgconf \
	llvm \
	go \
	ruby \
	bash \
	sudo

if ! pw usershow gitlab >/dev/null 2>&1; then
	pw useradd gitlab -m -s /usr/local/bin/bash
	echo 'gitlab ALL=(ALL) NOPASSWD:ALL' >>/usr/local/etc/sudoers.d/gitlab
	chmod 440 /usr/local/etc/sudoers.d/gitlab
fi

# GitLab Runner connects over SSH (see VirtualBox executor docs).
sysrc sshd_enable=YES
service sshd restart

python3 -m pip install --user --upgrade pip
su - gitlab -c 'python3 -m pip install --user conan==2.26.1'

echo "Base VM ready. Shut down cleanly, then snapshot as 'clean' in VirtualBox."
