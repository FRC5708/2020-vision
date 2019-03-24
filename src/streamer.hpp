#pragma once

#include <unistd.h>
#include <opencv2/core.hpp>
#include <mutex>
#include <condition_variable>
#include <optional>

#include "vision.hpp"
#include "DataComm.hpp"


class Streamer {	
	cv::Mat image;
	
	void launchGStreamer(const char* recieveAddress, int bitrate);
	const char* prevRecvAddr;

	pid_t gstreamerPID = 0, ffmpegPID = 0;
	int servFd;

	std::vector<VisionTarget>* drawTargets;
	std::optional<DataComm> computer_udp;
public:
	int width, height;

	void setDrawTargets(std::vector<VisionTarget>* drawPoints);
	
	void start(int width, int height);
	volatile bool handlingLaunchRequest = false;
	
	void relaunchGStreamer();
	
	cv::Mat getBGRFrame();

	void run(std::function<void(void)> frameNotifier); // run thread
	void launchFFmpeg(); // for loopback video
};
extern int clientFd; 