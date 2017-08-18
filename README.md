# BLE tree network

## Introduction
This project allows the built-in BLE module and one plugged-in BLE module to cooperate on Raspberry Pi 3 model B. With these two BLE modeuls, multiple Pi 3 can form a BLE tree network.

## Prerequisites
Install [bleno](https://github.com/sandeepmistry/bleno) first. Note that `bluetoothd` should be disabled as the document of `bleno` describes.

## Build
1. Clone this repository.
2. Clone [bleno repository](https://github.com/sandeepmistry/bleno) under this directory.
3. Change the directory.
 ```
 cd ./centers
 ```
4. Compile and build `sensorCenter`.
```
make
```

5. Change the directory.
```
cd ../bluez-5.43/attrib
```

6. Compile and build `gatttool`.
```
make
```

7. Finally, go back to the home directory of this repository.
```
cd ../../
```

## Run

### Maintain the setting file

Before running the project, a setting file should be named `info.txt` and put in `/home/pi/Documents`. Proper setting file can be referred to [info_example.txt](./info_example.txt).

### Reset

Before running the project, the plugged-in BLE module must be reset every time after the Pi 3 is turned on.
Use:
```
sudo bash centers/reset.sh
```

### Run

Use:
```
sudo bash run_v2.sh
```

### Stop

To stop the project, press `Ctrl` + `c` first to stop the foreground process first and type
```
sudo bash stop.sh
```
in terminal.
