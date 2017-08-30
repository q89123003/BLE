# BLE tree network

## Introduction
This project allows the built-in BLE module and one plugged-in BLE module to cooperate on Raspberry Pi 3 model B. With this project and two BLE modules, multiple Pi 3s can form a BLE tree network.
There are four main processes in this project: Master Agent ([gatttool.c](./bluez-5.43/attrib/gatttool.c)), Slave Agent ([main_v2.js](./echo/main_v2.js) & [characteristic_v2.js](./echo/characteristic_v2.js)), Scan Center ([server_scan.py](./centers/server_scan.py)) and Sensor Center ([sensorCenter.cpp](./centers/sensorCenter.cpp)).


## Prerequisites
Install [bleno](https://github.com/sandeepmistry/bleno) first. Note that `bluetoothd` should be disabled as the document of `bleno` describes.

## Build
1. Clone this repository under the directory `/home/pi/Documents`.
2. Clone [bleno repository](https://github.com/sandeepmistry/bleno) under this directory (`/home/pi/Documents/BLE`).
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

If the terminal shows
```
bleno - echo
on -> stateChange: poweredOn
on -> advertisingStart: success
```

then the process of reseting is complete.

Press `Ctrl` + `c` to exit.

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

### User Device

To allow a user device to interact with the BLE network, please download the Android Studio [project](https://drive.google.com/file/d/0B5pAHUprrBbMMnd4VkJ3RDR5SDg/view?usp=sharing) and build it on a Android phone.
After the app is built, simply execute it and the device will be connected to the BLE network automatically.