/*
This file is the code of Sensor Center.

Sensor Center is like the brain of the device. It records the table of the parent and children
and their corresponding MAC addresses and controls where packets should be transmitted to.

Sensor Center creates two Unix domain socekt files, '/tmp/sensor.socket' and '/tmp/sensor_node.socket'
to communicat with Master Agent and Slave Agent

Slave Agent to Master Agent (in notification callback):
    1.
        Receive from Slave Agent: n senso_type service_type (eg. n14)
        Send to Sensor Center: n MAC senso_type service_type (eg. nAA:BB:CC:DD:EE:FF14) // cat MAC in middle
    2.
        Receive from Slave Agent: t payload (eg. tabcd)
        Send to Sensor Center: t payload (eg. tabcd) //no change

Scan Center to Master Agent (in Socket Received)
    1.
        Receive from Scan Center: MAC (eg. AA:BB:CC:DD:EE:FF)
        Connect to MAC

Sensor Center to Master Agent (in Socket Received)
    1.
        Receive from Sensor Center: 0 selfNum (eg. 05)
        Maintain a variable "selfNum = 5"
    2.
        Receive from Sensor Center: t MAC payload (eg. tAA:BB:CC:DD:EE:FFabcd)
        Send to Slave Agent: t payload (eg. tabcd)
    3.

Connection callback
    1.
        Maintain a variable "ConnectionCount"
        Start from 1. "ConnectionCount += 1" after every successful connection.
    2.
        Send to Sensor Center: 0 ConnectionCount (eg. 01)
        Note that this ConnectionCount has not yet plussed 1.
    3. 
        Send to Slave Agent : n selfNum @ connectionNum (eg. n5@1)
        Note that this ConnectionCount has not yet plussed 1.
    4.
        ConnectionCount += 1
*/

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <fstream>
#include <math.h>
#include <map>
#include <queue>
#include <wiringPi.h>
#include <stdint.h>
#include <ctime>

#define MAX_TIMINGS	85
#define DHT_PIN		3	/* GPIO-22 */
#define LED_PIN     0

#define MaxSensorNum 7

#define TesterTargetNum 2
#define ListInterval 50

#define TEST_INTERVAL 500

using namespace std;
//char *socket_path = "./socket";
char socket_path[32] = "/tmp/sensor.socket";

class Sensor{
public:
    Sensor(){};
    Sensor(string addr, int t, int n){ mac = addr; type = t; number = n;};
    string mac;
    int type;
    int number;
};

class Tester{
  public:
    const static int TESTTIME = 200;
    int sendCount;
    int recvCount;
    long int totalRTT;
    long int sendTime[TESTTIME];
    long int recvTime[TESTTIME];
    Tester(){sendCount = 0; recvCount = 0; totalRTT = 0;};
    bool isDone(){return sendCount == TESTTIME;};
    int getSendCount() { return sendCount;};
    void send(long int time){sendTime[sendCount] = time; sendCount++;};
    void recv(int count, long int time){
      recvTime[count] = time;
      recvCount++;
      if(count == TESTTIME - 1){
        cout << "Test Done!" << endl;
        cout << "Deliver Rate = " << 100 * recvCount / TESTTIME << "% (" << recvCount << '/' << TESTTIME << ")" << endl;
        cout << "Average RTT = " << totalRTT / recvCount << endl; 
      }
      else{
        cout << "Deliver Rate = " << 100 * recvCount / TESTTIME << "% (" << recvCount << '/' << TESTTIME << ")" << endl;
        cout << "Average RTT = " << totalRTT / recvCount << endl; 
      }
    };
    void reset(){
      sendCount = 0;
      recvCount = 0;
      totalRTT = 0;
    };
    long int RTT(int count){
      totalRTT += recvTime[count] - sendTime[count];
      return recvTime[count] - sendTime[count];
    };
};


Tester tester;
struct timeval tp;
int interval = 2000;
int findSensor(Sensor* sensorArray, string mac, int sensorCount){
  for(int i = 0; i <= sensorCount; i++){
    if( sensorArray[i].mac == mac ) return i;
  }

  return -1;
}

string getMacByNum(Sensor* sensorArray, int Num, int sensorCount){
  for(int i = 0; i <= sensorCount; i++){
    if( sensorArray[i].number == Num )
      return sensorArray[i].mac;
  }
  cout << "Can't find connection!\n";
}

