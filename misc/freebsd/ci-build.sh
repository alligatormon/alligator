#!/bin/sh
# Release build for FreeBSD (GitHub Actions, GitLab job, or manual).
# Expects repo root as CWD.
set -eu

export ASSUME_ALWAYS_YES=YES
export PATH="${HOME}/.local/bin:${PATH}"

pkg update
pkg install -y \
	git \
	cmake \
	gmake \
	python3 \
	devel/py-pip \
	autoconf \
	automake \
	libtool \
	pkgconf \
	llvm \
	go \
	ruby \
	bash

python3 -m pip install --user --upgrade pip
python3 -m pip install --user conan==2.26.1

# vmactions copies the GitHub checkout into the VM as a different uid than root.
git config --global --add safe.directory '*'
git submodule sync --recursive
git submodule update --init --recursive
cd src

conan profile detect --force
conan install . --build=missing -s build_type=Release
conan build external/

cmake -S . -B build \
	-DCMAKE_TOOLCHAIN_FILE=build/Release/generators/conan_toolchain.cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_MAKE_PROGRAM=gmake
cmake --build build -j"$(sysctl -n hw.ncpu)"
./build/alligator --version
cd build && cpack -G FreeBSD
ls -la alligator-*.pkg 2>/dev/null || ls -la *.pkg
