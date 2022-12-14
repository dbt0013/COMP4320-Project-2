/*
Created by Carson Tillery and Brown Teague
Fall 2022
*/


#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>
#include <netdb.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/stat.h>

using namespace std;

#define SERV_PORT 9877 //default port
#define MAXLINE 4096 
#define BUFFSIZE 8192
#define PACKET_SIZE 512
#define HEADER_SIZE 10
#define DATA_SIZE 502


// Creates packet errors and loss
void gremlin(char pkt[], int pktLength, float probability) {
    if (1.0 * rand() / RAND_MAX > probability) {
        return;
    }
    
    if (1.0 * rand() / RAND_MAX >= 0 && 1.0 * rand() / RAND_MAX < 0.5) {
        pkt[(int)(1.0 * rand() / RAND_MAX * pktLength)] = ~pkt[(int)(1.0 * rand() / RAND_MAX * pktLength)];
    }
    else if (1.0 * rand() / RAND_MAX >= 0.5 && 1.0 * rand() / RAND_MAX < 0.8) {
        for (int i = 0; i < 2; i++) {
            pkt[(int)(1.0 * rand() / RAND_MAX * pktLength)] = ~pkt[(int)(1.0 * rand() / RAND_MAX * pktLength)];
        }
    } 
    else {
        for (int i = 0; i < 3; i++){
            pkt[(int)(1.0 * rand() / RAND_MAX * pktLength)] = ~pkt[(int)(1.0 * rand() / RAND_MAX * pktLength)];
        }
    }
}

// Creates packet delays
void delayGremlin(char pkt[], int pktLength, float probability, double delayTime) {
    if (1.0 * rand() / RAND_MAX > probability) {
        return;
    }
    
    if (1.0 * rand() / RAND_MAX >= 0 && 1.0 * rand() / RAND_MAX < 0.5) {
        sleep(delayTime / 1000);
    }
    else if (1.0 * rand() / RAND_MAX >= 0.5 && 1.0 * rand() / RAND_MAX < 0.8) {
        for (int i = 0; i < 2; i++) {
            sleep(delayTime / 1000);
        }
    } 
    else {
        for (int i = 0; i < 3; i++){
            sleep(delayTime / 1000);
        }
    }
}

// Checks the sum of data in the packet, excluding header [0-5]
int checksum(char pkt[], int pktLength) {
    int sum = 0;
    for(int i = 6; i < pktLength; i++) {
        sum += (int)pkt[i];
    }
    return sum;
}


// Checks whether packet content matches its header
bool checkPkt(char pkt[], int pktLength) {
    int sum = (pkt[0] - '0') * 100000 + (pkt[1] - '0') * 10000 + (pkt[2] - '0') * 1000 
        + (pkt[3] - '0') * 100 + (pkt[4] - '0') * 10 + (pkt[5] - '0');
    return sum == checksum(pkt, pktLength);
}


// Places the packet in its proper place in the file
void reassemblePkt(char pkt[], char *content, int pktLength) {
    int seq = (pkt[6] - '0') * 1000 + (pkt[7] - '0') * 100 + (pkt[8] - '0') * 10 + (pkt[9] - '0');
    for (int i = 0; i < pktLength - HEADER_SIZE; i++) {
        content[seq * DATA_SIZE + i] = pkt[i + HEADER_SIZE];
    }
}


