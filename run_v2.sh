sudo bash ./centers/clean.sh
sleep 1
sudo ./centers/sensorCenter &
sudo bash ./start_gatttool.sh &
sudo bash ./start_echo.sh &
sudo python ./centers/exp_server_scan.py

