# zipgateway

## Licensing

ZipGateway is covered by one of several different licenses.
The default license is the [Master Software License Agreement (MSLA)](https://www.silabs.com/about-us/legal/master-software-license-agreement), which applies unless otherwise noted. 
Refer to [LICENSE](./LICENSE) for more details.

# How to build zipgateway

Reference OS is currently debian-9 (EoL), Only 32 bits OS are currently supported.

A helper script allows to setup env and build on device, docker or cloud (eg: github actions).

```bash
./helper.mk help
./helper.mk setup/debian
./helper.mk
```

If using a different OS you can rely on docker (assuming you have installed it along docker-compose)

```bash
./helper.mk help
./helper.mk docker/run
```

Building on reference device (Raspberry Pi3+ debian-9 EoL) is also possible too:

Dump image to sdcard from:

- http://downloads.raspberrypi.org/raspbian_lite/images/raspbian_lite-2019-04-09/2019-04-08-raspbian-stretch-lite.zip

```bash
./helper.mk help
./helper.mk setup/raspbian
./helper.mk
```
It should take less than 20 min to build and run tests,
debugging on board using gdb can be helpful too.


# How to build documentation on Ubuntu 20.04.5 LTS (Focal Fossa)
----------------------------
```bash
$ sudo apt-get install -y doxygen graphviz mscgen roffit perl git python3 cmake\
                           gcc xsltproc bison flex gcc-9-multilib \
                          pkg-config:i386 libssl-dev:i386 libc6-dev:i386 \
                          libusb-1.0-0-dev:i386 libjson-c-dev:i386 \
                          openjdk-8-jre curl g++-9-multilib libstdc++-9-dev
```
On all systems:
```bash
$ curl -L http://sourceforge.net/projects/plantuml/files/plantuml.1.2019.7.jar/download --output /opt/plantuml.jar
```

2. Build documentation
```bash
$ export PLANTUML_JAR_PATH=/opt/plantuml.jar
$ mkdir build
$ cd build/
$ cmake ..
$ make doc 
```

3. For detailed description on compiling zipgateway please refer to user guide 
generated in step 2. 
Open src/doc/html/index.hml in browser

```bash
$ xdg-open src/doc/html/index.html
```
# Quick compiling Z/IP Gateway debian package

## Compilation of Z/IP Gateway in i386 Ubuntu docker

**Assuming you cloned the git repo in ```~/zw-zgw```**

### On host machine
```bash
dev-machine:~/zw-zgw/$ cd docker/i386_ubuntu_20_04/
dev-machine:~/zw-zgw/$ make image
dev-machine:~/zw-zgw/$ docker run -v ~/zw-zgw/:/zgw -it zwave/zgw_i386_ubuntu_20_04 bash
```

### Inside docker

```java
root@docker:/zgw/# mkdir build
root@docker:/zgw/# cd build
root@docker:/zgw/# cmake ..
root@docker:/zgw/# make
root@docker:/zgw/# make package
```

## Compilation of Z/IP Gateway in arm Ubuntu docker for Raspberry Pi

### On host machine

```bash
dev-machine:~/zw-zgw/$ cd docker/armhf_debian_stretch_cross/
dev-machine:~/zw-zgw/$ make image
dev-machine:~/zw-zgw/$ docker run -v ~/zw-zgw/:/zgw -it zwave/zgw_armhf_debian_stretch_cross bash
```

### Inside docker

```java
root@docker:/zgw/# mkdir build
root@docker:/zgw/# cd build
root@docker:/zgw/# cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/debian_stretch_armhf.cmake ..
root@docker:/zgw/# make
root@docker:/zgw/# make package
```


