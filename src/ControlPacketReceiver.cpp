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
#include <functional>
#include <string>


ControlPacketReceiver::ControlPacketReceiver(std::function<std::string(std::string)> parsePacketCallback,short port){
	this->port=port;
	this->parsePacketCallback=parsePacketCallback;
	start();
}

void ControlPacketReceiver::start(){
    int retval=setupSocket();
	if(retval!=0) std::cerr << "ControlPacketReceiver::setupSocket() returned status " << retval << std::endl;
    receiverThread=std::thread(&ControlPacketReceiver::receivePackets,this);
}
int ControlPacketReceiver::setupSocket(){
	//Sets up the network socket for receiving control messages.
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
	servAddr.sin6_port = htons(port); //Port that the control stream uses

	if (bind(servFd, (struct sockaddr*)&servAddr, sizeof(servAddr)) == -1) {
		perror("bind");
		return -1;
	}
	if (listen(servFd, 10)== -1) {
		perror("listen");
		return -1;
	}
	fcntl(servFd, F_SETFD, fcntl(servFd, F_GETFD) | FD_CLOEXEC);
    return 0;
}

void ControlPacketReceiver::receivePackets(){
    while (!destroyReceiver) {
		std::cout << "Listening for control packet sender..." << std::endl;
		struct sockaddr_in6 clientAddr;
		socklen_t clientAddrLen = sizeof(clientAddr);
		int clientFd = accept(servFd, (struct sockaddr*) &clientAddr, &clientAddrLen);
		if (clientFd < 0) {
			perror("accept");
			// Don't spam the console
			sleep(1);
			continue;
		}
		std::cout << "Connection to controller established." << std::endl;
		const char* connect_msg="Connection established.\n\0";
		write(clientFd,connect_msg,strlen(connect_msg));
		while(true){
			char controlMessage[65536];
			int len = read(clientFd, controlMessage, sizeof(controlMessage)-1);
			if(len<=0){
				std::cerr << "Connection to controller broken." << std::endl;
				break; //Our connection has been broken.
			} 
			std::cout << "Received control message " << controlMessage << std::endl;
			controlMessage[len] = '\0'; //Nullchar-delimit our message.
			std::string status = parsePacketCallback(controlMessage); //Send the control packet to our external packet-parsing function.
			std::cout << "Control Message status: \n" << status << std::endl;
			int retval=write(clientFd,status.c_str(), status.length());
			if(retval<0){
				std::cerr << "Connection to controller broken." << std::endl;
				break;
			}
		}
    }

}

ControlPacketReceiver::~ControlPacketReceiver(){
	std::cout << "Destroying control packet receiver..." << std::endl;
	destroyReceiver=true;
	close(servFd);
	receiverThread.join();
	std::cout << "Control Packet Receiver destroyed." << std::endl;
}