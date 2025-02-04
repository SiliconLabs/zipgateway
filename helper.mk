#!/usr/bin/make -f
# -*- makefile -*-
# ex: set tabstop=4 noexpandtab:
# -*- coding: utf-8 -*

project?=zipgateway

default: help all
	@date

SELF?=${CURDIR}/helper.mk
sudo?=sudo
build_dir?=build

# TODO: adapt for arm and support 64bits
target_debian_arch?=$(shell dpkg --print-architecture || echo 'i386')

packages?=make cmake time file git sudo
packages+=build-essential pkg-config bison flex python
packages+=libusb-1.0-0-dev libssl-dev libxml2-dev libjson-c-dev
packages+=doxygen xsltproc plantuml roffit
packages+=radvd parprouted bridge-utils net-tools zip unzip
# TODO: https://www.tcpdump.org/release/libpcap-1.5.3.tar.gz

# libzwaveip deps
packages+=libbsd-dev libncurses5-dev libavahi-client-dev

cmake?=cmake
ctest?=ctest
#cmake_options?=-DENABLE_CODE_COVERAGE=True
cmake_options+=-DCMAKE_VERBOSE_MAKEFILE=True
cmake_options+=-DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=${target_debian_arch}

PLANTUML_JAR_PATH?=/usr/share/plantuml/plantuml.jar
export PLANTUML_JAR_PATH

exe?=${CURDIR}/${build_dir}/files/zipgateway
# exe_args?=--help

all_rules?=all tests dist

help: ${SELF}
	cat README.md
	@echo "# Available rules at your own risk"
	@echo ""
	@grep -o '^[^ ]*:' ${SELF} | grep -v '\$$' | grep -v '^#' | grep -v '^\.' | grep -v "=" | grep -v '%'
	@echo "# PATH=${PATH}"

dist: ${build_dir}/Makefile
	${MAKE} -C ${<D} package_source
	${MAKE} -C ${<D} package
	${MAKE} -C ${<D} doc_zip


cleanall:
	@echo "# TODO: https://www.gnu.org/software/make/manual/make.html#Standard-Targets"

prepare:
	git submodule update --init --recursive

configure: ${build_dir}/CMakeCache.txt
	ls $<

${build_dir}/CMakeCache.txt ${build_dir}/Makefile: CMakeLists.txt
	mkdir -p ${@D}
	${cmake} --version
	${cmake} --help
	git describe --tags --always --dirty 
	cd ${@D} && ${cmake} ${cmake_options} ${CURDIR}
	ls ${CURDIR}/$@

.PHONY: build

build: ${build_dir}/CMakeCache.txt
	${cmake} --build ${build_dir}

all: ${all_rules}
	date -u

setup/debian/stretch: /etc/apt/sources.list /etc/os-release
	grep 'VERSION="9 (stretch)"' /etc/os-release
	${sudo} sed -e 's|\(http://\)\(.*\)\(.debian.org\)|\1archive\3|g' -i $<
	${sudo} sed -e 's|stretch-updates|stretch-proposed-updates|g' -i $<
	echo "deb http://archive.debian.org/debian stretch-backports main contrib non-free" \
| ${sudo} tee "$<.d/backports.list"
	${sudo} apt-get update
	${sudo} apt-get install -y ${packages}

setup/debian/buster: /etc/apt/sources.list /etc/os-release
	grep 'VERSION="10 (buster)"' /etc/os-release
#	${sudo} sed -e 's|\(http://\)\(.*\)\(.debian.org\)|\1archive\3|g' -i $<
#	${sudo} sed -e 's|stretch-updates|stretch-proposed-updates|g' -i $<
#	echo "deb http://archive.debian.org/debian stretch-backports main contrib non-free" \
| ${sudo} tee "$<.d/backports.list"
	${sudo} apt-get update
	${sudo} apt-get install -y ${packages}

setup/debian: setup/debian/stretch
	echo "# $@"

setup: setup/debian
	echo "# $@"

setup/raspbian/stretch: /etc/os-release /etc/apt/sources.list.d/raspi.list
	grep 'Raspberry Pi' /proc/device-tree/model
	grep 'ID=raspbian' $<
	${sudo} sed -e 's|raspbian.raspberrypi.org|legacy.raspbian.org|g' \
-i /etc/apt/sources.list -i /etc/apt/sources.list.d/*.list

setup/raspbian: setup/raspbian/stretch setup/debian/stretch

/etc/apt/sources.list:
	@echo "error: Distro unsuported please use debian"
	exit 1

install: ${build_dir}
	make -C $< V=1 install

/usr/local/sbin/zipgateway /etc/init.d/zipgateway: install
	ls $@

init/%: /etc/init.d/zipgateway
	$< ${@F}

run: /usr/local/sbin/zipgateway
	@echo "# log: $@ $<"
	-$< --help
	file $<
	$< ${exe_args}

start: /etc/init.d/zipgateway
	@echo "log: $@ $< (setup before start)"
	$< restart
	sleep 1
	$< stop
	${SELF} run

tests: ${build_dir}
	cd ${build_dir} && ctest V=1

docker-compose/up: docker-compose.yml
	docker version
	docker-compose --version
	docker-compose up --build

docker/run: docker-compose/up
