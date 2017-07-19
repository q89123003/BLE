#!/usr/bin/python

from gattlib import DiscoveryService
import socket, sys, time
import pandas as pd

banList = set("AA:BB:CC:DD:EE:FF")
info = pd.read_csv('/home/pi/Documents/info.txt', header = None)
power = int(info[0][1].split(' ')[1])
type = int(info[0][0].split(' ')[1])
mac = info[0][2].split(' ')[1]

for i in range(4, info.shape[0]):
    ban = info[0][i].split(' ')[1]
    banList.add(ban)

print power, type, mac

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.bind('/tmp/scan.socket')
sock.listen(1)
connection, client_address = sock.accept()

sock_nodejs = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock_nodejs.bind('/tmp/scan_node.socket')
sock_nodejs.listen(1)
connection_nodejs, client_address_nodejs = sock_nodejs.accept()
print sys.stderr, 'connection from', client_address
s = set("AA:BB:CC:DD:EE:FF")
x = 0

neighborPowerMax = power - 1
CON_SIG_TIMER = 1e9

while True:
    time.sleep(1)
    connection_nodejs.settimeout(0.5)
    try:
        data = connection_nodejs.recv(1024)
    except:
        data = 'NULL'
        print "recv timeout.\n"
    connection_nodejs.settimeout(None)
    if(data == 'CON_SIG'):
        print 'Set CON_SIG'
        CON_SIG_TIMER = x
    try:
        service = DiscoveryService("hci1");
        devices = service.discover(1);
    except:
        print("Exception when discoverying");
        time.sleep(1)
        continue
        
    for address, name in devices.items():
        print("name: {}, address: {}".format(name, address));
        #print(address[0])
        if address in banList:
            continue
        wordlist = name.split(' ')

        if len(wordlist) >= 4:
            tmpType = int(wordlist[1])
            tmpPower = int(wordlist[3])
        
        else:
            continue
        if len(wordlist) >= 4 and x <= 4:
            tmpType = int(wordlist[1])
            tmpPower = int(wordlist[3])
            print "Received broadcast from neighbor: ", tmpType, tmpPower
            if tmpType == type and tmpPower > neighborPowerMax:
                neighborPowerMax = tmpPower
        if x > 4 and power > neighborPowerMax and ( tmpType == type or x - CON_SIG_TIMER > 3 ) and tmpType == type - 1:
            if address not in s:
                if(address[0] != '0' and address[0] != 'B'):
                    print "User Device"
                    if(type != 1):
                        continue
                print "Sensor Device"
                if tmpType != 0:
                    s.add(address)
                print(address.lower())
                time.sleep(2)
                connection.send(address.lower())
                time.sleep(2)
        time.sleep(1)
    print(x)
    x +=1
