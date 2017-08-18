#!/usr/bin/python

'''
This file is the code of Scan Center.
Scan Center controls the same BLE module as Slave Agent does
and allows the BLE module to listen to the broadcast signals from neighbors
and send corresponding messages to Master Agent.

Scan Center creates two Unix domain socket files,
'/tmp/scan.socket' and '/tmp/scan_node.socket' 
to interact with Master Agent and Slave Agent.

Scan Center uses '/tmp/scan_node.socket' to interact with Slave Agent.
When Slave Agent is connected by another device, Slave Agent will tell Scan Center
that it is connected, so that Scan Center can start to scan surrounding device.

When Scan Center scans surrounding devices, it will check whether
there are devices with proper name, e.g. 'type 1 power 2'.
If so, it will send the MAC address of those devices with proper name to Master Agent
through Unix domain socket file, '/tmp/scan.socket'.
'''

from gattlib import DiscoveryService
import socket, sys, time
import pandas as pd

'''
Read the setting file, 'info.txt' first. Proper setting file is like below:

type 1
power 2
MAC AA:AA:AA:AA:AA:AA
service 4
BAN BB:BB:BB:BB:BB:BB
BAN CC:CC:CC:CC:CC:CC

In the first line, we can set the type of this device. Devices with the same type
should be in the same cluster. However, this mechanism has not yet been finished,
so it is better to set every device with different types.

In the second line, we can set the virtual power (battery life or computing power) of this device.
This argument is to allow devices with the same type to determine who is the cluster head.
For now, it can be set to any number.

In the third line, we can set the MAC address of the slave module so that this Scan Center won't
scan this device itself. However, since the module used by Scan Center and Slave Agent is the same,
Scan Center will not discover the slave module, this MAC address can be set to any string
and it won't affect any function.

We can ban this device from connecting to some MAC addresses by putting those MAC address
after the fourth line. This can allows us to control the topology that the tree network becomes.
'''
banList = set("AA:BB:CC:DD:EE:FF")
info = pd.read_csv('/home/pi/Documents/info.txt', header = None)
power = int(info[0][1].split(' ')[1])
type = int(info[0][0].split(' ')[1])
mac = info[0][2].split(' ')[1]

for i in range(4, info.shape[0]):
    ban = info[0][i].split(' ')[1]
    banList.add(ban)

print power, type, mac



#Open two socket files, 'scan.socket' and 'scan_node.socket'.
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

    #Listen to the 'scan_node.socket' to check whether the Slave Agent is connected.
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

    #Scan surrounding devices.
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

        #Check whether the scanned device has proper name, e.g. 'type 1 power 2'.
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
        
        #The next line contains the rule of selecting the cluster head. Disabled now.
        #if x > 4 and power > neighborPowerMax and ( tmpType == type or x - CON_SIG_TIMER > 5 ):

        if x - CON_SIG_TIMER > 5:
            if address not in s:
                #The next line checks whether the scanned device is user device (smart phone)
                if(address[0] != '0' and address[0] != 'B'):
                    print "User Device"
                    #If this device's type is 1, it is allowed to connect to the user device.
                    #This rule is to prevent the situation
                    #that multiple devices connecting to the same user device
                    if(type != 1):
                        continue
                print "Sensor Device"
                s.add(address)
                print(address.lower())
                connection.send(address.lower())
                time.sleep(2)
        time.sleep(1)
    print(x)
    x +=1
