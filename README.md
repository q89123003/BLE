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

### Reset

Before running the project, the plugged-in BLE module must be reset every time after the Pi 3 is turned on.
Use:
```
sudo bash reset.sh
```


Bleno to TinyB (in notification callback):
    1.
        Receive from Bleno: n senso_type service_type (eg. n14)
        Send to Sensor Center: n MAC senso_type service_type (eg. nAA:BB:CC:DD:EE:FF14) // cat MAC in middle
    2.
        Receive from Bleno: t payload (eg. tabcd)
        Send to Sensor Center: t payload (eg. tabcd) //no change

Scan Center to TinyB (in Socket Received)
    1.
        Receive from Scan Center: MAC (eg. AA:BB:CC:DD:EE:FF)
        Connect to MAC

Sensor Center to TinyB (in Socket Received)
    1.
        Receive from Sensor Center: 0 selfNum (eg. 05)
        Maintain a variable "selfNum = 5"
    2.
        Receive from Sensor Center: t MAC payload (eg. tAA:BB:CC:DD:EE:FFabcd)
        Send to Bleno: t payload (eg. tabcd)
    3.

Connection callback
    1.
        Maintain a variable "ConnectionCount"
        Start from 1. "ConnectionCount += 1" after every successful connection.
    2.
        Send to Sensor Center: 0 ConnectionCount (eg. 01)
        Note that this ConnectionCount has not yet plussed 1.
    3. 
        Send to Bleno : n selfNum @ connectionNum (eg. n5@1)
        Note that this ConnectionCount has not yet plussed 1.
    4.
        ConnectionCount += 1