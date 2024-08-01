# Using the Example Application

**libzwaveip** includes two sample applications that demonstrate how a Z/IP
Client can handle outgoing and incoming messages to/from a Z/IP Gateway.
The ```reference_client``` application provides a command line interface that
lists discovered Z-Wave IP devices and sends Command Class messages to them.
The ```reference_listener``` application provides visualization of Command
Class messages that a Z/IP Gateway forwards to its **unsolicited destination**.
Each of these applications are explained in more detail below.

## reference_client Basic Usage

Typically, ```reference_client``` is executed with two additional parameters:

* The IP address of a Z/IP Gateway (either an IPv4 or an IPv6 address) with
the ```-s``` switch
* The PSK used by the Z/IP Gateway that's being connected to with the ```-p```
switch. Default: ```123456789012345678901234567890aa```

To connect to the Z/IP Gateway running locally in the Raspberry Pi,
get its address from ```/usr/local/etc/zipgateway.cfg``` by looking at the
value of the ```ZipLanIp6``` key. For example, if the configuration file
shows the following:

```
[...]
ZipLanIp6 = fd00:ffef::3
[...]
```

 ```reference_client``` can be connected to it by running:

```
$ ./reference_client -s fd00:ffef::3
```

If the connection to the Z/IP Gateway is successful, you'll see the ```(ZIP)```
prompt in your shell.
```
Logging to "/tmp/libzw_reference_client.log"
DTLS PSK not configured - using default.
(ZIP)
```

The ```reference_client```'s shell supports a few commands. Type ```help``` for a list.

```
(ZIP) help
Usage: help [addnode|removenode|learnmode|acceptdsk|grantkeys|setdefault|list|nodeinfo|hexsend|send|pl_list|pl_add|pl_remove|pl_reset|identify|lifeline|bye|exit|quit|]
Usage: help <tab>: for autocompleting Z-Wave command class names

Classic Network management:
	 learnmode: Start learn mode at the Z/IP GW
	 addnode: Add a new node in the Z/IP GW's network
	 removenode: Remove a node from the Z/IP GW's network
	 identify "Service name": Blink the identify indicator of the device
	 lifeline "Service name" nodeid: Set the lifeline association to the specified nodeid
	          (if nodeid is zero all lifeline associations will be removed)
	 list: List the nodes present in the network (use -n option to print NIF for each node
	 nodeinfo "Service name" nodeid: Get fresh node info for the specified nodeid
	 setdefault: Reset the Z/IP Gateway to default.

SmartStart Node Provisioning list management (pl_):
	 pl_list: Display the nodes in the provisioning list
	 pl_add dsk [tlv:value] [tlv:value] ... Add/update a node in the provisioning list
	 pl_remove dsk: Remove a node from the provisioning list
	 pl_reset: Flushes the entire provisioning list at the Z/IP GW

Sending frames to nodes:
	 hexsend: Send data to a node specifying the payload as hexadecimal argument
	 send "Service name" COMMAND_CLASS_NAME COMMAND hexpayload: Send a command to the node

Exit the program: bye|exit|quit
```

### Adding and Removing Devices

To instruct the Z/IP Gateway to add a new node to the Z-Wave network, use
the ```addnode``` command. This will put Z/IP Gateway into inclusion mode, where
it'll wait for a joining node wanting to be added to the Z-Wave network. The
command doesn't produce any output in the console, but the Z/IP Gateway is now
waiting for a new device to request inclusion to the Z-Wave network. Different
device manufacturers have different procedures for including their devices, so
you'll need to refer to the product manual for the device you're using for the
correct procedure. Typically, a single- or quick triple-press on their button is
what is required to request inclusion.

After the node has been successfully included, ```reference_client``` will print
out a summary of the Command Classes the included device can work with and some
additional information to its console - for example:

