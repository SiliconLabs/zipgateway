#! /usr/bin/env bash
# -*- mode: Bash; tab-width: 2; indent-tabs-mode: nil; coding: utf-8 -*-
# vim:shiftwidth=4:softtabstop=4:tabstop=4:
# SPDX-License-Identifier: LicenseRef-MSLA
# SPDX-FileCopyrightText: Silicon Laboratories Inc. https://www.silabs.com

set -e
set -x

sudo="sudo"
project="zipgateway"
debian_suite="stretch"
debian_mirror_url="http://archive.debian.org/debian"
qemu_system="qemu-system-arm"
qemu_file="/usr/bin/${qemu_system}"
target_debian_arch="armhf"
machine="${project}-${debian_suite}-${target_debian_arch}"
rootfs_dir="/var/tmp/var/lib/machines/${machine}"
MAKE=/usr/bin/make
CURDIR="$PWD"
chroot="systemd-nspawn"

${sudo} apt-get update
${sudo} apt install -y \
        binfmt-support \
        debian-archive-keyring \
        debootstrap \
        qemu-system-arm \
        qemu-user-static \
        systemd-container \
        time

${sudo} update-binfmts --enable qemu-arm

if [ ! -d "${rootfs_dir}" ] ; then
    ${sudo} mkdir -pv "${rootfs_dir}"
    time ${sudo} debootstrap \
         --arch="${target_debian_arch}" \
	       "${debian_suite}" "${rootfs_dir}" "${debian_mirror_url}"
    ${sudo} chmod -v u+rX "${rootfs_dir}"
fi

case ${chroot} in
    "systemd-nspawn")
        rootfs_shell="${sudo} systemd-nspawn \
 --bind "${qemu_file}" \
 --directory="${rootfs_dir}" \
 --machine="${machine}" \
 --bind="${CURDIR}:${CURDIR}" \
"
        ;;
    *)
        rootfs_shell="${sudo} chroot ${rootfs_dir}"
        l="dev dev/pts sys proc"
        for t in $l ; do $sudo mkdir -p "${rootfs_dir}/$t" && $sudo mount --bind "/$t" "${rootfs_dir}/$t" ; done
    ;;
esac

${rootfs_shell} \
    apt-get install -y -- make sudo util-linux


${rootfs_shell}	\
    ${MAKE} \
    --directory="${CURDIR}" \
    --file="${CURDIR}/helper.mk" \
    HOME="${HOME}" \
    USER="${USER}" \
    -- \
    help setup prepare configure all dist \
    target_debian_arch="${target_debian_arch}"

find "${CURDIR}/" -iname "*.deb"

sudo du -hs "/var/tmp/var/lib/machines/${machine}"