int main(int argc, char *argv[]) {
    while (true) {
        srand(time(NULL));
        struct sockaddr_in servaddr; // Server address
        struct sockaddr_in cliaddr; // Client address
        char sendBuffer[BUFFSIZE];
        char receiveBuffer[BUFFSIZE];
        float errorProbability;
        float delayProbability;
        double delayTime;
        int fileLength;

        // Check input IP
        if (argc != 2) {
            perror("error: no IP");
            exit(EXIT_FAILURE);
        }

        char *phostname = argv[1];
        // Get valid IP from args
        struct hostent *pservname = gethostbyname(phostname);
        if (pservname == NULL) {
            perror("error: servname");
            exit(EXIT_FAILURE);
        }

        // Create socket
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd == 0) {
            perror("error: sockfd");
            exit(EXIT_FAILURE);
        }

        // Initialize client
        bzero(&servaddr, sizeof(struct sockaddr_in));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(SERV_PORT);
        servaddr.sin_addr.s_addr = inet_addr(phostname);
        int serverLength = sizeof(servaddr);

        // Start output
        cout << "Client start:" << endl;

        // Get packet error probability
        while (true) {
            cout << "Enter packet error probability [0, 1]: ";
            cin >> errorProbability;
            if (errorProbability >= 0 && errorProbability <= 1) {
                break;
            }
            cout << "Invalid probability, please input a numerical value between 0 and 1" << endl;
        }

        // Get packet delay probability and time
        while (true) {
            cout << "Enter packet delay probability [0, 1]: ";
            cin >> delayProbability;
            if (delayProbability > 0 && delayProbability <= 1) {
                while (true) {
                    cout << "Enter packet delay time [milliseconds]: ";
                    cin >> delayTime;
                    if (delayTime > 0) {
                        break;
                    }
                    cout << "Invalid probability, please input a numerical value greater than zero" << endl;
                }
                break;
            } else if (delayProbability == 0){
                break;
            }
            cout << "Invalid probability, please input a numerical value between 0 and 1" << endl;
        }
        

        // Send filename to server
        bzero(&sendBuffer, BUFFSIZE);
        char filename[128];
        cout << "Enter the requested filename: ";
        cin >> filename;

        // Construct request to send
        string request = "GET ";
        request += filename;
        request += " HTTP/1.0";
        cout << "client send: " << request << endl;
        strcpy(sendBuffer, request.c_str());
        int sendNum = sendto(sockfd, sendBuffer, strlen(sendBuffer), 0, (struct sockaddr *)&servaddr, serverLength);
        if (sendNum < 0) {
            perror("error: sendto");
            exit(EXIT_FAILURE);
        }
        
        // Receive server response
        int receivedNum = recvfrom(sockfd, receiveBuffer, BUFFSIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&serverLength);
        if (receivedNum < 0) {
            perror("error: recvfrom");
            exit(EXIT_FAILURE);
        }
        cout << "Server response: \n" << receiveBuffer;
        
        // Get length of file
        receivedNum = recvfrom(sockfd, receiveBuffer, BUFFSIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&serverLength);
        if (receivedNum < 0) {
            perror("error: recvfrom");
            exit(EXIT_FAILURE);
        }
        sscanf(receiveBuffer, "%d", &fileLength);
        if (fileLength == -1) {
            cout << "client end" << endl;
            break;
        }

        // Open file stream
        char *pFileContent = (char *)malloc(sizeof(char) * fileLength);
        FILE *pFile = fopen(filename, "w");
        if (pFile == NULL) {
            perror("error: open file");
            exit(EXIT_FAILURE);
        }

        // Receive packets
        cout << "Receiving packets..." << endl;
        while(true) {
            receivedNum = recvfrom(sockfd, receiveBuffer, BUFFSIZE, 0, (struct sockaddr *)&servaddr, (socklen_t *)&serverLength);
            if (receivedNum < 0) {
                perror("error: revfrom");
                exit(EXIT_FAILURE);
            }

            if (receivedNum == 0) {
                cout << "All packets received" << endl;
                break;
            }

            // Temporary packet
            int pktLength = receivedNum;
            char pkt[pktLength];
            memset(&pkt, 0, sizeof(pkt));
            for (int i = 0; i < pktLength; i++) {
                pkt[i] = receiveBuffer[i];
            }
            
            int seq = ((pkt[6] - '0') * 1000 + (pkt[7] - '0') * 100 + (pkt[8] - '0') * 10 + (pkt[9] - '0'));
            cout << "Received packet [" << seq << "] size: " << pktLength << " bytes from server | ";
            
            // Generate packet errors if error probability > 0
            gremlin(pkt, sizeof(pkt), errorProbability);
            delayGremlin(pkt, sizeof(pkt), delayProbability, delayTime);

            // Prepare client response for packet
            bzero(&sendBuffer, BUFFSIZE);
            string response = "";
            
            // Print packet and check result
            if (checkPkt(pkt, pktLength)) {
                cout << "pass" << endl;
                response += "ACK" + to_string(seq);
                reassemblePkt(pkt, pFileContent, pktLength);
            }
            else {
                cout << "error" << endl;
                response += "NAK" + to_string(seq);
            }

            strcpy(sendBuffer, response.c_str());
            int sendNum = sendto(sockfd, sendBuffer, strlen(sendBuffer), 0, (struct sockaddr *)&servaddr, serverLength);
            if (sendNum < 0) {
                perror("error: sendto");
                exit(EXIT_FAILURE);
            }
            cout << "Sent " << response << " to server \n";
        }

        // Write to file
        fwrite(pFileContent, sizeof(char), fileLength, pFile);
        fclose(pFile);
        free(pFileContent);
        cout << "file: " << filename << " has been obtained" << endl;
        char con;
        while(true) {
            cout << "continue(y/n): ";
            cin >> con;
            if(con == 'y' || con == 'n') {
                break;
            }
            cout << "wrong input" << endl;
        }
        if (con == 'n') {
            cout << "client end" << endl;
            break;
        }
    }
    
    return 0;
}
