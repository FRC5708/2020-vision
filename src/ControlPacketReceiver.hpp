#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <thread>
#include <cstring>


class ControlPacketReceiver{
    int servFd=-1;
    std::thread receiverThread;
    public:
    void start();
    int setupSocket();
    void receivePackets();
    const char* parsePacket(char* controlMessage);
};