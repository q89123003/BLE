var bleno = require('../bleno');

var BlenoPrimaryService = bleno.PrimaryService;

var EchoCharacteristic = require('./characteristic');

var startAdvertisingFlag = 1;
var fs = require('fs');

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

function intervalFunc () {
  //console.log(bleno.state);
  //console.log(bleno.platform);
  //console.log(bleno.address);
  if(echochar._updateValueCallback == null){
    bleno.startAdvertising(array[0] + ' ' +array[1], ['ec00']);
  }
  else{
    //console.log("Connected. Stop Advertising.");
    bleno.stopAdvertising();
  }
  if(echochar._subscribeFlag == 1){
    console.log("Calling ActiveSend");
    echochar.ActiveSend(Buffer.from('n' + typeID.toString() + serviceID.toString(), 'utf8'), echochar._updateValueCallback);
    echochar._subscribeFlag = 0;
  }
}

setInterval(intervalFunc, 2000);
setInterval(function(){ echochar.checkClient(echochar._updateValueCallback); }, 50);

//exports.echochar = echochar;