```
(ZIP) addnode
(ZIP)
cmd_class:  COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION  v3
cmd:  NODE_ADD_STATUS
param:  Seq. No 0x01
param:  Status  >
	 NODE_ADD_STATUS_DONE
param:  Reserved1 0x00
param:  New Node ID 0x06
param:  Node Info Length 0x19
param:  Properties1  >
	Z-Wave Protocol Specific Part 1:: 53
	 Listening : true

param:  Properties2  >
	Z-Wave Protocol Specific Part 2:: 1c
	 Opt : true

param:  Basic Device Class 0x04
param:  Generic Device Class 0x10
param:  Specific Device Class 0x01
param:  Command Class  >
	 COMMAND_CLASS_ZWAVEPLUS_INFO :
	 COMMAND_CLASS_SUPERVISION :
	 COMMAND_CLASS_TRANSPORT_SERVICE :
	 COMMAND_CLASS_SECURITY :
	 COMMAND_CLASS_SECURITY_2 :
	 COMMAND_CLASS_ZIP_NAMING :
	 COMMAND_CLASS_ZIP :
	 (null) :
	 COMMAND_CLASS_NO_OPERATION :
	 COMMAND_CLASS_VERSION :
	 COMMAND_CLASS_SWITCH_BINARY :
	 COMMAND_CLASS_ASSOCIATION :
	 COMMAND_CLASS_IP_ASSOCIATION :
	 COMMAND_CLASS_MULTI_INSTANCE_ASSOCIATION :
	 COMMAND_CLASS_ASSOCIATION_GRP_INFO :
	 COMMAND_CLASS_MANUFACTURER_SPECIFIC :
	 COMMAND_CLASS_DEVICE_RESET_LOCALLY :
	 COMMAND_CLASS_POWERLEVEL :
	 COMMAND_CLASS_FIRMWARE_UPDATE_MD :

param:  Granted Keys 0x81
param:  KEX Fail Type 0x00
param:  Properties3  >
	DSK Length:: 10
	Reserved2:: 00

param:  DSK  >
 0x4b 0x18 0x0b 0x80 0xcc 0x3e 0x71 0x1c 0xea 0xef 0xd6 0x4f 0xa4 0x86 0x32 0xa5
bytestream: 34 02 01 06 00 06 19 d3 9c 04 10 01 5e 6c 55 98 9f 68 23 f1 00 86 25 85 5c 8e 59 72 5a 73 7a 81 00 10 4b 18 0b 80 cc 3e 71 1c ea ef d6 4f a4 86 32 a5

Inclusion done
(ZIP)
```

You can instruct the Z/IP Gateway to prepare to remove a device using the ```removenode```
command. Different device manufacturers have different
procedures for having their devices excluded, so you'll need to refer to the
product manual for the device you're using for the correct procedure. Typically,
        a single- or quick triple-press on their button is what is required to
        request exclusion.

After the operation completes, ```reference_client``` will print out a summary - for example:

```
(ZIP) removenode
(ZIP)
cmd_class:  COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION  v3
cmd:  NODE_REMOVE_STATUS
param:  Seq. No 0x04
param:  Status  >
	 NODE_REMOVE_STATUS_DONE
param:  NodeID 0x06
bytestream: 34 04 04 06 06

```

### Listing and Controlling Devices

Z/IP Gateway announces the Z-Wave devices it manages as mDNS services. You can browse for
Z-Wave IP devices from outside ```reference_client``` by running:

```
$ avahi-browse _z-wave._udp
```

which will show any discovered Z-Wave IP devices:

```
+ br-lan IPv6 Static Controller [e3a6e2620100]              _z-wave._udp         local
+ br-lan IPv6 Switch Binary [e3a6e2620700]                  _z-wave._udp         local
```

This command continues running in the background, looking for more services,
until you stop it with ```Ctrl+C```. For more details on using ```avahi-browse```
, refer to its ```man``` page:

```
$ man avahi-browse
```

From within ```reference_client``` , use the ```list``` command to show Z-Wave
IP devices that the Z/IP Gateway is managing:

