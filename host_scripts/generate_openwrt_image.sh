#!/bin/bash

# generate_openwrt_images.sh - Generates an openwrt image with the zipgateway and dependencies

SCRIPT_NAME=generate_openwrt_images.sh

##### Functions
print_usage()
{
    echo "$SCRIPT_NAME <imagebuilder_root> <openwrt_profile> <target_files> <output_path>"
}

check_parameters()
{
    if [ "$#" -ne 4 ]; then
        echo "Illegal number of parameters"
        print_usage
        exit 2
    fi
}

check_parameters $@
IMAGE_BUILDER_ROOT=`realpath $1`
OPENWRT_PROFILE=$2
TARGET_FILES=`realpath $3`
OUTPUT_PATH=`realpath $4`

echo IMAGE_BUILDER_ROOT: $IMAGE_BUILDER_ROOT
echo TARGET_FILES: $TARGET_FILES
echo OPENWRT_PROFILE: $OPENWRT_PROFILE

##### Override OpenWRT default bridge setup to support zipgateway IPv6 forwarding

mkdir -p $TARGET_FILES/etc/config/
echo "config interface 'loopback'
	option ifname 'lo'
	option proto 'static'
	option ipaddr '127.0.0.1'
	option netmask '255.0.0.0'
config globals 'globals'
	option ula_prefix 'fde0:6347:b3c2::/48'
config interface 'lan'
	option type 'bridge'
	option ifname 'eth0'
	option proto 'static'
	option ipaddr '192.168.1.1'
	option netmask '255.255.255.0'
	option ip6assign '60'
#config interface 'wan'
#	option ifname 'eth1'
#	option proto 'dhcp'
#config interface 'wan6'
#	option ifname 'eth1'
#	option proto 'dhcpv6'" > $TARGET_FILES/etc/config/network

echo "net.ipv6.conf.br-zip.forwarding=1
net.ipv6.conf.br-zip.accept_ra=2" > $TARGET_FILES/etc/sysctl.conf

##### Build OpenWRT Image
make -C $IMAGE_BUILDER_ROOT image \
    PROFILE=$OPENWRT_PROFILE \
    FILES=$TARGET_FILES \
    BIN_DIR=$OUTPUT_PATH \
    PACKAGES="base-files busybox dnsmasq dropbear firewall fstools fwtool\
              hostapd-common ip6tables iptables iw iwinfo jshn jsonfilter \
              kernel kmod-ath kmod-ath9k kmod-ath9k-common kmod-cfg80211 \
              kmod-gpio-button-hotplug kmod-ip6tables kmod-ipt-conntrack \
              kmod-ipt-core kmod-ipt-nat kmod-lib-crc-ccitt kmod-mac80211 \
              kmod-nf-conntrack kmod-nf-conntrack6 kmod-nf-ipt kmod-nf-ipt6 \
              kmod-nf-nat kmod-nf-reject kmod-nf-reject6 kmod-nls-base \
              kmod-ppp kmod-pppoe kmod-pppox kmod-slhc kmod-usb-core \
              kmod-usb-ehci kmod-usb2 libblobmsg-json libc libgcc libip4tc \
              libip6tc libiwinfo libiwinfo-lua libjson-c libjson-script liblua \
              liblucihttp liblucihttp-lua libnl-tiny libpthread libubox libubus \
              libubus-lua libuci libuclient libxtables logd lua luci \
              luci-app-firewall luci-base luci-lib-ip luci-lib-jsonc \
              luci-lib-nixio luci-mod-admin-full luci-proto-ipv6 luci-proto-ppp \
              luci-theme-bootstrap mtd netifd odhcp6c odhcpd-ipv6only \
              openwrt-keyring opkg ppp ppp-mod-pppoe procd rpcd rpcd-mod-rrdns \
              swconfig uboot-envtools ubox ubus ubusd uci uclient-fetch uhttpd \
              usign wireless-regdb wpad-mini kmod-usb-acm kmod-tun libpcap \
              libusb-1.0 zip unzip coreutils-install coreutils-mktemp bash"
