sudo bash ./centers/clean.sh
sudo python ./centers/server_scan.py
sudo ./centers/sensorCenter
sudo ./bluez-5.43/attrib/gatttool -S
sudo bash ./echo/main.sh