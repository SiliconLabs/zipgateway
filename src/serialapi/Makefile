#CC=mips-openwrt-linux-gcc
#CC=mips-linux-gnu-gcc
ZWAVEDIR=../../Z-Wave
CFLAGS=-I$(ZWAVEDIR)/include -g -Os

SERIALAPI_OBJ=conhandle.o Serialapi.o linux.o

serialapi_test: serialapi_test.o $(SERIALAPI_OBJ)
stresstest: stresstest.o $(SERIALAPI_OBJ)
clean:
	rm *.o
