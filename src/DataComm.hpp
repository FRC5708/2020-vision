#pragma once
#include <vector>
#include <chrono>
#include "vision.hpp"
class DataComm{
    int fd=-1;
    const char* client_name;
    const char* port;
    std::chrono::steady_clock clock;
public:
    void setupSocket();
    DataComm(const char* client_name,const char* port);
    void sendData(VisionData data, std::chrono::time_point<std::chrono::steady_clock> timeFrom);
    void sendDraw(VisionDrawPoints* data);
};