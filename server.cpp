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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sstream>
#include <vector>
#include <errno.h>

using namespace std;

#define PACKET_SIZE 512
#define HEADER_SIZE 10
#define DATA_SIZE 502
#define MAXLINE 4096
#define BUFFSIZE 8192
#define PORT 9877
#define WINDOW_SIZE 32

int checkSum(char pkt[], int pktLength) {
    int checksum = 0;

    for (int i = 6; i < pktLength; i++) {
        checksum += (int)pkt[i];
    }
    return checksum;
}

string preview(char* p) {
    string prev = "";
    for (int i = 4; i < 54; i++) {
        if (p[i] == '\0') {
            break;
        }
        prev = prev + p[i];
    }

    return prev;
}

string intToString(int input) {
    stringstream ss;
    ss << input;
    return ss.str();
}

void error(char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sockfd;

    struct sockaddr_in servaddr;
    struct sockaddr_in clientaddr;
    char sendBuffer[BUFFSIZE];
    char receiveBuffer[BUFFSIZE];
    // create socket file descriptor
    // through error if it fails
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&clientaddr, 0, sizeof(clientaddr));
    
    bzero(&servaddr, sizeof(struct sockaddr_in));

    // fill server information
    servaddr.sin_family = AF_INET; //IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    int serverLength = sizeof(servaddr);
    // Bind socket with server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr,
        sizeof(servaddr)) < 0) {
        perror("bind failure");
        exit(EXIT_FAILURE);
    }

    int clientLength = sizeof(clientaddr);
    cout << "Server starting..." << endl;
    while (true) {
        cout << "Server waiting..." << endl;
        // receive
        bzero(&receiveBuffer, BUFFSIZE);
        int n = recvfrom(sockfd, (char *)receiveBuffer, MAXLINE,
            MSG_WAITALL, ( struct sockaddr *) &clientaddr, (socklen_t *)&clientLength);

        if (n < 0) {
            perror("error receiving from client");
            exit(EXIT_FAILURE);
        }

        // print obtained data
        cout << "Request from clinet" << endl;
        receiveBuffer[n] = '\0';
        printf("Client : %s\n", receiveBuffer);

        // get file request
        char filename[128];
        char *pFileContent;
        sscanf(receiveBuffer, "GET %s HTTP/1.0", filename);
        FILE *pf = fopen(filename, "r");

        if (pf == NULL) {
            perror("error: file not found");
            exit(EXIT_FAILURE);
        }

        fseek(pf, 0, SEEK_END);
        int fileLength = ftell(pf);

        pFileContent = (char *)malloc(sizeof(char) * fileLength);

        if (pFileContent == NULL) {
            perror("error: memory allocation");
            exit(EXIT_FAILURE);
        }

        rewind(pf);
        fread(pFileContent, sizeof(char), fileLength, pf);
        fclose(pf);

        bzero(&receiveBuffer, BUFFSIZE);

        //send file length
        string filelength = intToString(fileLength);
        strcpy(sendBuffer, filelength.c_str());
        int sendNum = sendto(sockfd, sendBuffer, strlen(sendBuffer), 0, (struct sockaddr *)&clientaddr, clientLength);
        if(sendNum < 0) {
            perror("error: sendto");
            exit(1);
        }
        
        
        //packet[0-5]: checksum
        //packet[6-9]: sequence
        //packet[10-511]: data
        //if it is the last packet, the length will < 512
        cout << "sending..." << endl;

        int lastAck = 0;
        while (lastAck != (fileLength + (DATA_SIZE - 1)) / DATA_SIZE) {

            // initialize current window
            int window_start = lastAck;
            int window_end = window_start + WINDOW_SIZE;
            bool containsLast = false;
            if (window_end > fileLength / DATA_SIZE) {
                window_end = fileLength / DATA_SIZE;
                containsLast = true;
            }

            // send window
            for (int j = window_start; j < window_end; j++) {
                // zero out buffer
                bzero(&sendBuffer, BUFFSIZE);
                bool lastPacket = false;
                if (j > 99999) {
                    perror("error: file is too large");
                    exit(EXIT_FAILURE);
                }

                //determine whether it is the last packet
                int pktlen = PACKET_SIZE;
                if (j == fileLength / DATA_SIZE) {
                    pktlen = fileLength % DATA_SIZE + HEADER_SIZE;
                    lastPacket = true;
                }

                char pkt[pktlen];
                memset(&pkt, 0, sizeof(pkt));

                //put sequence header
                pkt[6] = j / 1000 % 10 + '0';
                pkt[7] = j / 100 % 10 + '0';
                pkt[8] = j / 10 % 10 + '0';
                pkt[9] = j % 10 + '0';

                //put data
                for(int k = HEADER_SIZE; k < pktlen; k++) {
                    pkt[k] = pFileContent[(j * DATA_SIZE) + (k - HEADER_SIZE)];
                }

                
                //put checksum header
                int csum = checkSum(pkt, pktlen);
                pkt[0] = csum / 100000 % 10 + '0';
                pkt[1] = csum / 10000 % 10 + '0';
                pkt[2] = csum / 1000 % 10 + '0';
                pkt[3] = csum / 100 % 10 + '0';
                pkt[4] = csum / 10 % 10 + '0';
                pkt[5] = csum % 10 + '0';

                //put packet in buff
                for(int k = 0; k < pktlen; k++) {
                    sendBuffer[k] = pkt[k];
                }

                //send
                cout << "Sent packet [" << j << "] size: " << sizeof(pkt) << " bytes to client" << endl;
                n = sendto(sockfd, (char *)sendBuffer, pktlen, 0, (struct sockaddr *)&clientaddr, clientLength);
                if (n < 0) {
                    perror("error: Sending packet");
                    exit(EXIT_FAILURE);
                }

                //send NULL after sending all packet
                if(lastPacket) {
                    cout << "Full file sent to client" << endl;
                    bzero(&sendBuffer, BUFFSIZE);
                    n = sendto(sockfd, (char *)sendBuffer, 0, 0, (struct sockaddr *)&clientaddr, clientLength);
                    if (n < 0) {
                        perror("error: Sending last packet");
                        exit(1);
                    }
                }

                // check for ACK/NAK
                int n = recvfrom(sockfd, (char *)receiveBuffer, MAXLINE,
                    MSG_WAITALL, ( struct sockaddr *) &clientaddr, (socklen_t *)&clientLength);

                if (n < 0) {
                    perror("error receiving from client");
                    exit(EXIT_FAILURE);
                }

                // timeout
                else if (n == 0) {
                    if (errno == EWOULDBLOCK) {
                        printf("Timeout: No ACK/NAK received");
                        break;
                    }
                }

                // print obtained data
                cout << "Message from client" << endl;
                receiveBuffer[n] = '\0';
                printf("Client : %s\n", receiveBuffer);
                
                // get message in string form
                vector<char> buffer(MAXLINE);
                string rcv;

                rcv.append(buffer.cbegin(), buffer.cend() );

                for (int k = 0; k < n; k+=4) {
                    string msg = rcv.substr(k, 3);
                    int seqNum = (int)rcv.at(k+3);
                    if (msg == "ACK") {
                        // advance window
                        if (seqNum > lastAck) {
                            lastAck = seqNum;
                        }
                    }

                    else if (msg == "NAK") {
                        lastAck = seqNum;
                        break;
                    }

                    else {
                        perror("ACK/NACK receiving error");
                        exit(EXIT_FAILURE);
                    }
                    
                }

                bzero(&receiveBuffer, BUFFSIZE);

                // timeout??
            }
        }
        cout << endl;
        free(pFileContent);
    }
    
    return 0;
}
