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

#define MaxSensorNum 7

#define TesterTargetNum 9
#define ListInterval 50

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
  private:
    const static int TESTTIME = 50;
  public:
    int sendCount;
    int recvCount;
    long int totalRTT;
    long int sendTime[TESTTIME];
    long int recvTime[TESTTIME];
    Tester(){sendCount = 0; recvCount = 0; totalRTT = 0;};
    bool isDone(){return sendCount == TESTTIME;};
    int getSendCount() { return sendCount;};
    void send(long int time){sendTime[sendCount] = time; sendCount++;};
    void recv(int count, int time){
      recvTime[count] = time;
      recvCount++;
      if(count == TESTTIME - 1){
        cout << "Test Done!" << endl;
        cout << "Deliver Rate = " << 100 * recvCount / TESTTIME << "% (" << recvCount << '/' << TESTTIME << ")" << endl;
        cout << "Average RTT = " << totalRTT / recvCount << endl; 
      }
    };
    long int RTT(int count){ totalRTT += recvTime[count] - sendTime[count]; return recvTime[count] - sendTime[count];};
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

    //sockfd_scan = socket(AF_UNIX, SOCK_STREAM, 0);
    //memset(&addr_scan, 0, sizeof(addr_scan));
    //addr_scan.sun_family = AF_UNIX;
    //strcpy(addr_scan.sun_path, "/tmp/scan.socket");
    
    //bind(sockfd_scan, (struct sockaddr*)&addr_scan, sizeof(addr_scan));

    /* make it listen to socket with max 1 connections */
    //listen(sockfd_scan, 2);
    
    //scanfd = accept(sockfd_scan, NULL, NULL);


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
              }
              else{
                int packetCount = atoi(token);

                if( targetNum != TesterTargetNum ){
                    struct timeval tp;
                    gettimeofday(&tp, NULL);
                    long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
                    tester.recv(packetCount, ms);
                    cout << "RTT: " << tester.RTT(packetCount) << " ms\n";
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
              gettimeofday(&tp, NULL);
              connectedTime = tp.tv_sec * 1000 + tp.tv_usec / 1000;

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
                else{
                  int packetCount = atoi(token);

                  if( targetNum != TesterTargetNum ){
                      struct timeval tp;
                      gettimeofday(&tp, NULL);
                      long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
                      tester.recv(packetCount, ms);
                      cout << "RTT: " << tester.RTT(packetCount) << " ms\n";
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
        gettimeofday(&tp, NULL);
        long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;

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
          
          gettimeofday(&tp, NULL);
          ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
          tester.send(ms);
          send(clientfd_node, sendBuffer, 32, MSG_DONTWAIT);

          connectedTime = ms;
          interval = 500;
        }
      }

      if (!queueOfNodeForList.empty())
      {
        //Send List: t [selfNum] @ [targetNum] @ s @ [nodeNum] @ [serviceNum]
        gettimeofday(&tp, NULL);
        long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;

        if (ms - listSendTime >= ListInterval){
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
          
          gettimeofday(&tp, NULL);
          ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
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