int getNum(int, int);
int getDirection(int, int);
map<int,int> serviceMap; //<NodeNum, serviceNum>
queue<int> queueOfNodeForList; //queue of Node that is asking service list. <NodeNum>
map<int,int>::iterator serviceMapIt;

int data[5] = { 0, 0, 0, 0, 0 };
void read_dht_data();
void changeLed();
int humidity, celsius;
int ledState = 0;

int main(int argc, char *argv[]) {
  long int connectedTime;
  long int listSendTime = 0;
  int selfNum = -1;

  fstream fin;
  char line[32];
  char* token;
	fin.open("/home/pi/Documents/info.txt",ios::in);
	
  fin.getline(line, sizeof(line), '\n');
  token = strtok(line, " ");
  token = strtok(NULL, " ");
  const int myType = token[0] - '0';
  cout << "My Type: " << myType << endl;
  fin.getline(line, sizeof(line), '\n');
  fin.getline(line, sizeof(line), '\n');
  fin.getline(line, sizeof(line), '\n');
  token = strtok(line, " ");
  token = strtok(NULL, " ");
  const int myService = token[0] - '0';
  cout << "My Service: " << myService << endl;

  Sensor sensor[20];
  struct sockaddr_un addr;
  struct sockaddr_un addr_node;
  char buf[32], buf_node[32];
  string mac;
  int fd, bytes_read, clientfd, connectCount = 0;
  int fd_node, bytes_read_node, clientfd_node;

  int sensorCount = -1;
  if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket error");
    exit(-1);
  }
  if ( (fd_node = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
    perror("socket error");
    exit(-1);
  }

  if ( wiringPiSetup() == -1 ){
    perror("wiringPi setup error");
    exit( 1 );
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, "/tmp/sensor.socket");
  bind(fd, (struct sockaddr*)&addr, sizeof(addr));

  memset(&addr_node, 0, sizeof(addr_node));
  addr_node.sun_family = AF_UNIX;
  strcpy(addr_node.sun_path, "/tmp/sensor_node.socket");
  bind(fd_node, (struct sockaddr*)&addr_node, sizeof(addr_node));

  listen(fd, 2);    
  clientfd = accept(fd, NULL, NULL);
  cout << "Accepted  first socket\n";

  listen(fd_node, 2);    
  clientfd_node = accept(fd_node, NULL, NULL);
  cout << "Accepted second socket\n";

  clock_t start;
  start = clock();

  while(true){
      memset(&buf, 0, sizeof(buf));
      if( (bytes_read = recv(clientfd, buf, sizeof(buf), MSG_DONTWAIT)) > 0 ){
        cout << "Received " << bytes_read << " bytes.\n Message: " << buf << endl;
        switch(buf[0]){
          case 's': {
            mac = string(buf, 1, 17);
            cout << "mac: " << mac << endl;            
            if(findSensor(sensor, mac, sensorCount) == -1){
              sensorCount++;
              cout << "sensor ID: " << sensorCount << endl;
              sensor[sensorCount] = Sensor(mac, -1, 0);
            }
            break;
          }
          case 'd': {
            string ad = string(buf, 1, 6);
            int sensorId = (buf[7] - '0');
            string payload = string(buf, 8, 32);
            cout << "ad: " << ad << ", target ID: " << sensorId << ", payload: " << payload << endl;
            string dst = sensor[sensorId].mac;
            char sendBuffer[32];
            strcpy(sendBuffer, "d");
            strcat(sendBuffer, dst.c_str());
            strcat(sendBuffer, ad.c_str());
            strcat(sendBuffer, payload.c_str());
            send(clientfd, sendBuffer, 32, MSG_DONTWAIT);
            break;
          }
          case 'l':{
            for(int i = 0; i < sensorCount; i++){
              char tmp = (char)('0' + i);
              strcat(buf, &tmp);
              tmp = (char)('0' + sensor[i].type);
              strcat(buf, &tmp);
              send(clientfd, buf, strlen(buf), MSG_DONTWAIT);
            }
            break;
          }

          case 'n':{
            mac = string(buf, 1, 17);
            cout << "mac: " << mac << endl;            
            if(findSensor(sensor, mac, sensorCount) == -1){
              sensorCount++;
              int typeID = buf[18] - '0';
              int serviceID = buf[19] - '0';
              cout << "sensor ID: " << sensorCount << endl;
              cout << "Type ID: " << typeID << endl;
              cout << "Service ID: " << serviceID << endl;
              if(typeID == myType){
                cout << "Same Type!\n";
                sensor[sensorCount] = Sensor(mac, typeID, -1);
              }
              else{
                cout << "Different Type!\n";
                //connectCount++;
                cout << "Connection Count: " << connectCount << endl;
                sensor[sensorCount] = Sensor(mac, -1, getNum(selfNum, connectCount));
                cout << "Connection Num: " << getNum(selfNum, connectCount) << endl;
              }
            }
            break;
          }
          
          case 't':{
            char toToken[32];
            strcpy(toToken, buf);
            token = strtok(toToken + 1, "@");
            int targetNum = atoi(token);
            int direction = getDirection(targetNum, selfNum);

            if(direction == 0){
              cout << "Reach Target Node!\n";
              token = strtok(NULL, "@");
              int returnTargetNum = atoi(token);
              token = strtok(NULL, "@");
              if(token[0] == 'r'){ //packet for registering: t 2 @ [nodeNum] @ r [serviceNum]
                cout << "Application Layer: Register" << endl;
                int packetServiceNum = token[1] - '0';
                cout << "(Node Number, Service Number) = (" << returnTargetNum << ", " << packetServiceNum << ")" << endl;
                serviceMap[returnTargetNum] = packetServiceNum;
                serviceMapIt = serviceMap.begin();
              }
              else if(token[0] == 'l'){ //packet for asking list: t 2 @ [nodeNum] @ l
                cout << "Node " << returnTargetNum << " is asking for Service List" << endl;
                queueOfNodeForList.push(returnTargetNum);
                //gettimeofday(&tp, NULL);
                //long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
                long int ms = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;
                listSendTime = ms;
              }

              else if(token[0] == 'A'){
                  char sendBuffer[32];
                  memset(&sendBuffer, 0, sizeof(sendBuffer));
                  char selfNumBuffer[8];
                  char targetNumBuffer[8];
                  sprintf(targetNumBuffer, "%d\0", returnTargetNum);
                  sprintf(selfNumBuffer, "%d\0", selfNum);
                  string linkMAC = getMacByNum(sensor, getNum(selfNum, getDirection(returnTargetNum, selfNum)), sensorCount);
                  char dataNumBuffer[8];
                  if(myService == 1){
                    read_dht_data();
                    //sprintf(dataNumBuffer, "%d\0", humidity);
                    sprintf(dataNumBuffer, "%d\0", celsius);
                  }
                  else if(myService == 3){
                    changeLed();
                    sprintf(dataNumBuffer, "%d\0", ledState);
                  }
                  strcpy(sendBuffer, "t");
                  strcat(sendBuffer, linkMAC.c_str());
                  strcat(sendBuffer, targetNumBuffer);
                  strcat(sendBuffer, "@");
                  strcat(sendBuffer, selfNumBuffer);
                  strcat(sendBuffer, "@A@");
                  strcat(sendBuffer, dataNumBuffer);
                  strcat(sendBuffer, "@\0");
                  send(clientfd, sendBuffer, 32, MSG_DONTWAIT);
              }

              else{
                int packetCount = atoi(token);

                if( targetNum != TesterTargetNum ){
                  if(packetCount >= 0 && packetCount < tester.TESTTIME){
                    struct timeval tp;
                    //gettimeofday(&tp, NULL);
                    //long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
                    long int ms = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;
                    tester.recv(packetCount, ms);
                    cout << "RTT: " << tester.RTT(packetCount) << " ms\n";
                  }
                }

                else{
                  if(getDirection(returnTargetNum, selfNum) == -1){
                    char sendBuffer[32];
                    memset(&sendBuffer, 0, sizeof(sendBuffer));
                    char countBuffer[8];
                    char selfNumBuffer[8];
                    char targetNumBuffer[8];
                    sprintf(targetNumBuffer, "%d\0", returnTargetNum);
                    sprintf(countBuffer, "%d\0", packetCount);
                    sprintf(selfNumBuffer, "%d\0", selfNum);
                    strcpy(sendBuffer, "t");
                    strcat(sendBuffer, targetNumBuffer);
                    strcat(sendBuffer, "@");
                    strcat(sendBuffer, selfNumBuffer);
                    strcat(sendBuffer, "@");
                    strcat(sendBuffer, countBuffer);
                    send(clientfd_node, sendBuffer, 32, MSG_DONTWAIT);
                  }
                  else{
                    string linkMAC = getMacByNum(sensor, getNum(selfNum, getDirection(returnTargetNum, selfNum)), sensorCount);
                    char sendBuffer[32];
                    memset(&sendBuffer, 0, sizeof(sendBuffer));
                    char countBuffer[8];
                    char selfNumBuffer[8];
                    char targetNumBuffer[8];
                    sprintf(targetNumBuffer, "%d\0", returnTargetNum);
                    sprintf(countBuffer, "%d\0", packetCount);
                    sprintf(selfNumBuffer, "%d\0", selfNum);
                    strcpy(sendBuffer, "t");
                    strcat(sendBuffer, linkMAC.c_str());
                    strcat(sendBuffer, targetNumBuffer);
                    strcat(sendBuffer, "@");
                    strcat(sendBuffer, selfNumBuffer);
                    strcat(sendBuffer, "@");
                    strcat(sendBuffer, countBuffer);

                    send(clientfd, sendBuffer, 32, MSG_DONTWAIT);
                  }
                }
              }
            }

            else if(direction == -1){
              cout << "Send Packet to Nodejs\n";
              send(clientfd_node, buf, 32, MSG_DONTWAIT);
            }
            else{
              cout << "Send Packet to gatttool\n";
              string linkMAC = getMacByNum(sensor, getNum(selfNum, direction), sensorCount);
              char sendBuffer[32];
              strcpy(sendBuffer, "t");
              strcat(sendBuffer, linkMAC.c_str());
              strcat(sendBuffer, buf + 1);
              send(clientfd, sendBuffer, 32, MSG_DONTWAIT);
            }
            break;
          }

          case '0':
            connectCount = buf[1] - '0';
            break;
        }
      }

      memset(&buf_node, 0, sizeof(buf_node));
      if( (bytes_read_node = recv(clientfd_node, buf_node, sizeof(buf_node), MSG_DONTWAIT)) > 0 ){
          int parentNum, biasNum;
          switch(buf_node[0]){
            case 'n': //packet: n [parentNum] @ biasNum
              token = strtok(buf_node + 1, "@");
              parentNum = atoi(token);
              token = strtok(NULL, " ");
              biasNum = atoi(token);
              selfNum = getNum(parentNum, biasNum); //calculate selfNum by parentNum and biasNum
              cout << "Self Num: " << selfNum << endl;
              char sendBuffer[32];
              sendBuffer[0] = '0';
              sprintf(sendBuffer+1,"%d", selfNum);
              //send to gatttool: 0 [selfNum]
              //to let gatttool know selfNum
              send(clientfd, sendBuffer, 32, MSG_DONTWAIT);
              
              //register to sub-root (number 2)
              memset(&sendBuffer, 0, sizeof(sendBuffer));
              //packet: t2@ [selfNum] @ r [myService]             
              char selfNumBuffer[8];
              char targetNumBuffer[8];
              char serviceNumBuffer[8];
              memset(&targetNumBuffer, 0, sizeof(targetNumBuffer));
              memset(&selfNumBuffer, 0, sizeof(selfNumBuffer));
              memset(&serviceNumBuffer, 0, sizeof(serviceNumBuffer));
              sprintf(selfNumBuffer, "%d\0", selfNum);
              sprintf(targetNumBuffer, "%d\0", 2);
              sprintf(serviceNumBuffer, "%d\0", myService);
              strcpy(sendBuffer, "t");
              strcat(sendBuffer, targetNumBuffer);
              strcat(sendBuffer, "@");
              strcat(sendBuffer, selfNumBuffer);
              strcat(sendBuffer, "@");
              strcat(sendBuffer, "r");
              strcat(sendBuffer, serviceNumBuffer);

              usleep(500000);
              cout << "Sending Out " << sendBuffer << endl;
              send(clientfd_node, sendBuffer, 32, MSG_DONTWAIT);

              //update connectedTime
              //gettimeofday(&tp, NULL);
              //connectedTime = tp.tv_sec * 1000 + tp.tv_usec / 1000;
              connectedTime = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;
              break;

            case 't':{
              cout << "Received 't': " << buf_node << endl;


              char toToken[32];
              strcpy(toToken, buf_node);
              token = strtok(toToken + 1, "@");
              int targetNum = atoi(token);
              cout << "Target Num: " << targetNum << endl;
              int direction = getDirection(targetNum, selfNum);

              if(direction == 0){
                cout << "Reach Target Node!\n";
                token = strtok(NULL, "@");
                int returnTargetNum = atoi(token);
                token = strtok(NULL, "@");
                if(token[0] == 'r'){
                  cout << "Application Layer: Register" << endl; 
                }
                else if(token[0] == 'A'){
                  char sendBuffer[32];
                  memset(&sendBuffer, 0, sizeof(sendBuffer));
                  char selfNumBuffer[8];
                  char targetNumBuffer[8];
                  sprintf(targetNumBuffer, "%d\0", returnTargetNum);
                  sprintf(selfNumBuffer, "%d\0", selfNum);
                  char dataNumBuffer[8];
                  if(myService == 1){
                    read_dht_data();
                    sprintf(dataNumBuffer, "%d\0", humidity);
                  }
                  else if(myService == 3){
                    changeLed();
                    sprintf(dataNumBuffer, "%d\0", ledState);
                  }
                  strcpy(sendBuffer, "t");
                  strcat(sendBuffer, targetNumBuffer);
                  strcat(sendBuffer, "@");
                  strcat(sendBuffer, selfNumBuffer);
                  strcat(sendBuffer, "@A@");
                  strcat(sendBuffer, dataNumBuffer);
                  strcat(sendBuffer, "@\0");
                  send(clientfd_node, sendBuffer, 32, MSG_DONTWAIT);
                }
                else{
                  int packetCount = atoi(token);

                  if( targetNum != TesterTargetNum ){
                    if(packetCount >= 0 && packetCount < tester.TESTTIME){
                      //struct timeval tp;
                      //gettimeofday(&tp, NULL);
                      //long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
                      long int ms = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;
                      tester.recv(packetCount, ms);
                      cout << "RTT: " << tester.RTT(packetCount) << " ms\n";
                    }
                  }

                  else{
                    if(getDirection(returnTargetNum, selfNum) == -1){
                      char sendBuffer[32];
                      memset(&sendBuffer, 0, sizeof(sendBuffer));
                      char countBuffer[8];
                      char selfNumBuffer[8];
                      char targetNumBuffer[8];
                      sprintf(targetNumBuffer, "%d\0", returnTargetNum);
                      sprintf(countBuffer, "%d\0", packetCount);
                      sprintf(selfNumBuffer, "%d\0", selfNum);
                      strcpy(sendBuffer, "t");
                      strcat(sendBuffer, targetNumBuffer);
                      strcat(sendBuffer, "@");
                      strcat(sendBuffer, selfNumBuffer);
                      strcat(sendBuffer, "@");
                      strcat(sendBuffer, countBuffer);
                      send(clientfd_node, sendBuffer, 32, MSG_DONTWAIT);
                    }
                    else{
                      string linkMAC = getMacByNum(sensor, getNum(selfNum, getDirection(returnTargetNum, selfNum)), sensorCount);
                      char sendBuffer[32];
                      memset(&sendBuffer, 0, sizeof(sendBuffer));
                      char countBuffer[8];
                      char selfNumBuffer[8];
                      char targetNumBuffer[8];
                      sprintf(targetNumBuffer, "%d\0", returnTargetNum);
                      sprintf(countBuffer, "%d\0", packetCount);
                      sprintf(selfNumBuffer, "%d\0", selfNum);
                      strcpy(sendBuffer, "t");
                      strcat(sendBuffer, linkMAC.c_str());
                      strcat(sendBuffer, targetNumBuffer);
                      strcat(sendBuffer, "@");
                      strcat(sendBuffer, selfNumBuffer);
                      strcat(sendBuffer, "@");
                      strcat(sendBuffer, countBuffer);

                      send(clientfd, sendBuffer, 32, MSG_DONTWAIT);
                    }
                  }
                }
                

              }
              else if(direction == -1){
                cout << "This Should Not Happen! (Send Packet to Nodejs)\n";
              }
              else{
                cout << "Send Packet to gatttool\n";
                string linkMAC = getMacByNum(sensor, getNum(selfNum, direction), sensorCount);
                char sendBuffer[32];
                strcpy(sendBuffer, "t");
                strcat(sendBuffer, linkMAC.c_str());
                strcat(sendBuffer, buf_node + 1);
                send(clientfd, sendBuffer, 32, MSG_DONTWAIT);
              }
            break;
          }
            default:
              cout << "Unknown Packet from Nodejs\n";
          }
      }

      if(myType == 0 && tester.isDone() != 1 && selfNum != TesterTargetNum && selfNum != -1)
      {
        //gettimeofday(&tp, NULL);
        //long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
        long int ms = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;

        if (ms - connectedTime >= interval){
          char sendBuffer[32];
          char countBuffer[8];
          char selfNumBuffer[8];
          char targetNumBuffer[8];
          memset(&sendBuffer, 0, sizeof(sendBuffer));
          memset(&countBuffer, 0, sizeof(countBuffer));
          memset(&selfNumBuffer, 0, sizeof(selfNumBuffer));
          memset(&targetNumBuffer, 0, sizeof(targetNumBuffer));
          sprintf(countBuffer, "%d\0", tester.getSendCount());
          sprintf(selfNumBuffer, "%d\0", selfNum);
          sprintf(targetNumBuffer, "%d\0", TesterTargetNum);
          strcpy(sendBuffer, "t");
          strcat(sendBuffer, targetNumBuffer);
          strcat(sendBuffer, "@");
          strcat(sendBuffer, selfNumBuffer);
          strcat(sendBuffer, "@");
          strcat(sendBuffer, countBuffer);

          cout << "Sending Out " << sendBuffer << endl;
          
          //gettimeofday(&tp, NULL);
          //ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
          long int ms = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;
          tester.send(ms);
          send(clientfd_node, sendBuffer, 32, MSG_DONTWAIT);

          connectedTime = ms;
          interval = TEST_INTERVAL;
        }
      }

      if (queueOfNodeForList.empty() != true)
      {
        //Send List: t [selfNum] @ [targetNum] @ s @ [nodeNum] @ [serviceNum]
        //gettimeofday(&tp, NULL);
        //long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
        long int ms = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;

        if (ms - listSendTime >= ListInterval){
           cout << "Queue of NodeForList is not empty! Going to Send..." << endl;
          string linkMAC = getMacByNum(sensor, getNum(selfNum, getDirection(queueOfNodeForList.front(), selfNum)), sensorCount);
          char sendBuffer[32];
          char selfNumBuffer[8];
          char targetNumBuffer[8];
          char nodeNumBuffer[8];
          char serviceNumBuffer[8];
          memset(&sendBuffer, 0, sizeof(sendBuffer));
          memset(&targetNumBuffer, 0, sizeof(targetNumBuffer));
          memset(&selfNumBuffer, 0, sizeof(selfNumBuffer));
          memset(&nodeNumBuffer, 0, sizeof(nodeNumBuffer));
          memset(&serviceNumBuffer, 0, sizeof(serviceNumBuffer));
          sprintf(selfNumBuffer, "%d\0", selfNum);
          sprintf(targetNumBuffer, "%d\0", queueOfNodeForList.front());
          sprintf(nodeNumBuffer, "%d\0", serviceMapIt->first);
          sprintf(serviceNumBuffer, "%d\0", serviceMapIt->second);
          strcpy(sendBuffer, "t");
          strcat(sendBuffer, linkMAC.c_str());
          strcat(sendBuffer, targetNumBuffer);
          strcat(sendBuffer, "@");
          strcat(sendBuffer, selfNumBuffer);
          strcat(sendBuffer, "@s@");
          strcat(sendBuffer, nodeNumBuffer);
          strcat(sendBuffer, "@");
          strcat(sendBuffer, serviceNumBuffer);

          cout << "Sending Out " << sendBuffer << endl;
          
          //gettimeofday(&tp, NULL);
          //ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
          long int ms = (long int) (clock() - start) / (double) CLOCKS_PER_SEC * 1000;
          send(clientfd, sendBuffer, 32, MSG_DONTWAIT);

          listSendTime = ms;

          ++serviceMapIt;
          //Check whether finish sending the whole list
          if(serviceMapIt == serviceMap.end()){
            cout << "Finish sending list to Node " << queueOfNodeForList.front() << endl;
            serviceMapIt = serviceMap.begin();
            //pop the Node which is asking list from the queue
            queueOfNodeForList.pop();
          }

        }
      }

    //if(getchar())
    //  tester.reset();
  }

  return 0;
}

