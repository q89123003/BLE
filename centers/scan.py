#!/usr/bin/python

from gattlib import DiscoveryService
import socket, sys, time

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/tmp/scan.socket')
s = set("AA:BB:CC:DD:EE:FF")
x = 0
while True:
	time.sleep(5)
	service = DiscoveryService("hci0");
	devices = service.discover(1);
	for address, name in devices.items():
		print("name: {}, address: {}".format(name, address));
		#print(address[0])
		if address not in s:
			if(address[0] != '9'):
				s.add(address)
			if(address[0] != '7'):
				sock.send(address.lower())
		time.sleep(5)
	print(x)
	x +=1