```
(ZIP) list
List of discovered Z/IP services:
"Static Controller [d9a5cd3e-001-000]" IP:fd00:ffef::3
"Switch Binary [d9a5cd3e-007-000]" IP:fd00:bbbb::7
```

Use the ```send``` command to send Z-Wave messages to these service names.
The messages must be Z-Wave Command Classes - their construction was briefly
discussed in [Command Class Basics](Command-Class-Basics). As a convenience, the ```send```
command allows you to use names for the **Command Class** and
**Command** identifiers - the first two bytes of a Z-Wave Command Class message - and
provides Tab completion for them.

For example, to turn on the Switch Binary that was listed above, send it
a "Switch Binary Set" command with a value of "0xff":

```
(ZIP) send "Switch Binary [d9a5cd3e-007-000]" COMMAND_CLASS_SWITCH_BINARY SWITCH_BINARY_SET ff
```

and to turn it off:

```
(ZIP) send "Switch Binary [d9a5cd3e-007-000]" COMMAND_CLASS_SWITCH_BINARY SWITCH_BINARY_SET 00
```

If you know the IP address of the device you want to communicate with and are
comfortable typing the "raw" Z-Wave Command Class message as a hexadecimal
string, the ```hexsend``` command can accomplish the same thing:

```
(ZIP) hexsend fd00:ab45:754f:c15d::7 2501ff
```

## reference_listener Basic Usage

reference_listener can act as a server that listens connections and
messages from client, which is Z/IP Gateway's unsolicited destination and
message in this case.

A basic help can be seen by typing ```./reference_listener```

```
$ ./reference_listener
Logging to "/tmp/libzw_reference_listener.log"
IP address not specified or too long.

Usage: reference_listener [-p pskkey] [-n] [-x zwave_xml_file] -l ip_address -o port

  -p Provide the DTLS pre-shared key as a hex string.
     Default value: 123456789012345678901234567890AA
     If the -n option is also used the key will not be used.

  -n Use a non-secure UDP connection to the gateway. Default is a DTLS connection.

  -x Provide XML file containing command class definitions. Used to decode
     received messages. Default is to search for the following two files:
      - /usr/local/share/zwave/ZWave_custom_cmd_classes.xml
      - ./ZWave_custom_cmd_classes.xml

  -l Specify the IP address of the interface to listen on.
     Can be IPv4 or IPv6.

  -o Specify the port to listen for incoming connections on.
     Default value: 41230
Examples:
  reference_listener -l fd00:aaaa::3 -o 54000
  reference_listener -l 10.168.23.10 -p 123456789012345678901234567890AA

```

Use the command below to start referen_listener with an IPv6 unsolicited address and port,
    which both can be specified in the configuration of Z/IP Gateway. Note that
    reference_listener supports both IPv6 and IPv4.

```
./reference_listener -l fd00:ffef::a5b4:b4d4:c445:738f -o 41230
```

Then if the Z/IP Gateway is started, you should be able to see the Node List Report that
Z/IP Gateway sends out towards its unsolicited destination every time it starts
up.

```
$ sudo ./reference_listener -l fd00:ffef::a5b4:b4d4:c445:738f -o 41230
Logging to "/tmp/libzw_reference_listener.log"
DTLS PSK not configured - using default.
---------- STARTING NEW DTLS SERVER SESSION ----------
Listening for DTLS handshake on: [fd00:ffef::a5b4:b4d4:c445:738f]:41230
Connection received from: fd00:ffef::3
Negotiated DTLS version: DTLS 1.2
---------- STARTING NEW DTLS SERVER SESSION ----------
Listening for DTLS handshake on: [fd00:ffef::a5b4:b4d4:c445:738f]:41230

-----------------------------------------
Received data:
52 02 15 00 01 41 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00

cmd_class:  COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY  v2
cmd:  NODE_LIST_REPORT
param:  Seq. No 0x15
param:  Status  >
	Latest
param:  Node List Controller ID 0x01
param:  Node List Data  >
 value: 41
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00
 value: 00

bytestream: 52 02 15 00 01 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```
