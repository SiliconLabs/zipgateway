# Z/IP Gateway

The Z/IP gateway is a free reference design intended for development and demonstration purposes only. It is provided as is, with no warranties. Customers are advised to conduct security and compliance testing of all gateways during the product design process.

> [!WARNING] 
> The Z/IP Gateway solution is in maintenance mode. To access a maintained Z-Wave gateway solution supporting the latest protocol features, refer to the [Unify SDK](https://github.com/SiliconLabs/UnifySDK) project.

## LICENSING

Z/IP Gateway is covered the [Master Software License Agreement (MSLA)](https://www.silabs.com/about-us/legal/master-software-license-agreement), which applies unless otherwise noted. Refer to [LICENSE](./LICENSE) for more details.

## HOW TO USE Z/IP GATEWAY

Z/IP Gateway reference setup is a Raspberry Pi running raspbian-9 OS (EoL).

To get started with Z/IP gateway:
- Retrieve the image from the link below and flash it to an SD card : http://downloads.raspberrypi.org/raspbian_lite/images/raspbian_lite-2019-04-09/2019-04-08-raspbian-stretch-lite.zip
- Prepare your system with the required dependencies:
```sh
sudo sed -e 's|raspbian.raspberrypi.org|legacy.raspbian.org|g' \
            -i /etc/apt/sources.list \
            -i /etc/apt/sources.list.d/*.list

sudo apt-get update
sudo apt-get install -y etckeeper # Will track changes in /etc
```

- Set up a Z-Wave NCP controller. Refer to [Z-Wave online documentation](https://docs.silabs.com/z-wave/7.22.1/zwave-getting-started-overview/). An alternative option is to download an NCP controller firmware
["zwave_ncp_serial_api_controller-${board}-${region}.hex"](https://github.com/SiliconLabs/gecko_sdk/releases#demo_application.zip)
and deploy it using
[Simplicity Commander](https://www.silabs.com/documents/public/software/SimplicityCommander-Linux.zip).
- Connect the controller to a USB port. Check the proper detection using:

```sh
ls /dev/serial/by-id/usb-Silicon_Labs_*
```

- Download the Z/IP Gateway deb package from the release page: [https://github.com/SiliconLabs/zipgateway/releases](https://github.com/SiliconLabs/zipgateway/releases)
- Copy it to the target device and install it along its dependencies. Finally, configure the daemon accordingly.

```sh
sudo dpkg -i zipgateway-*-Linux-armhf.deb || sudo apt install -f # To install missing deps
sudo apt --fix-broken install # Will resume installing package if needed
sudo dpkg -L zipgateway # List server and client and other utilities
cat /usr/local/etc/zipgateway.cfg # To check configuration file
```

- Use Z/IP Gateway with libzwaveip's reference client (part of the package).

```sh
# Check if daemon is running
$ systemctl status zipgateway # should report service active
$ tail -F /usr/log/zipgateway.log # To see traces

# Connect to daemon using client, and use interactive shell:
$ reference_client # Will report usage

libzwaveip version
Logging to "/tmp/libzw_reference_client.log"

Usage: reference_client [-p pskkey] [-n] [-x zwave_xml_file] [-g logging file path] [-u UI message severity level] [-f logging severity filter level] -s ip_address
(...)

$ reference_client -s fd00:aaaa::3 -p 123456789012345678901234567890AA -g ~/reference_client.log
(ZIP) help
Usage: help [addnode|removenode|learnmode|acceptdsk|grantkeys|setdefault|list|nodeinfo|hexsend|send|pl_list|pl_add|pl_remove|pl_reset|identify|lifeline|bye|exit|quit|]
(...)

(ZIP) list
"Static Controller [FFFFFFFF-0001-000]" IP:42.42.42.1

(ZIP) setdefault
data: 0x4D datalen: 3
Transmit OK
cmd_class:  COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC  v2
cmd:  DEFAULT_SET_COMPLETE
(...)
```


To interact with another device, flash an end device with a Z-Wave application
(eg: zwave_soc_switch_on_off-brd4202a-eu.hex), and then included it to your Z-Wave network.


```
(ZIP) addnode
cmd_class:  COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION  v4
cmd:  NODE_ADD_KEYS_REPORT
(...)
Enter 'grantkeys' to accept or 'abortkeys' to cancel.

(ZIP) acceptdsk 424242
cmd_class:  COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION  v4
cmd:  NODE_ADD_STATUS
(...)
param:  Status  >
         NODE_ADD_STATUS_DONE
(...)
Inclusion done

(ZIP) list
(...)
"Switch Binary [FFFFFFFF-0002-000]" IP:42.42.42.2

(ZIP) send "Switch Binary [FFFFFFFF-0002-000]" COMMAND_CLASS_SWITCH_BINARY SWITCH_BINARY_SET ff
(...)

(ZIP) send "Switch Binary [FFFFFFFF-0002-000]" COMMAND_CLASS_SWITCH_BINARY SWITCH_BINARY_GET
(...)
bytestream: 25 03 ff ff 00

(ZIP) send "Switch Binary [FFFFFFFF-0002-000]" COMMAND_CLASS_SWITCH_BINARY SWITCH_BINARY_SET 00
(...)
bytestream: 25 03 00 00 00

```

## HOW TO BUILD Z/IP GATEWAY

Only 32-bit platforms are currently supported.

The reference OS is currently debian-9 (EoL). Native build is supported using cmake. You can rely on helper scripts to set up a system and pass tests to generate a debian package ready to be installed.

### NATIVE BUILD ON TARGET DEVICE

To build Z/IP Gateway, execute the following command.

```sh
sudo apt install make
./helper.mk help
./helper.mk setup/raspbian
./helper.mk
```

For the record, dependencies are listed in helper.mk and the
compilation relies on cmake using standard directives:

```sh
mkdir build
cmake ..
cmake --build .
```

Feel free to tweak env, debugging using gdb can be helpful too.

### BUILD ON HOST

To speed up the build process, native build can be deported to the host using
different containerization techniques (docker, systemd, chroot, qemu, binfmt).
Check the [DevTools](./DevTools/) directory for more information.

## MORE

Additional documentation is available in doc folder or online:

- https://github.com/SiliconLabs/zipgateway/
- https://www.silabs.com/wireless/z-wave/specification
- https://docs.silabs.com/z-wave/1.0.1/version-history
- https://www.silabs.com/wireless/z-wave
- https://www.silabs.com/documents/public/release-notes/SRN14932-1C.pdf
- https://community.silabs.com/s/topic/0TO1M000000qHcQWAU/zwave
- https://z-wavealliance.org/
- https://github.com/Z-Wave-Alliance/
- https://www.silabs.com/support
- https://en.wikipedia.org/wiki/Z-Wave
