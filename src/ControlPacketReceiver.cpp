#include "ControlPacketReceiver.hpp"
#include <string>
#include <iostream>
#include <fcntl.h>
#include <chrono>
#include <thread>

#include <opencv2/imgproc.hpp>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

void ControlPacketReceiver::start(){
    int retval=setupSocket();
    std::cout << "@setupSocket(): " << retval << std::endl;
    receiverThread=std::thread(&ControlPacketReceiver::receivePackets,this);
}
int ControlPacketReceiver::setupSocket(){
    servFd = socket(AF_INET6, SOCK_STREAM, 0);
	if (servFd < 0) {
		perror("socket");
		return -1;
	}
	
	int flag = 1;
	if (setsockopt(servFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
		perror("setsockopt");
        return -1;
	}

    struct sockaddr_in6 servAddr;
	memset(&servAddr, 0, sizeof(servAddr));
	
	servAddr.sin6_family = AF_INET6;
	servAddr.sin6_addr = in6addr_any;
	servAddr.sin6_port = htons(58000); //Port that the control stream uses

	if (bind(servFd, (struct sockaddr*)&servAddr, sizeof(servAddr)) == -1) {
		perror("bind");
		return -1;
	}
	if (listen(servFd, 10)== -1) {
		perror("listen");
		return -1;
	}
    return 0;
}

void ControlPacketReceiver::receivePackets(){
    std::cout << "@ReceivePackets thread started" << std::endl;
    while (true) {
		std::cout << "Attempting to establish connection to control packet sender..." << std::endl;
		struct sockaddr_in6 clientAddr;
		socklen_t clientAddrLen = sizeof(clientAddr);
		int clientFd = accept(servFd, (struct sockaddr*) &clientAddr, &clientAddrLen);
		if (clientFd < 0) {
			perror("accept");
			continue;
		}
		std::cout << "Connection to controller established." << std::endl;

		while(true){
			char controlMessage[65536];
			int len = read(clientFd, controlMessage, sizeof(controlMessage)-1);
			if(len<=0){
				std::cout << "Connection to controller broken." << std::endl;
				break; //Our connection has been broken.
			} 
			std::cout << "Received control message " << controlMessage << std::endl;
			controlMessage[len] = '\0'; //Nullchar-delimit our message.
			const char* status = parsePacket(controlMessage);
			std::cout << "Control Message status: " << status << std::endl;
			int retval=write(clientFd,status,strlen(status));
			if(retval<0){
				std::cout << "Connection to controller broken." << std::endl;
				break;
			}
		}
    }

}

const char* ControlPacketReceiver::parsePacket(char* controlMessage){
	const char* message=controlMessage;
	return message;
}


    
