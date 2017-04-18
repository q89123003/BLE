var fs = require('fs');

var array = fs.readFileSync('./centers/info.txt').toString().split("\n");
for(i in array) {
    console.log(array[i]);
}
var arr = array[3].split(' ');
console.log(arr[1]);
