var util = require('util');
var Type = require('type-of-is');

var bleno = require('../bleno');

var BlenoCharacteristic = bleno.Characteristic;

var fs = require('fs');

var array = fs.readFileSync('../centers/info.txt').toString().split("\n");
for(i in array) {
    console.log(array[i]);
}
var arr = array[0].split(' ');
typeID = arr[1];
arr = array[3].split(' ');
serviceID = arr[1];

var net = require('net');

var client = net.createConnection("/tmp/scan_node.socket");
client.on("connect", function() {
    console.log('Connected to sensor.socket');
    client.write('Hello, Server.')
});


client.on('close', function() {
	console.log('Connection closed');
});

var sensorCon = net.createConnection("/tmp/sensor_node.socket");
sensorCon.on("connect", function() {
    console.log('Connected to sensor.socket');
    sensorCon.write('Hello, Server.')
});
sensorCon.on('data', function(data) {
	console.log('Received: ' + data);
});

sensorCon.on('close', function() {
	console.log('Connection closed');
});

var EchoCharacteristic = function() {
  EchoCharacteristic.super_.call(this, {
    uuid: 'ec0e',
    properties: ['read', 'write', 'notify'],
    value: null
  });

  this._value = new Buffer(0);
  this._updateValueCallback = null;
  this._subscribeFlag = 0;

  EchoCharacteristic.prototype.ActiveSend = function(data, callback){
    console.log("Active Sending: " + data);
	  callback(data);
  }
};

util.inherits(EchoCharacteristic, BlenoCharacteristic);

EchoCharacteristic.prototype.onReadRequest = function(offset, callback) {
  console.log('EchoCharacteristic - onReadRequest: value = ' + this._value.toString('hex'));

  callback(this.RESULT_SUCCESS, this._value);
};

EchoCharacteristic.prototype.onWriteRequest = function(data, offset, withoutResponse, callback) {
  this._value = data;

  console.log('EchoCharacteristic - onWriteRequest: value = ' + this._value.toString('hex'));

  sensorCon.write(data.toString())
  if (this._updateValueCallback) {
    console.log('EchoCharacteristic - onWriteRequest: notifying');
    //console.log(Type(data));
    //this._updateValueCallback(this._value);
    this._updateValueCallback(Buffer.from("Got it.", 'utf8'));
  }

  callback(this.RESULT_SUCCESS);
};

EchoCharacteristic.prototype.onSubscribe = function(maxValueSize, updateValueCallback) {
  console.log('EchoCharacteristic - onSubscribe');
  this._subscribeFlag = 1;
  this._updateValueCallback = updateValueCallback;
  client.write('CON_SIG');
};

EchoCharacteristic.prototype.onUnsubscribe = function() {
  console.log('EchoCharacteristic - onUnsubscribe');

  this._updateValueCallback = null;
};


module.exports = EchoCharacteristic;

var main = require('./main');

echochar = main.echochar;

var sendFlag = 0;

client.on('data', function(data) {
	console.log('Received: ' + data);
	//client.destroy(); // kill client after server's response
  sendFlag = 1;
});


function intervalFunc () {
  if(sendFlag == 1){
    console.log("Calling ActiveSend From Socket Client");
    sendFlag = 0;
    echochar.ActiveSend(data, echochar._updateValueCallback);
  }
}

setInterval(intervalFunc, 50);