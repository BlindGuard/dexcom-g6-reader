# dexcom-g6-reader

## Getting started

### Hardware

This project was created on and for the ESP-WROOM-32 SoC.


### Prerequisites

To compile this project you need to setup the [ESP-IDF and toolchain](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#get-started).


### Configuration

For this software to work, you have to change the 6-digit `transmitter_id` in `dexcom_g6_reader.h` to the serial number of the 
transmitter you want to connect to.  
```
const char *transmitter_id = "8xxxxx";
```
The serial number can be found on the backside of the transmitter or on its packaging.


### Building

1. Go to the top level directory in this repositoriy.
2. Then enter `make -j` to start the build process.


### Deployment

Use `make flash` to flash the program to a connected ESP32 board.  
With the `make monitor` command you can get log output from the device.

