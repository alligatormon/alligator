#!/bin/sh
# Release build for FreeBSD (GitHub Actions, GitLab job, or manual).
# Expects repo root as CWD.
set -eu

export ASSUME_ALWAYS_YES=YES
export PATH="/usr/local/bin:${HOME}/.local/bin:${PATH}"
export MAKE=gmake

pkg update
pkg install -y \
	git \
	cmake \
	gmake \
	python3 \
	autoconf \
	automake \
	libtool \
	pkgconf \
	flex \
	bison \
	m4 \
	llvm \
	go \
	ruby \
	bash
PYVER=$(python3 -c 'import sys; print("%d%d" % (sys.version_info.major, sys.version_info.minor))')
pkg install -y "py${PYVER}-pip" "py${PYVER}-sqlite3"

# GNU m4 from pkg is /usr/local/bin/gm4; base /usr/bin/m4 is BSD and
# fails Conan autoconf ("need GNU m4 1.4 or later").
if [ -x /usr/local/bin/gm4 ]; then
	export M4=/usr/local/bin/gm4
	if [ ! -e /usr/local/bin/m4 ]; then
		ln -s gm4 /usr/local/bin/m4
	fi
fi

python3 -m pip install --user --upgrade pip
python3 -m pip install --user conan==2.26.1

# vmactions copies the GitHub checkout into the VM as a different uid than root.
git config --global --add safe.directory '*'
git submodule sync --recursive
git submodule update --init --recursive
cd src

conan profile detect --force
conan install . --build=missing -s build_type=Release \
	-pr:a default -pr:a ../misc/freebsd/conan-platform.profile
conan build external/

cmake -S . -B build \
	-DCMAKE_TOOLCHAIN_FILE=build/Release/generators/conan_toolchain.cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_MAKE_PROGRAM=gmake
cmake --build build -j"$(sysctl -n hw.ncpu)"
./build/alligator --version
cd build && cpack -G FREEBSD
ls -la alligator-*.pkg 2>/dev/null || ls -la *.pkg
