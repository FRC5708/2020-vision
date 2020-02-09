#pragma once
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <thread>
#include <cstring>
#include <functional>
#include <string>

/* ControlPacketReceiver(std::function<const char*(char*)> parsePacketCallback, short port=58000)
** This class sets up a listening tcp socket on the specified port (58000 if not specified) 
** and sends received messages to the callback function provided to its constructor.
** If the connection is interrupted, it will wait for it to be re-established.
** This class logs its operations to stdout.
*/
class ControlPacketReceiver{
private:
    int servFd=-1;
    short port;
    std::thread receiverThread;
    volatile bool destroyReceiver=false; //Gets set to true when receiver's destructor is called. This is so we can properly clean up the network socket we create.
    int setupSocket(); //Initializes network socket.
    void receivePackets(); //Self-resetting loop that receives control packets. Runs on a seperate thread.
    std::function<std::string(char* controlMessage)> parsePacketCallback; //Callback function that parses control messages received.
public:
    ControlPacketReceiver(std::function<std::string(char*)> parsePacketCallback,short port=58000);
    ~ControlPacketReceiver();
    void start();
};