#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <math.h>
#include <string>
#include <chrono>
#include <thread>

#include <mutex>
#include <condition_variable>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

#include <signal.h>

#include "vision.hpp"
#include "streamer.hpp"
#include "DataComm.hpp"

using std::cout; using std::cerr; using std::endl; using std::string;

void DataComm::setupSocket() {
    fd = -1;
    
    // addrinfo is linked list
    struct addrinfo* addrs;
    
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    
    int error = getaddrinfo(client_name, this->port, &hints, &addrs);
    if (error != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
    }
    
    for (struct addrinfo* rp = addrs; rp != nullptr; rp = rp->ai_next) {
        
        fd = socket(rp->ai_family, rp->ai_socktype,
                        rp->ai_protocol);
        if (fd == -1) continue;
        
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        else {
            close(fd);
            fd = -1;
            perror("connect failed");
        }
    }
    freeaddrinfo(addrs);
    if (fd == -1) {
        printf("could not resolve or connect to: %s\n", client_name);
        return;
    }
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
}
DataComm::DataComm(const char* client_name, const char* port="5808") : client_name(client_name) {
    this->port=port;
    setupSocket();
}


void DataComm::sendData(VisionData data, std::chrono::time_point<std::chrono::steady_clock> timeFrom) {
    std::stringstream toSend;
    
    if (fd < 0) setupSocket();
    
    if (fd >= 0) {
    
        
        char buf[200];
        sprintf(buf, "# distance=%f tapeAngle=%f robotAngle=%f\n", data.distance, data.tapeAngle, data.robotAngle);
        toSend << buf;
        
        toSend << "@" <<
        std::chrono::duration_cast<std::chrono::milliseconds>(clock.now() - timeFrom).count()
            << endl;
        
        string sendStr = toSend.str();
        
        if (send(fd, sendStr.c_str(), sendStr.length(), 0) < 0 && errno!=EAGAIN) {
            perror("Failed to send data to robot");
            setupSocket();
        }
        cout << sendStr << std::endl;
	}
};
void DataComm::sendDraw(VisionDrawPoints* data){
    if(send(fd,(void*) data,sizeof(VisionDrawPoints),0) < 0){
        perror("Failed to send draw points!");
        setupSocket();
    }
}