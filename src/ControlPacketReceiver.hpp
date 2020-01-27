#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <thread>
#include <cstring>
#include <functional>


class ControlPacketReceiver{
private:
    int servFd=-1;
    short port;
    std::thread receiverThread;
    int setupSocket(); //Initializes network socket.
    void receivePackets(); //Self-resetting loop that receives control packets. Runs on a seperate thread.
    std::function<const char*(char* controlMessage)> parsePacket; //Callback function that parses control messages received.
public:
    ControlPacketReceiver(std::function<const char*(char*)> parsePacket,short port=58000);
    void start();
};