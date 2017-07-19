sudo bash ./centers/clean.sh
sudo python ./centers/server_scan.py &
sleep 2
sudo ./centers/sensorCenter &
sleep 2
sudo ./bluez-5.43/attrib/gatttool -S &
sleep 2
sudo bash ./echo/main.sh &