int getNum(int parentNum, int biasNum){
  int k = floor(log(parentNum * (MaxSensorNum - 1)) / log(MaxSensorNum));
  return (pow(MaxSensorNum, k + 1) - 1) / (MaxSensorNum - 1) + (parentNum - (pow(MaxSensorNum, k) - 1) / (MaxSensorNum - 1) - 1) * MaxSensorNum + biasNum;
}

int getDirection(int targetNum, int selfNum){
  int link;
  if(targetNum == selfNum)
    return 0;
  int tmp = targetNum;
  while(tmp > selfNum){
    int p = floor(log(tmp * (MaxSensorNum - 1)) / log(MaxSensorNum));
    tmp = tmp - ( pow(MaxSensorNum, p) - 1 ) / (MaxSensorNum - 1);
    link = tmp % MaxSensorNum;
    tmp = ceil( (float)tmp / (float)(MaxSensorNum - 1)) + ( pow(MaxSensorNum, p - 1) - 1 ) / (MaxSensorNum - 1);
  }
  if( tmp < selfNum ){
    cout << "Direction: To root.\n";
    return -1;
  }
  if( tmp == selfNum ){
    cout << "Direction: To leaf. Connection Num: " << link << endl;
    return link;
  }
}

void read_dht_data()
{
	uint8_t laststate	= HIGH;
	uint8_t counter		= 0;
	uint8_t j			= 0, i;

	data[0] = data[1] = data[2] = data[3] = data[4] = 0;

	/* pull pin down for 18 milliseconds */
	pinMode( DHT_PIN, OUTPUT );
	digitalWrite( DHT_PIN, LOW );
	delay( 18 );

	/* prepare to read the pin */
	pinMode( DHT_PIN, INPUT );

	/* detect change and read data */
	for ( i = 0; i < MAX_TIMINGS; i++ )
	{
		counter = 0;
		while ( digitalRead( DHT_PIN ) == laststate )
		{
			counter++;
			delayMicroseconds( 1 );
			if ( counter == 255 )
			{
				break;
			}
		}
		laststate = digitalRead( DHT_PIN );

		if ( counter == 255 )
			break;

		/* ignore first 3 transitions */
		if ( (i >= 4) && (i % 2 == 0) )
		{
			/* shove each bit into the storage bytes */
			data[j / 8] <<= 1;
			if ( counter > 16 )
				data[j / 8] |= 1;
			j++;
		}
	}

	/*
	 * check we read 40 bits (8bit x 5 ) + verify checksum in the last byte
	 * print it out if data is good
	 */
	if ( (j >= 40) &&
	     (data[4] == ( (data[0] + data[1] + data[2] + data[3]) & 0xFF) ) )
	{
		float h = (float)((data[0] << 8) + data[1]) / 10;
		if ( h > 100 )
		{
			h = data[0];	// for DHT11
		}
		float c = (float)(((data[2] & 0x7F) << 8) + data[3]) / 10;
		if ( c > 125 )
		{
			c = data[2];	// for DHT11
		}
		if ( data[2] & 0x80 )
		{
			c = -c;
		}
		float f = c * 1.8f + 32;
		printf( "Humidity = %.1f %% Temperature = %.1f *C (%.1f *F)\n", h, c, f );
    humidity = nearbyint(h);
    celsius = nearbyint(c);
	}else  {
		printf( "Data not good, skip\n" );
	}
}

void changeLed(){
    pinMode(LED_PIN, OUTPUT);

    if(ledState == 0){
        ledState = 1;
        digitalWrite(LED_PIN, ledState);
    }
    else if(ledState == 1){
        ledState = 0;
        digitalWrite(LED_PIN, ledState);
    }
}