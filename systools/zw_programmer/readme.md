# NVM programmer

## Get started

### Build
Just run CMake and make at the Z/IP Gateway level:
```
mkdir _build/ && cd _build && cmake .. && make
```
The binary will be available at `_build/systools/zw_programmer/zw_programmer`

### Usage
Run the program with a mode : `read`, `write` or `fw` and 2 arguments:
-s to specify the serial port
-f to specify the input/outout filename


Examples
```
nvm-programmer read -s /dev/ttyUSB0 -f my_output_file.nvm
nvm-programmer write -s /dev/ttyUSB0 -f my_input_file.nvm
nvm-programmer fw -s /dev/ttyUSB0 -f my_input_firmware.hex
```

The output filename is optional in case of reading the NVM, it will default to `output_file.nvm`
If no serial_device is provided, the programmer will try with `/dev/ttyUSB0`

### Samples
Some sample NVM files are provided in the `samples/` subdirectory. They can be used for testing read/write functionalities with bridge controllers.

### Examples
Restore the NVM for a 500 series chip:
```
> ./nvm-programmer read -s /dev/ttyACM0 -f output_file.nvm
> sha1sum output_file.nvm
ffc1b6eb66ae2543265bdb18ce058adac47cb900  output_file.nvm
> sha1sum ../samples/my_500_nvm_backup.nvm
7639f8a15696b39ee22e30998e49c9f3ac51b3f4  ../samples/my_500_nvm_backup.nvm
> ./nvm-programmer write -s /dev/ttyACM0 -f 500_nvm_backup.nvm
Using serial device /dev/ttyACM0
Connected to Serial device: OK
[...] Written 14403 / 14402 bytes...
Closing Serial connection
> ./nvm-programmer read -s /dev/ttyACM0 -f current_state.nvm
[...] NVM read successfully
> sha1sum current_state.nvm
7639f8a15696b39ee22e30998e49c9f3ac51b3f4  current_state.nvm
```
