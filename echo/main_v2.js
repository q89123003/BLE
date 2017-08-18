/*
This file and 'characteristic_v2.js' are the main code of the Slave Agent

The main function of Slave Agent is to control another BLE module to work in the slave mode.
Slave Agent allows the device to broadcast their existence and UUID,
and wait for a connection from other BLE module in the master mode.

Slave Agent connects to two Unix domain socket file, '/tmp/scan_node.socket'
and '/tmp/sensor_node.socket'.

Upon Slave Agent is connected, it tells Scan Center through 'scan_node.scoekt',
so that Scan Center can start to scan surrounding device.

Slave Agent uses 'sensor_node' to communicate with Sensor Center.
When Slave Agent receives a packet from the parent device, it will forward the packet
to Sensor Center. If Sensor Center wants to send packets to the parent device,
it will send the packets to Slave Agent through the socket file.
*/


var bleno = require('../bleno');

var BlenoPrimaryService = bleno.PrimaryService;

var EchoCharacteristic = require('./characteristic_v2').EchoCharacteristic;
var sensorCon = require('./characteristic_v2').sensorCon;
var startAdvertisingFlag = 1;
var fs = require('fs');


/*
Read the setting file, so that the Slave Agent can broadcast proper name.
*/
var array = fs.readFileSync('/home/pi/Documents/info.txt').toString().split("\n");
var arr = array[0].split(' ');
typeID = arr[1];
arr = array[3].split(' ');
serviceID = arr[1];

console.log('bleno - echo');

bleno.on('stateChange', function(state) {
  console.log('on -> stateChange: ' + state);

  if (state === 'poweredOn') {
    //bleno.startAdvertising('echo', ['ec00']);
    if(startAdvertisingFlag == 1){
   bleno.startAdvertising(array[0] + ' ' +array[1], ['ec00']);
}
  } else {
    bleno.stopAdvertising();
  }
});

var echochar = new EchoCharacteristic()

bleno.on('advertisingStart', function(error) {
  //console.log('on -> advertisingStart: ' + (error ? 'error ' + error : 'success'));

  if (!error) {
    bleno.setServices([
      new BlenoPrimaryService({
        uuid: 'ec00',
        characteristics: [
          echochar
        ]
      })
    ]);
  }
});

//An interval function to check whether Slave Agent is connected.
function intervalFunc () {
  //console.log(bleno.state);
  //console.log(bleno.platform);
  //console.log(bleno.address);
  if(echochar._updateValueCallback == null){
    //if not connected, echochar._updateValueCallback remains null
    //keep advertising.
    bleno.startAdvertising(array[0] + ' ' +array[1], ['ec00']);
  }
  else{
    //if connected, stop advertising
    //console.log("Connected. Stop Advertising.");
    bleno.stopAdvertising();
  }
    //Upon connected, tell the parent device its type and service ID
  if(echochar._subscribeFlag == 1){
    console.log("Calling ActiveSend");
    echochar.ActiveSend(Buffer.from('n' + typeID.toString() + serviceID.toString(), 'utf8'), echochar._updateValueCallback);
    echochar._subscribeFlag = 0;
  }
}

setInterval(intervalFunc, 2000);

//When receive packets from Sensor Center, transmit packets to the parent device.
sensorCon.on('data', function(data) {
  console.log('Received: ' + data);
	//client.destroy(); // kill client after server's response
  echochar.ActiveSend(data, echochar._updateValueCallback);
});
//exports.echochar = echochar;
